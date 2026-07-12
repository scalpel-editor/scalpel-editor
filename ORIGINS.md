# Origins

This repository was started fresh on 2026-07-12. It carries no git history from the projects it draws on; this file records where the imported code came from, so the full history of each piece stays reachable in its source repository.

## scintilla/

The platform-independent Scintilla 5.6.4 core, imported verbatim from the official source archive. See `scintilla/UPSTREAM.md` for the release identity, the exact list of imported and excluded paths, and the byte-for-byte verification record. License: `scintilla/License.txt`.

## seed/

Working code copied from the OnlyWayUi repository (a Wayland-only hard fork of RmlUi) at tag `scintilla-seed`, commit `5e373e9e8fd3d83c7f514f029a2299df9c1face2`. OnlyWayUi lives at `/my/src/OnlyWayUi` and remains intact; consult its history there.

These files are reference material, not the intended architecture. They show a working Scintilla-on-Wayland editor built through RmlUi's element and render abstractions, and they will be dissolved into direct code as this project takes shape. Expect them to shrink and disappear from the tree; this record is what remains.

| Path | Copied from | What it is |
| --- | --- | --- |
| `seed/editor/` | `Source/Editor/` | The Scintilla glue: `PlatOWUI` (Scintilla `Platform.h` implementation over the OnlyWayUi renderer), `ScintillaOWUI` (a `ScintillaBase` subclass), `ElementScintilla` (the RML element wrapper) |
| `seed/backends/` | `Backends/` | The Wayland platform layer: compositor connection, xdg-shell/xdg-decoration/presentation-time/xdg-foreign protocol use, xkbcommon input, cursor themes, clipboard, EGL setup, GL3 renderer, portal URI opening |
| `seed/cmake/DependenciesForBackends.cmake` | `CMake/DependenciesForBackends.cmake` | How the Wayland backend finds its dependencies and generates protocol code with wayland-scanner |
| `seed/sample/` | `Samples/basic/text_editor/` | The working text editor sample: main loop, event wiring, and the RML document that hosts `ElementScintilla` |
| `seed/LICENSE.txt` | `LICENSE.txt` | The MIT license and copyright notice covering all of the above |

The OnlyWayUi code is MIT licensed (`seed/LICENSE.txt`). Files derived from it must keep that copyright notice with them for as long as any derived code remains.
