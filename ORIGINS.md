# Origins

This repository was started fresh on 2026-07-12. It carries no git history from the projects it draws on; this file records where the imported code came from, so the full history of each piece stays reachable in its source repository.

## scintilla/

The platform-independent Scintilla 5.6.4 core, imported verbatim from the official source archive. See `scintilla/UPSTREAM.md` for the release identity, the exact list of imported and excluded paths, and the byte-for-byte verification record. License: `scintilla/License.txt`.

## seed/

Working code copied from the OnlyWayUi repository (a Wayland-only hard fork of RmlUi) at tag `scintilla-seed`, commit `5e373e9e8fd3d83c7f514f029a2299df9c1face2`. OnlyWayUi lives at `/my/src/OnlyWayUi` and remains intact; consult its history there.

These files are reference material, not the intended architecture. The original snapshot showed a working Scintilla-on-Wayland editor built through RmlUi's element and render abstractions, and it is dissolved into direct code as this project takes shape. The retained tree is deliberately partial: absorbed files are deleted even when the remaining historical CMake or wrapper sources still name them. It does not build in this repository.

The removed `PlatOWUI` measured and drew through OnlyWayUi's default FreeType font engine, and its Scintilla screen-line layout method was unimplemented. It supplied working knowledge for basic left-to-right surface wiring, not for HarfBuzz shaping or bidirectional layout. OnlyWayUi's separate `Samples/basic/harfbuzz/` demonstrates FreeType and HarfBuzz integration, but it does not provide the per-input-byte measurements Scintilla requires; Phase 6 built that mapping from shaped clusters and uses it consistently for measurement, caret placement, selection, and drawing.

The copied Wayland backend paces redraws with `wl_surface.frame` callbacks and uses presentation-time to report when submitted frames were displayed. It does not implement xkbcommon compose, text-input-v3, primary selection, fractional scaling, or viewporter support. Those phase 7 features require new code based on their protocol and library sources; the seed is not a reference implementation for them.

| Path | Copied from | What it is |
| --- | --- | --- |
| `seed/editor/` | `Source/Editor/` | Remaining historical `ScintillaOWUI` host and `ElementScintilla` RML wrapper; their absorbed `PlatOWUI` dependency was removed after Phase 6 |
| `seed/backends/` | `Backends/` | Remaining Wayland platform layer: compositor connection, xdg-shell/xdg-decoration/presentation-time/xdg-foreign protocol use, basic xkbcommon input, cursor themes, clipboard, EGL setup, asynchronous desktop-portal open/save dialogs, and portal URI conversion; the absorbed GL3 renderer was removed after Phase 6 |
| `seed/cmake/DependenciesForBackends.cmake` | `CMake/DependenciesForBackends.cmake` | How the Wayland backend finds its dependencies and generates protocol code with wayland-scanner |
| `seed/sample/` | `Samples/basic/text_editor/` | The working text editor sample: main loop, event wiring, and the RML document that hosts `ElementScintilla` |
| `seed/LICENSE.txt` | `LICENSE.txt` | The MIT license and copyright notice covering all of the above |

The OnlyWayUi code is MIT licensed (`seed/LICENSE.txt`). Files derived from it must keep that copyright notice with them for as long as any derived code remains.
