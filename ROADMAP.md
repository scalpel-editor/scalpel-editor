# Roadmap

Phases run roughly in order; each lists its deliverable and how it is verified. Update this file when a phase completes or the plan changes.

Routine work uses the smallest relevant normal-tree build and focused tests, with the complete normal workflow run before a compiled-code handoff. The local `dev` / `asan` / `ubsan` matrix is the phase and release gate and is also used for broad or sanitizer-sensitive changes. There is no hosted CI. Behavior reductions are deliberate scope changes, not behavior-preserving refactors; document and test the retained behavior in the same change.

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

`DISCOVERABILITY.md` defines the fixed search benchmark. The final phase 4 inventory accounts for all 853 callable and notification entries, with every retained callable mapped to a typed destination and every deleted callable absent from dispatch. Final exact-name, held-out, cold-navigation, boundary, build, and sanitizer evidence is in `benchmark-results/phase4-final/`; under vector search over the whole repository, 27 of 33 retained natural-language queries placed the expected concern in the top three and every sampled implementation was reachable within two searches.

Deliverable: no retained behavior is reachable only through a message number; deleted behavior and documentation are gone; the concern split meets the checks in [DISCOVERABILITY.md](DISCOVERABILITY.md); the only remaining message cases are thin compatibility forwarders owned by phase 5.

## Phase 5 — Delete the generated message layer

Remove the two compatibility switches and the generated client API without losing the typed core API, Lexilla attachment surface, notifications, or focused coverage. Focused normal-tree tests are used while iterating, the complete normal workflow runs before each compiled-code handoff, and step 11 runs the three-tree phase gate.

1. Freeze the removal boundary. ✅ (2026-07-16) Recorded in [tools/phase5-boundary.md](tools/phase5-boundary.md): which generated enums, constants, structures, and notifications production core, Lexilla-facing interfaces, and tests still use; each classified as retained project data or client/message-only; completion check `tools/check-no-message-layer.sh`; phase 4 inventory kept as historical evidence in `MESSAGE_REMOVAL.md`.
2. Remove message-shaped parameters from named code. ✅ (2026-07-16) Named production operations take domain types: caret/visible policies, selection mode, search flags and needle, text width style index, margin index, and optional colours. Temporary `WndProc` shells convert at the boundary only. `CommandFromMessage` remains shell-only until step 6. Recording replay and focused recording tests use the typed forms.
3. Convert the `Editor` concern tests off messages. ✅ (2026-07-16) Application-facing calls and parity checks use public named methods or `RunCommand`; protected concern operations are exposed only through narrow typed methods or thin forwards on the test-only `TestEditor`. All `Editor*Test` suites outside the ScintillaBase-owned set (autocomplete, call tips, lexing, popup host) reach behavior without `WndProc` / `Message::`. `DefWndProc` observation and `ScintillaMessages.h` remain for step 4 shell tests.
4. Convert the `ScintillaBase` and host tests off messages. ✅ (2026-07-16) Autocomplete, call tips, lexing, and popup-host suites use named typed entry points only. `TestEditor` no longer records fall-through messages; its `DefWndProc` override remains empty until step 6 deletes the pure virtual. No test calls `WndProc`. Production shells remain for steps 5–6; `TestNotification` still carries message-shaped fields until step 7.
5. Delete the `ScintillaBase::WndProc` forwarding shell. ✅ (2026-07-16) Removed its declaration, definition, message includes, parameter conversions, and the comments that describe temporary forwarding. Autocomplete, call-tip, lexing, popup-host, and full `editorTest` (link + all cases) pass. Only `Editor::WndProc` remains for step 6.
6. Delete the `Editor::WndProc` shell and compatibility helpers. ✅ (2026-07-16) Removed `WndProc`, `DefWndProc`, `StringResult`, `BytesResult`, pointer and integer coercion helpers, `CommandFromMessage`, packed key-mod unpack helpers, and message-only includes. Notification packing fields no longer use the generated `Message` type; `ScintillaMessages.h` is deleted. The concrete editor construction/link test remains so the static archive cannot hide a missing named definition.
7. Replace the generated notification contract. ✅ (2026-07-16) Project-owned `EditorNotifications.h` holds retained `Notification` kinds and flat `NotificationData` (no `NotifyHeader`, no `message` / `wParam` / `lParam`). Production emitters and `TestEditor` use typed fields only. `MacroRecord`, `Key`, and `URIDropped` are gone from the kind enum; recording stays on `RecordedAction`. `EditorNotificationsTest` covers every retained kind.
8. Replace generated range and printing structures. ✅ (2026-07-16) `FormatRange` takes `Sci::Position` bounds, `PRectangle`, and platform `SurfaceID` values (view side uses `Surface *`). Named text-range and search paths already used buffer + positions / target search. Client packing types (`CharacterRange*`, `TextRange*`, `TextToFind*`, `RangeToFormat*`, client `Rectangle`) and `ScintillaStructures.h` are gone. Dead `*Full` declarations are also gone from `ScintillaCall.h`; parallel C structs remain in `Scintilla.h` until step 10.
9. Replace generated enums and constants in concern-sized batches. ✅ (2026-07-16) Retained definitions live in hand-maintained `scintilla/src/EditorBasicTypes.h`, `EditorDocumentTypes.h`, `EditorStyleTypes.h`, `EditorInputTypes.h`, and `EditorLayoutTypes.h`. `Accessibility` and `ScaleTechnique` were deleted as client/message-only. `uptr_t` / `sptr_t` and `ScintillaTypes.h` are gone from production and tests.
10. Delete the remaining generated client and documentation surface. Delete `ScintillaCall.h`, `Scintilla.h` after its retained data has moved, `Scintilla.iface`, and GTK-only `ScintillaWidget.h`; do not leave forwarding compatibility headers. Delete `ScintillaDoc.html` after confirming each retained feature has useful documentation beside its implementation and every deleted feature's prose is gone. Update includes, CMake, `MESSAGE_REMOVAL.md`, discoverability data, and phase 4 inventory tools so nothing expects the deleted interface input.
11. Run completion searches and the phase gate. Update grepai for every replacement header and affected source, run exact-name and cold-navigation checks, and record the results. Search the whole repository for the forbidden forms below, run the normal workflow, run the editor link test explicitly, and run `./check.sh` before marking the phase complete.

Completion searches must find no `WndProc` / `DefWndProc`, `Message` command type, `SCI_*` or `SCN_*` message constants, `ScintillaMessages.h`, generated-section markers, client parameter packing, or documentation that directs readers through the deleted number-to-switch path. Any same-spelling term retained by an external contract must be listed explicitly with its owner and reason.

Deliverable: the name-to-number-to-switch path and its generated client surface are gone; core and Lexilla-facing data have one project-owned definition; tests reach behavior through typed operations; repository-wide completion searches and all tests pass.

## Phase 6 — Renderer and minimal Wayland vertical slice

Build the renderer together with the smallest shell needed to verify it: a Wayland connection, xdg-toplevel, EGL context and surface, event loop, resize and focus handling, basic keyboard and pointer input, timers, and a visible editor buffer. This is the first runnable program in the repository. The renderer and shell are also meant to be read: they are this project's worked example of a C++ Wayland application without a GUI toolkit, so favor clear structure and comments that explain protocol decisions, and keep them an example to read, not a library to import. Keep a deterministic offscreen or image-comparison path for renderer tests so drawing is checked without relying only on manual inspection, and use a checked-in test font with a compatible license so system font changes cannot move expected pixels or caret positions.

Collapse `Platform.h` into the one concrete implementation, mining `seed/editor/PlatOWUI.cxx` and `seed/backends/OnlyWayUi_Renderer_GL3.cpp` for working knowledge. Use FreeType for font metrics and glyph rasterization, HarfBuzz for shaping, and Fontconfig to find system fonts and fallbacks. Shape text once into a cached run containing glyphs, advances, offsets, input-byte clusters, and valid caret stops; measurement, wrapping, hit testing, selection, and drawing must all consume that same result. Correct left-to-right layout with required shaping is the goal; disable discretionary ligatures. Bidirectional screen-line layout is not on this roadmap (HarfBuzz shapes directional runs but does not order a mixed-direction line, and that ordering step is a project of its own). Leave the door open for it: keep the shaped-run cache carrying cluster and direction information, and do not collapse the run model into per-character assumptions that reordering would have to undo.

`ListBox`, `Menu`, and the call-tip window serve autocomplete, call tips, and the context menu, none of which a plain text editor exercises yet. Keep their core-side code compiled and retained under the scope principle; the Wayland platform gives them explicit not-yet-implemented stubs that record the request or fail loudly, never fake success. Implementing real popup windows is follow-on work. Delete the absorbed `PlatOWUI` and renderer seed files while retaining the backend references still needed by phase 7, and preserve the OnlyWayUi license notice on derived code.

Deliverable: a minimal editor runs in a Wayland window and draws through one renderer with no product abstraction; measured byte positions, caret stops, selection bounds, and rendered glyph positions agree for ASCII, kerning pairs, combining sequences, required shaping, and font fallback; automated image checks cover clipping, text, caret, selection, and scrolling.

## Phase 7 — Complete the Wayland shell

Finish the window and input layer by mining the parts that actually exist in `seed/backends/`: compositor and seat lifecycle, xdg-toplevel and decoration handling, cursor themes, EGL damage, clipboard transfers, presentation feedback, and portal event-loop support. Pace drawing with `wl_surface.frame`; use presentation-time only to learn when a submitted frame was displayed.

Implement the features the seed does not contain from the relevant protocol and library sources: xkbcommon compose and key repeat, text-input-v3 for IME, primary selection, fractional scaling and viewporter support, output and buffer-scale changes, and their fallback behavior when an optional protocol is unavailable. Keep pointer, caret, font, cursor, damage, and buffer coordinates consistent across scale changes. Absorb the asynchronous D-Bus watches, timers, and xdg-foreign parent handle needed by the phase 8 file chooser without putting file-dialog policy in the shell.

Deliverable: keyboard compose and repeat, IME pre-edit and commit, pointer input, cursors, clipboard, primary selection, focus and seat changes, scaling, resize, frame callbacks, and presentation feedback work in the standalone editor; protocol-unavailable paths are tested; all absorbed backend seed files are deleted with required license notices retained.

## Phase 8 — Chrome and a shippable plain text editor

Bespoke, compile-time-known chrome: scrollbar, find bar, and only the other controls that earn their place. Add asynchronous open and save dialogs through the desktop portal and the small application-facing editor methods those features actually need. This phase ships a plain text editor; styling and Markdown come after it is solid (see "After this roadmap").

Define "daily usable" with observable behavior: dirty-buffer prompts on close and replacement, atomic saves with explicit symlink and permission handling, external-file change detection and conflict handling, a tested invalid-UTF-8 file policy, preserved or deliberately normalized line endings, clear portal cancellation and failure behavior, and a documented response to files too large for comfortable editing. Test open, edit, save, save-as, failed save, external change, and close-with-unsaved-work flows.

Deliverable: daily-usable plain text editing that satisfies the file-lifecycle checks above, with application workflow tests and no generic UI framework.

## After this roadmap

Work the plan deliberately leaves out but keeps the door open for, in rough order of expected value:

- Lexilla integration and Markdown-aware styling, once the plain text editor is solid. The retained lexer interface is the attachment point.
- Wayland popup windows for autocomplete, call tips, and the context menu, replacing the phase 6 stubs. The core-side feature code is already retained and converted.
- Bidirectional screen-line layout, building on the direction-aware shaped-run cache from phase 6.
