# Releasing SDR9700

Every GitHub release must include substantive, maintainer-readable release
notes. GitHub-generated notes may be used as source material, but a changelog
link by itself is not an acceptable release description.

## Version naming

Stable releases use the numeric version directly, for example `26.8.2`.
Prereleases append a hyphenated prerelease identifier, starting with
`-beta.1` and incrementing the final number for each subsequent beta, for
example `26.8.2-beta.1` and `26.8.2-beta.2`.

Keep the CMake project version numeric because CMake's `project(VERSION)`
field does not accept prerelease suffixes. For a beta of `26.8.2`, set the
project version to `26.8.2` and `SDR9700_DISPLAY_VERSION` to the complete
prerelease version such as `26.8.2-beta.1`.

Do not include a leading `v` in `SDR9700_DISPLAY_VERSION`. The application
adds that prefix when it builds the title bar, which must read
`SDR9700 v<version>` (for example, `SDR9700 v26.8.2-beta.1`).

Git tags always add a leading `v`. A beta release therefore uses a tag such
as `v26.8.2-beta.1`, the title `SDR9700 v26.8.2-beta.1`, and must be marked as
a GitHub prerelease. Create it with:

```bash
gh release create v26.8.2-beta.1 --prerelease \
  --title "SDR9700 v26.8.2-beta.1" --notes-file <file>
```

## Stable release checklist

1. Set `SDR9700_DISPLAY_VERSION` in `CMakeLists.txt` to the final version.
2. Run a clean Release build with `make release`.
3. Run the complete test suite with
   `ctest --test-dir src/build --output-on-failure`.
4. Write release notes that summarize user-visible highlights, improvements,
   and fixes since the previous stable release. End with the full changelog
   comparison link.
5. Commit and push the version change.
6. Publish the release as `SDR9700 v<version>` with tag `v<version>`, supplying
   the authored notes with `gh release create --notes-file <file>`. Do not rely
   on `--generate-notes` without reviewing and expanding its output first.
7. Read the published release back with `gh release view` and verify that it is
   neither a draft nor a prerelease, its title and tag are correct, and its body
   contains the authored notes rather than only a changelog link.
8. Confirm that the macOS release workflow started and will attach the signed,
   notarized Apple Silicon DMG to the release.
