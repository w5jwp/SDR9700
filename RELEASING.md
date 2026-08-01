# Releasing SDR9700

Every GitHub release must include substantive, maintainer-readable release
notes. GitHub-generated notes may be used as source material, but a changelog
link by itself is not an acceptable release description.

## Release checklist

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
