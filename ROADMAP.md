# Roadmap

Phases run roughly in order; each lists its deliverable and how it is verified. Update this file when a phase completes or the plan changes.

Routine work uses one normal-tree target and focused Catch patterns. Widen only when that is not enough; run the complete normal workflow only for multi-concern or shared-infrastructure handoffs. The local `dev` / `asan` / `ubsan` matrix is rare: phase and release gates, explicit request, or changes that clearly need every tree. There is no hosted CI. Behavior reductions are deliberate scope changes, not behavior-preserving refactors; document and test the retained behavior in the same change.

Scope principle: the refactor changes how features are reached, not which features exist. Keep Scintilla's editing features intact and delete only material for absent platforms, the message layer itself, and the encoding paths removed in phase 3. A retained feature with no consumer yet, such as autocomplete, call tips, printing, the lexer interface, or pre-phase-7 IME machinery, stays compiled, named, and documented. The lexer interface must remain compatible with Lexilla; Lexilla integration and Markdown styling are follow-on work.

The first visual executable arrives in phase 6. Until then, the OnlyWayUi repository at tag `scintilla-seed` is a source reference, not a regression test.

## Phase 1 — Stand up the harness ✅ (2026-07-12)

The Scintilla core builds as static library `scintilla_core`; the upstream platform-free unit tests build and pass under root CMake. These tests cover document and container code but do not force all editor objects out of the static archive.

## Phase 2 — Test the editor and establish the pattern ✅ (2026-07-13)

`editorTest` links the complete `Editor` / `ScintillaBase` path against a deterministic test host, so missing editor definitions fail the link. The host exposes size, cursor, capture, invalidation, scrollbars, notifications, selection ownership, clipboard, idle and ticker requests, popup requests, and recorded drawing for assertions. Fonts and surfaces use fixed metrics; popup lists and call tips are inspectable; time and clipboard data are test-controlled. Unsupported host operations must be reported rather than made to look successful.

The fixture checks editor state and host interaction, not rendered glyphs, Wayland lifecycles, or asynchronous clipboard and IME behavior. Phase 6 replaces its test surface with the concrete renderer's deterministic offscreen path while retaining host-observation state.

`MESSAGE_REMOVAL.md` records the lasting conversion rules: classify each interface entry as an application-facing method, private editor operation, keyboard command, retained type or notification, or feature to delete; do not make an operation public merely because it appeared in `Scintilla.iface`. Retained documentation moves beside the named implementation, deleted behavior loses its documentation, and focused tests assert visible effects. The wrap-mode conversion is the kept before-and-after example. This phase also established `check.sh` and the `dev` / `asan` / `ubsan` matrix.

## Phase 3 — UTF-8 only ✅ (2026-07-13)

Document, search, regular expressions, case conversion, layout, measurement, clipboard, autocomplete, and drawing now use one UTF-8 path. Code-page APIs, DBCS files and branches, and parallel encoded surface methods are gone. `IDocument::CodePage()` and `IsDBCSLeadByte()` remain only for Lexilla compatibility and return UTF-8 and false.

Invalid UTF-8 is a tested byte-preserving policy: the document stores input unchanged; each byte outside a valid sequence acts as one character for movement, width, and deletion; `GetCharacterAndWidth` reports it as `0xDC80 + byte`; the view draws it as a hex blob; search and round-trip preserve it; case conversion leaves it unchanged.

## Phase 4 — Dissolve the dispatch ✅ (2026-07-16)

The complete interface inventory was classified and converted by concern. Retained application operations have narrow named methods, internal behavior stays protected, bindable actions use `EditorCommand`, and macro recording uses an owning typed `RecordedAction` variant and callback. Platform and embedding operations with no place in this Wayland-only editor were deleted with their documentation. Features without an initial application consumer, including autocomplete, call tips, printing, and lexing, remain compiled and tested.

The former 9,000-line `Editor.cxx` was split into greppable `Editor*.cxx` concern files with matching focused test files. `Editor.cxx` now keeps shared paint, geometry, notifications, document-watcher work, core editing and movement helpers, and the temporary dispatch shell; `ScintillaBase.cxx` keeps lifecycle, popup command routing, IME helpers, and its temporary forwarding shell. Production code no longer calls through `WndProc`, and application-facing behavior has direct named-path tests. Tests may still use thin compatibility cases to reach protected operations; phase 5 removes that temporary access.

The final phase 4 inventory accounts for all 853 callable and notification entries, with every retained callable mapped to a typed destination and every deleted callable absent from dispatch.

Deliverable: no retained behavior is reachable only through a message number; deleted behavior and documentation are gone; the only remaining message cases are thin compatibility forwarders owned by phase 5.

## Phase 5 — Delete the generated message layer ✅ (2026-07-16)

The `Editor` and `ScintillaBase` compatibility switches, coercion helpers, generated client API, parameter-packing structures, message constants, and generated catalog are gone. Tests and production callers reach behavior through named typed operations or `EditorCommand`; recording uses `RecordedAction`; retained notifications use the flat project-owned contract in `EditorNotifications.h`.

Retained core and Lexilla-facing data now have hand-maintained definitions in the concern type headers. [tools/phase5-boundary.md](tools/phase5-boundary.md) and [MESSAGE_REMOVAL.md](MESSAGE_REMOVAL.md) remain the historical classification record; same-spelling definitions required by an external contract have an explicit owner and reason.

`tools/check-no-message-layer.sh` guards the deleted boundary. The explicit editor construction/link test, the normal workflow, and the complete `dev` / `asan` / `ubsan` matrix passed.

Deliverable: the name-to-number-to-switch path and its generated client surface are gone; core and Lexilla-facing data have one project-owned definition; tests reach behavior through typed operations; repository-wide completion searches and all tests pass.

## Phase 6 — Renderer and minimal Wayland vertical slice ✅ (2026-07-22)

The first runnable editor uses one concrete FreeType / HarfBuzz / Fontconfig and OpenGL renderer for both deterministic offscreen tests and a Wayland EGL window. Text measurement, wrapping, hit testing, selection, caret placement, and drawing share cached shaped runs. The supported layout target is left-to-right English; the cache retains clusters and direction so later work can extend it. Checked-in fonts and image tests keep font metrics and rendered pixels stable.

`ApplicationEditor` owns the production Scintilla host state and services. `WaylandWindow` owns the xdg-toplevel, current seat, basic keyboard and pointer events, resize and focus changes, and the display wait; `GlContext`, `Renderer`, and `DrawSurface` own window and offscreen rendering. The event loop combines Wayland readiness with editor ticker deadlines and idle work. [tools/phase6-boundary.md](tools/phase6-boundary.md) records the detailed platform boundary and dependency decisions.

Autocomplete, call-tip, and context-menu code remains compiled, but its platform windows are explicit stubs until popup support is added after this roadmap. Clipboard, primary selection, compose, repeat, IME, cursor themes, frame pacing, presentation feedback, scaling, and robust registry and seat changes were completed in Phase 7. The absorbed renderer, platform, backend, editor-host, and sample seed files are deleted; only the Phase 8 portal URI reference remains. The normal workflow, renderer image suite, live Wayland smoke test, self-contained production headers, and the complete `dev` / `asan` / `ubsan` matrix passed at the phase gate.

## Phase 7 — Complete the Wayland shell ✅ (2026-07-23)

The standalone shell now owns robust registry, output, seat, toplevel, keyboard, pointer, cursor, clipboard, primary-selection, text-input, frame, presentation, and scale lifecycles. It supports locale-aware compose, compositor-driven repeat, text-input-v3 IME, bounded nonblocking transfers, themed and scaled cursors, integer and fractional scaling, damage-aware frame pacing, and presentation reports. Optional services are hot-pluggable and have tested fallbacks; required-global loss closes the editor cleanly.

One poll plan combines Wayland, transfer, and D-Bus descriptors with editor, repeat, transfer, and D-Bus deadlines. The shell also retains an optional xdg-foreign parent token for Phase 8 portal dialogs without owning file-dialog policy. The absorbed backend, editor-host, sample, and seed-build references are deleted; only the Phase 8 portal URI converter remains under `seed/`. [tools/phase7-boundary.md](tools/phase7-boundary.md) records the detailed ownership, protocol, fallback, coordinate, dependency, test, and source decisions.

The phase gate passed the production-header audit, normal workflow, application tests, live KWin frame and presentation smoke test, and complete `dev` / `asan` / `ubsan` matrix. Input, transfer, IME, live scale-change, and compositor-removal paths have deterministic coverage but were not driven live by the available runner.

## Phase 8 — Chrome and a shippable plain text editor

Bespoke, compile-time-known chrome: scrollbar, find bar, and only the other controls that earn their place. Add asynchronous open and save dialogs through the desktop portal and the small application-facing editor methods those features actually need. This phase ships a plain text editor; styling and Markdown come after it is solid (see "After this roadmap").

Define "daily usable" with observable behavior: dirty-buffer prompts on close and replacement, atomic saves with explicit symlink and permission handling, external-file change detection and conflict handling, a tested invalid-UTF-8 file policy, preserved or deliberately normalized line endings, clear portal cancellation and failure behavior, and a documented response to files too large for comfortable editing. Test open, edit, save, save-as, failed save, external change, and close-with-unsaved-work flows.

In progress: asynchronous portal open and save dialogs, host load/dirty/mark-saved methods, atomic whole-file saves (temp file plus rename, symlink follow and mode preserve), and Ctrl+O / Ctrl+S / Ctrl+Shift+S bindings are in place. Open refuses to start or apply while the buffer is dirty until a save/discard/cancel prompt exists. Portal response code 1 is cancel and other nonzero codes are failure; the predicted Request path is tracked before the method call. Remaining work is chrome and the rest of the file-lifecycle policy above.

Deliverable: daily-usable plain text editing that satisfies the file-lifecycle checks above, with application workflow tests and no generic UI framework.

## After this roadmap

Work the plan deliberately leaves out but keeps the door open for, in rough order of expected value:

- Lexilla integration and Markdown-aware styling, once the plain text editor is solid. The retained lexer interface is the attachment point.
- Wayland popup windows for autocomplete, call tips, and the context menu, replacing the phase 6 stubs. The core-side feature code is already retained and converted.
- Bidirectional screen-line layout, building on the direction-aware shaped-run cache from phase 6.
