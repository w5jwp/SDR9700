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
qtpaths_path="$(command -v qtpaths6 || command -v qtpaths || true)"
if [ -z "${deployqt_path}" ] || [ -z "${qtpaths_path}" ]; then
    echo "macdeployqt and qtpaths are required on the build machine." >&2
    exit 1
fi

qt_plugins_path="$("${qtpaths_path}" --plugin-dir)"
if [ ! -d "${qt_plugins_path}" ]; then
    echo "Qt plugin directory not found: ${qt_plugins_path}" >&2
    exit 1
fi

# SDR9700 uses Qt Widgets, Qt Network, and Qt Multimedia. Stage only the
# corresponding native macOS runtime plugins instead of deploying every plugin
# installed on the build machine.
plugin_files="
platforms/libqcocoa.dylib
styles/libqmacstyle.dylib
multimedia/libdarwinmediaplugin.dylib
networkinformation/libqapplenetworkinformation.dylib
tls/libqsecuretransportbackend.dylib
"

rm -rf "${plugins_path}"
deployqt_args=""
for plugin_file in ${plugin_files}; do
    source_file="${qt_plugins_path}/${plugin_file}"
    destination_file="${plugins_path}/${plugin_file}"
    if [ ! -f "${source_file}" ]; then
        echo "Required Qt plugin not found: ${source_file}" >&2
        exit 1
    fi
    mkdir -p "$(dirname "${destination_file}")"
    /usr/bin/ditto "${source_file}" "${destination_file}"
    deployqt_args="${deployqt_args} -executable=${destination_file}"
done

# macdeployqt rewrites the application and staged plugins to use libraries
# inside Contents/Frameworks. Third-party libraries linked by SDR9700 are
# discovered from the build machine and copied transitively.
# shellcheck disable=SC2086
"${deployqt_path}" "${app_path}" \
    -verbose=1 \
    -always-overwrite \
    -no-plugins \
    -no-codesign \
    ${deployqt_args}

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
