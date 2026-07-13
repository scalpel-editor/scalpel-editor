# Roadmap

Phases run roughly in order; each lists its deliverable and how it is verified. Update this file when a phase completes or the plan changes.

The phase 1 tests cover the platform-free document and container code well, but they do not instantiate `Editor` or `ScintillaBase`. Because `scintilla_core` is a static library, the test executable also does not link the editor objects it never calls. Phase 2 closes that gap before the dispatch refactor begins. The first visual executable arrives in phase 6; until then, the OnlyWayUi repository (tag `scintilla-seed`) can answer questions about the original core, but it is a reference rather than a regression test for the refactored code.

Phase 2 added the local check matrix. Every later phase must pass it: the normal build, the focused editor tests, and the AddressSanitizer and UndefinedBehaviorSanitizer builds. Development is local; there is no hosted CI. The matrix is one script (or CMake workflow preset) that configures, builds, and tests all three trees, and running it green is part of the definition of done for each reviewable step. Behavior reductions are deliberate scope changes, not behavior-preserving refactors; document and test the retained behavior in the same change.

Scope principle: the refactor changes how features are reached, not which features exist. Keep Scintilla's editing features intact: the refactored core is only a meaningful artifact if it is recognizably Scintilla — full capability, made legible — rather than a small editor assembled from Scintilla parts. Delete only what serves other platforms or the message layer itself, plus the encoding reduction already decided in phase 3. Feature breadth and compatibility are separate questions: this project owes nothing to message numbers, upstream mergeability, or existing container code; it is the features themselves that are kept. A retained feature with no consumer in this editor yet — autocomplete, call tips, the lexer interface, IME machinery before phase 7 — stays compiled, named, and documented rather than deleted. Styling is expected to come through Lexilla eventually, so the lexer interface must stay compatible with it; wiring Lexilla in and Markdown styling are follow-on work after this roadmap (see "After this roadmap").

## Phase 1 — Stand up the harness ✅ (2026-07-12)

Create CMake for the Scintilla core and the upstream unit tests in `scintilla/test/unit`, and get them green under `ctest`. Document the build and test commands in AGENTS.md in the same change.

Deliverable: `cmake --preset dev && cmake --build build && ctest --test-dir build` works. This is the baseline safety net for the platform-free code; phase 2 extends it through the editor.

Done: the whole core compiles as static library `scintilla_core` (GCC 15, `-Wall -Wextra`, zero warnings); the upstream tests build against it and pass — 3,816 assertions in 52 test cases. `UnitTester.cxx` is excluded from the test build; it is an alternate `main()` for the Visual Studio build, matching the upstream GNU makefile.

## Phase 2 — Test the editor and establish the pattern ✅ (2026-07-13)

Add a concrete test editor and test-only platform implementation that link the whole editor path and expose invalidation, scrollbar, notification, clipboard, and drawing effects for assertions. Trace `SCI_SETWRAPMODE` end to end and dissolve it into a named public method because wrap mode is part of the application's intended surface. Move and condense its `ScintillaDoc.html` prose into a doc comment, leave the switch case as a temporary forwarding call, and test both the state change and its redraw, horizontal-scroll, and scrollbar effects. During the transition, compare the direct method with the forwarding path where that provides a useful mechanical check.

Use this exercise to define a classification applied before converting each later message: application-facing method, private editor operation, keyboard command, retained type or notification, or feature to delete. Under the scope principle, the deletion class covers only platform-only material and the message layer itself. Do not turn every interface entry into a public method. Generate a concern-grouped inventory from `Scintilla.iface`, write a short before-and-after note tracing wrap mode through the message architecture and through the named-method result — the one demonstration write-up this project keeps — and add the local check matrix: `asan` and `ubsan` configure presets alongside `dev`, each in its own build tree, plus a single command that configures, builds, and tests all three. Document the matrix and its definition-of-done role in AGENTS.md in the same change.

Deliverable: the full editor path links in a test executable; wrap mode and its side effects have focused tests; the five-way classification and documentation pattern are written down; the wrap-mode before-and-after note exists; the local check matrix runs all three build trees with one command and passes.

Done: `editorTest` links the full `Editor` and `ScintillaBase` path against the deterministic host below. The wrap-mode cases cover the direct method, the temporary forwarding message, visible host effects, and repeated-setting behavior. `MESSAGE_REMOVAL.md` defines the classification and documentation pattern, keeps the wrap trace, and inventories all callable interface entries and notifications by concern. The local `dev`, `asan`, and `ubsan` matrix passes through `check.sh`.

### Concrete test editor contract

"Concrete test editor" means a test-only final subclass of `ScintillaBase` that can be constructed, given a fixed client rectangle, loaded with text, exercised through named editor methods and input methods, and destroyed without Wayland, EGL, OnlyWayUi, or another process. Instantiating it in the unit-test executable must pull `Editor.cxx`, `ScintillaBase.cxx`, and their required core objects through the static-library link, so unresolved editor definitions fail the build.

The fixture owns deterministic test implementations of the current `Platform.h` contracts. Its window stores size, cursor, capture, and invalidated rectangles in memory. Its surface and font return fixed metrics, fill every requested UTF-8 byte position, and record drawing commands instead of producing pixels. Clipboard reads and writes are synchronous in-memory operations. Time advances only when a test requests it. List boxes, menus, call-tip windows, and other features not under test must either record an explicit request or report that they are unsupported; they must not silently make the observed operation look successful.

Every host callback that can reveal editor behavior has inspectable state: horizontal and vertical scroll updates, scrollbar range and page changes, invalidation, parent notifications with pointed-to text copied before the callback returns, selection ownership, clipboard contents, mouse capture, idle and ticker requests, popup requests, and calls that fall through to `DefWndProc`. Tests can clear this observation state without reconstructing the document, and a compact snapshot supports comparisons between a temporary message-forwarding path and its named replacement.

The first contract tests construct and destroy the fixture under the sanitizers, set and retrieve text, and change the client rectangle. The wrap-mode test starts with a nonzero horizontal offset, enables wrapping, and verifies the new wrap state, zero horizontal offset, redraw request, horizontal-scroll update, and scrollbar reconfiguration. Repeating the same setting must produce no change-only effects. While the forwarding case exists, the same operation through `WndProc` on a fresh fixture must produce the same snapshot as the named method.

This fixture is an editor-state and host-interaction test tool. It is not evidence that glyphs look correct, that Wayland lifecycles are correct, or that asynchronous clipboard and IME behavior works; phase 6 renderer checks and phase 7 shell tests cover those boundaries. The test platform must not become a reason to retain `Platform.h`: when phase 6 installs the concrete renderer, replace the test surface with that renderer's deterministic offscreen path and retain only the test editor's host-observation state.

## Phase 3 — UTF-8 only

Remove DBCS and code-page support before it can shape the new API: delete `DBCS.cxx`, code-page messages and documentation, non-UTF-8 branches in `Document`, `EditView`, case conversion, search, and measurement, and code-page parameters threaded through call chains. Collapse the parallel encoded and UTF-8 surface methods into one UTF-8 path. Decide and test what happens when invalid UTF-8 reaches the editor boundary so the invariant is explicit rather than assumed.

Deliverable: no code path asks which encoding is in use; no API accepts a code page; retained UTF-8 behavior and the invalid-input policy have focused tests; adjusted upstream tests pass.

## Phase 4 — Dissolve the dispatch

Apply the phase 2 classification across the `WndProc` switches in `Editor.cxx` and `ScintillaBase.cxx`, grouped by concern. Retained application operations become narrow named methods, internal behavior stays private, key bindings use a dedicated command type, and platform-only material is deleted rather than converted. Deletion candidates are things like technology selection (GDI versus Direct2D) and messages that exist only for other platforms' embedding arrangements; each deletion must state why the thing serves no platform this project has. Features this editor does not use yet — autocomplete, call tips, the lexer surface, printing via `FormatRange` — are retained and converted like everything else. Macro recording is the one open retention question: it reports operations as message numbers plus raw parameters, a format phase 5 removes, so retaining it means designing a message-free record; decide when its concern group is reached and record the decision here.

Follow [DISCOVERABILITY.md](DISCOVERABILITY.md) while splitting `Editor.cxx` into files named by what they do (wrapping, caret, scrolling, selection, find, styling, and so on). Keep neighboring code about the same concern, use the same plain feature nouns in filenames and named entry points, and move a complete concern rather than leaving small wrappers in the mixed-purpose file. Keep each change reviewable, add editor tests before moving behavior that is not already covered, migrate concise documentation for retained features, and delete documentation for deleted features in the same change.

Before applying the classification to the full inventory, create the fixed discoverability benchmark described in [DISCOVERABILITY.md](DISCOVERABILITY.md) and record the current-tree baseline. Complete two concern pilots. The first moves the complete wrapping concern, including its named entry points, private work, documentation, tests, and temporary forwarding paths, into a clearly named concern file. The second moves a concern with a different shape; autocomplete and call tips are the preferred candidate because they are owned by `ScintillaBase`, manage popup state, and must remain compiled despite having no initial application consumer. Run vector-only and hybrid grepai searches, whole-repository and source-limited searches, held-out paraphrases, boundary-stability checks, and cold navigation checks before and after each pilot. Apply the larger refactor only after both pilots meet the recorded acceptance criteria without search-engine wording or layout tied to one chunk boundary.

Deliverable: the two pilots and the full concern inventory meet the discoverability checks in [DISCOVERABILITY.md](DISCOVERABILITY.md); no retained behavior is reachable only through a message number; deleted behavior and its documentation are gone; `Editor.cxx` no longer exists as a 9,000-line file; focused tests cover each moved concern.

## Phase 5 — Delete the generated message layer

Remove the remains of `WndProc`, `DefWndProc`, numeric message constants, parameter coercion helpers, `ScintillaCall.h`, `ScintillaMessages.h`, the C message surface in `Scintilla.h`, and `Scintilla.iface`. Before deleting the generator, separate the enums and structures the core still needs from generated client API material: replace retained parts of `ScintillaTypes.h` and `ScintillaStructures.h` with small project-owned definitions, replace `Message` in key maps and helper signatures with the dedicated command type, and either delete macro-record notifications or give retained notification data direct fields. Remove unrelated public headers such as the GTK-only `ScintillaWidget.h` when nothing uses them.

Delete `ScintillaDoc.html` when every retained feature has a nearby doc comment and every deleted feature's prose is gone. Completion searches must find no message-number dispatch, `Message` command type, `SCI_*` command constants, generated-section markers, or client parameter-packing helpers. Keep the editor link test so a static archive cannot hide missing definitions.

Deliverable: the name-to-number-to-switch path and its generated client surface are gone; retained types have one project-owned definition; repository-wide completion searches and all tests pass.

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
- A message-free macro-recording format, if macro recording survives its phase 4 decision.
