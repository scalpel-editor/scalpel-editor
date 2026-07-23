# Origins

This repository was started fresh on 2026-07-12. It carries no git history from the projects it draws on; this file records where the imported code came from, so the full history of each piece stays reachable in its source repository.

## scintilla/

The platform-independent Scintilla 5.6.4 core, imported verbatim from the official source archive. See `scintilla/UPSTREAM.md` for the release identity, the exact list of imported and excluded paths, and the byte-for-byte verification record. License: `scintilla/License.txt`.

## seed/

Reference code was copied from the OnlyWayUi repository (a Wayland-only hard fork of RmlUi) at tag `scintilla-seed`, commit `5e373e9e8fd3d83c7f514f029a2299df9c1face2`. OnlyWayUi lives at `/my/src/OnlyWayUi` and remains intact; consult its history there.

The original snapshot showed a working Scintilla-on-Wayland editor built through RmlUi's element, renderer, and backend abstractions. Phases 6 and 7 replaced the useful renderer, platform, shell, input, transfer, frame, scaling, and event-loop techniques with direct production code, then deleted the absorbed backend, editor-host, sample, and build references.

The retained seed tree contains only `seed/backends/OnlyWayUi_Portal_Uri.cpp` and `.h`, copied from `Backends/OnlyWayUi_Portal_Uri.*`. They remain reference material for converting Phase 8 desktop-portal results into file paths and do not build in this repository.

The removed `PlatOWUI` measured and drew through OnlyWayUi's default FreeType font engine, and its Scintilla screen-line layout method was unimplemented. It supplied working knowledge for basic left-to-right surface wiring, not for HarfBuzz shaping or bidirectional layout. OnlyWayUi's separate `Samples/basic/harfbuzz/` demonstrates FreeType and HarfBuzz integration, but it does not provide the per-input-byte measurements Scintilla requires; Phase 6 built that mapping from shaped clusters and uses it consistently for measurement, caret placement, selection, and drawing.

The removed Wayland backend paced redraws with `wl_surface.frame` callbacks and used presentation-time to report displayed frames. It did not implement xkbcommon compose, text-input-v3, primary selection, fractional scaling, or viewporter support; Phase 7 implemented those features from the installed protocol and library sources.

The OnlyWayUi code is MIT licensed (`LICENSES/OnlyWayUi.txt`). Files derived from it must keep that copyright notice with them for as long as any derived code remains.
