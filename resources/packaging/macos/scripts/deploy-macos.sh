#!/bin/sh

set -eu

if [ "$(uname -s)" != "Darwin" ]; then
    echo "The macOS deployment script can only run on macOS." >&2
    exit 1
fi

app_path="${1:-src/build/bin/SDR9700.app}"
contents_path="${app_path}/Contents"
plugins_path="${contents_path}/PlugIns"

if [ ! -x "${contents_path}/MacOS/SDR9700" ]; then
    echo "SDR9700 application bundle not found at ${app_path}" >&2
    exit 1
fi

deployqt_path="$(command -v macdeployqt || true)"
if [ -z "${deployqt_path}" ]; then
    echo "macdeployqt is required on the build machine." >&2
    exit 1
fi

# Let Qt's deployment tool select plugins for this Qt build. Plugin filenames
# and backend composition change between Qt releases; maintaining a local list
# made routine Homebrew Qt upgrades unnecessarily brittle.
rm -rf "${plugins_path}"
"${deployqt_path}" "${app_path}" \
    -verbose=1 \
    -always-overwrite \
    -no-codesign

# Homebrew libraries may retain an absolute LC_ID_DYLIB even after their
# consumers have been rewritten. The ID is not used to locate that same file,
# but normalize it so no bundle load metadata refers to the build machine.
frameworks_path="${contents_path}/Frameworks"
while IFS= read -r binary_path; do
    if file "${binary_path}" | grep -q "Mach-O"; then
        install_id="$(otool -D "${binary_path}" 2>/dev/null | tail -n +2 | head -n 1)"
        if [ -n "${install_id}" ] && echo "${install_id}" | grep -q '^/opt/homebrew'; then
            relative_path="${binary_path#"${frameworks_path}/"}"
            install_name_tool -id "@rpath/${relative_path}" "${binary_path}"
        fi
    fi
done <<EOF
$(find "${frameworks_path}" -type f)
EOF

homebrew_references=""
while IFS= read -r binary_path; do
    if file "${binary_path}" | grep -q "Mach-O"; then
        references="$(otool -L "${binary_path}" | awk 'NR > 1 { print $1 }' | grep '^/opt/homebrew' || true)"
        if [ -n "${references}" ]; then
            homebrew_references="${homebrew_references}
${binary_path}
${references}"
        fi
    fi
done <<EOF
$(find "${contents_path}" -type f)
EOF

if [ -n "${homebrew_references}" ]; then
    echo "Bundle still contains Homebrew library references:" >&2
    echo "${homebrew_references}" >&2
    exit 1
fi

# Sign only after all bundle contents and load commands are final. This ad-hoc
# signature is for local testing; release signing is a separate packaging step.
codesign --force --deep --sign - "${app_path}"
codesign --verify --deep --strict "${app_path}"

echo "Created self-contained Apple Silicon bundle: ${app_path}"
