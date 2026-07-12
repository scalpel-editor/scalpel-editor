# Scintilla upstream record

## Release

- Project: Scintilla
- Advertised release: 5.6.4
- Upstream version file: `564`
- Official source archive: https://www.scintilla.org/scintilla564.zip
- Archive SHA-256: `3ffd69532649556978cdbe7ebe1293d9cf3259b290a7974a71e77a32bac50e12`
- Import date: 2026-07-12 (seeded from the OnlyWayUi vendored copy imported 2026-07-10)

## Source identity

The archive's `.hg_archival.txt` identifies Mercurial changeset `ec5a4a39853007fa6b811ee7646cd154c8672069` on the `default` branch. It reports `rel-5-6-4` as the latest tag at a distance of one changeset. The archive's `.hgtags` maps that tag to changeset `c9445d827f82c1b29dd448725be97fd044e3f8b4`.

This one-changeset difference is present in the official 5.6.4 release archive. The imported files come from the archive contents, not from a reconstructed tag snapshot.

On 2026-07-12 every imported file was verified byte-identical against a fresh download of the archive (SHA-256 matched the value above).

## Imported paths

- `.hg_archival.txt`
- `License.txt`
- `version.txt`
- `include/`
- `src/`
- `test/unit/` — upstream's platform-free unit tests, kept as the safety net for refactoring
- `ScintillaDoc.html` — from the archive's `doc/ScintillaDoc.html`, relocated to the top of this directory; source material for moving per-message documentation onto named methods

## Excluded paths

This project keeps the platform-independent Scintilla core and builds its own Wayland-only editor around it. The following archive paths were excluded:

- Platform ports and their build files: `cocoa/`, `gtk/`, `qt/`, and `win32/`
- Platform-driven tests: everything under `test/` except `test/unit/` (the rest is Python-driven and needs a platform binary)
- Documentation and project files other than `ScintillaDoc.html`: the rest of `doc/`, `.editorconfig`, `.hgeol`, `.hgignore`, `.hgtags`, `CONTRIBUTING`, `README`, and `cppcheck.suppress`
- Generated-call source, release scripts, and packaging helpers: `bin/`, `call/`, `scripts/`, `delbin.bat`, `tgzsrc`, and `zipsrc.bat`

## Local patches

None. All imported upstream files are unchanged. This `UPSTREAM.md` file is an audit record for this project and is not part of the source archive.
