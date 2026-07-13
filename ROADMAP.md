# Roadmap

Phases run roughly in order; each lists its deliverable and how it is verified. Update this file when a phase completes or the plan changes.

The phase 1 tests cover the platform-free document and container code well, but they do not instantiate `Editor` or `ScintillaBase`. Because `scintilla_core` is a static library, the test executable also does not link the editor objects it never calls. Phase 2 closes that gap before the dispatch refactor begins. The first visual executable arrives in phase 6; until then, the OnlyWayUi repository (tag `scintilla-seed`) can answer questions about the original core, but it is a reference rather than a regression test for the refactored code.

Once phase 2 adds continuous integration, every later phase must pass the normal build, the focused editor tests, and the sanitizer build. Behavior reductions such as deleting non-UTF-8 support or unused editor features are deliberate scope changes, not behavior-preserving refactors; document and test the retained behavior in the same change.

## Phase 1 — Stand up the harness ✅ (2026-07-12)

Create CMake for the Scintilla core and the upstream unit tests in `scintilla/test/unit`, and get them green under `ctest`. Document the build and test commands in AGENTS.md in the same change.

Deliverable: `cmake --preset dev && cmake --build build && ctest --test-dir build` works. This is the baseline safety net for the platform-free code; phase 2 extends it through the editor.

Done: the whole core compiles as static library `scintilla_core` (GCC 15, `-Wall -Wextra`, zero warnings); the upstream tests build against it and pass — 3,816 assertions in 52 test cases. `UnitTester.cxx` is excluded from the test build; it is an alternate `main()` for the Visual Studio build, matching the upstream GNU makefile.

## Phase 2 — Test the editor and establish the pattern

Add a concrete test editor and test-only platform implementation that link the whole editor path and expose invalidation, scrollbar, notification, clipboard, and drawing effects for assertions. Trace `SCI_SETWRAPMODE` end to end and dissolve it into a named public method because wrap mode is part of the application's intended surface. Move and condense its `ScintillaDoc.html` prose into a doc comment, leave the switch case as a temporary forwarding call, and test both the state change and its redraw, horizontal-scroll, and scrollbar effects. During the transition, compare the direct method with the forwarding path where that provides a useful mechanical check.

Use this exercise to define a classification applied before converting each later message: application-facing method, private editor operation, keyboard command, retained type or notification, or feature to delete. Do not turn every interface entry into a public method. Generate a concern-grouped inventory from `Scintilla.iface`, record baseline measurements for the case study, and add continuous integration for the normal build plus AddressSanitizer and UndefinedBehaviorSanitizer builds.

Deliverable: the full editor path links in a test executable; wrap mode and its side effects have focused tests; the five-way classification and documentation pattern are written down; continuous integration protects the build.

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

Apply the phase 2 classification across the `WndProc` switches in `Editor.cxx` and `ScintillaBase.cxx`, grouped by concern. Retained application operations become narrow named methods, internal behavior stays private, key bindings use a dedicated command type, and features outside this editor's scope are deleted rather than converted. Likely deletion candidates include printing, macro recording, technology selection, generic platform popups, and unused lexer or autocomplete surfaces; decide each group from actual application needs and dependencies instead of treating this list as automatic.

Split `Editor.cxx` into files named by what they do (wrap, caret, scrolling, selection, find, styling, and so on). Keep each change reviewable, add editor tests before moving behavior that is not already covered, migrate concise documentation for retained features, and delete documentation for deleted features in the same change. Update the running case-study notes and measurements after each concern group.

Deliverable: no retained behavior is reachable only through a message number; deleted behavior and its documentation are gone; `Editor.cxx` no longer exists as a 9,000-line file; focused tests cover each moved concern.

## Phase 5 — Delete the generated message layer

Remove the remains of `WndProc`, `DefWndProc`, numeric message constants, parameter coercion helpers, `ScintillaCall.h`, `ScintillaMessages.h`, the C message surface in `Scintilla.h`, and `Scintilla.iface`. Before deleting the generator, separate the enums and structures the core still needs from generated client API material: replace retained parts of `ScintillaTypes.h` and `ScintillaStructures.h` with small project-owned definitions, replace `Message` in key maps and helper signatures with the dedicated command type, and either delete macro-record notifications or give retained notification data direct fields. Remove unrelated public headers such as the GTK-only `ScintillaWidget.h` when nothing uses them.

Delete `ScintillaDoc.html` when every retained feature has a nearby doc comment and every deleted feature's prose is gone. Completion searches must find no message-number dispatch, `Message` command type, `SCI_*` command constants, generated-section markers, or client parameter-packing helpers. Keep the editor link test so a static archive cannot hide missing definitions.

Deliverable: the name-to-number-to-switch path and its generated client surface are gone; retained types have one project-owned definition; repository-wide completion searches and all tests pass.

## Phase 6 — Renderer and minimal Wayland vertical slice

Build the renderer together with the smallest shell needed to verify it: a Wayland connection, xdg-toplevel, EGL context and surface, event loop, resize and focus handling, basic keyboard and pointer input, timers, and a visible editor buffer. This is the first runnable program in the repository. Keep a deterministic offscreen or image-comparison path for renderer tests so drawing is checked without relying only on manual inspection, and use a checked-in test font with a compatible license so system font changes cannot move expected pixels or caret positions.

Collapse `Platform.h` into the one concrete implementation, mining `seed/editor/PlatOWUI.cxx` and `seed/backends/OnlyWayUi_Renderer_GL3.cpp` for working knowledge. Use FreeType for font metrics and glyph rasterization, HarfBuzz for shaping, and Fontconfig to find system fonts and fallbacks. Shape text once into a cached run containing glyphs, advances, offsets, input-byte clusters, and valid caret stops; measurement, wrapping, hit testing, selection, and drawing must all consume that same result. Start with correct left-to-right runs, disable discretionary ligatures while retaining shaping required by writing systems, then implement bidirectional screen-line layout as a separate step in this phase because HarfBuzz shapes directional runs but does not order a mixed-direction line.

Delete `ListBox`, `Menu`, and the other generic platform widgets Scintilla carries for popups; application chrome owns those jobs. Delete the absorbed `PlatOWUI` and renderer seed files while retaining the backend references still needed by phase 7, and preserve the OnlyWayUi license notice on derived code.

Deliverable: a minimal editor runs in a Wayland window and draws through one renderer with no product abstraction; measured byte positions, caret stops, selection bounds, and rendered glyph positions agree for ASCII, kerning pairs, combining sequences, required shaping, font fallback, and mixed-direction text; automated image checks cover clipping, text, caret, selection, and scrolling.

## Phase 7 — Complete the Wayland shell

Finish the window and input layer by mining the parts that actually exist in `seed/backends/`: compositor and seat lifecycle, xdg-toplevel and decoration handling, cursor themes, EGL damage, clipboard transfers, presentation feedback, and portal event-loop support. Pace drawing with `wl_surface.frame`; use presentation-time only to learn when a submitted frame was displayed.

Implement the features the seed does not contain from the relevant protocol and library sources: xkbcommon compose and key repeat, text-input-v3 for IME, primary selection, fractional scaling and viewporter support, output and buffer-scale changes, and their fallback behavior when an optional protocol is unavailable. Keep pointer, caret, font, cursor, damage, and buffer coordinates consistent across scale changes. Absorb the asynchronous D-Bus watches, timers, and xdg-foreign parent handle needed by the phase 8 file chooser without putting file-dialog policy in the shell.

Deliverable: keyboard compose and repeat, IME pre-edit and commit, pointer input, cursors, clipboard, primary selection, focus and seat changes, scaling, resize, frame callbacks, and presentation feedback work in the standalone editor; protocol-unavailable paths are tested; all absorbed backend seed files are deleted with required license notices retained.

## Phase 8 — Chrome and the editor application

Bespoke, compile-time-known chrome: scrollbar, find bar, and only the other controls that earn their place. Add asynchronous open and save dialogs through the desktop portal, Markdown-aware styling, and the small application-facing editor methods those features actually need.

Define "daily usable" with observable behavior: dirty-buffer prompts on close and replacement, atomic saves with explicit symlink and permission handling, external-file change detection and conflict handling, a tested invalid-UTF-8 file policy, preserved or deliberately normalized line endings, clear portal cancellation and failure behavior, and a documented response to files too large for comfortable editing. Test open, edit, save, save-as, failed save, external change, and close-with-unsaved-work flows.

Deliverable: daily-usable Markdown editing that satisfies the file-lifecycle checks above, with application workflow tests and no generic UI framework.

## Phase 9 — The case study

Turn the notes collected since phase 2 into the before-and-after study: trace wrap mode through both architectures, compare the number of files, search steps, dispatch cases, generated definitions, lines of code, and tests involved, and state which features were deleted rather than translated. Explain what a model could and could not determine in each version, the regressions the new tests caught, the cost of the refactor, and which parts of the method did not work as expected.

Deliverable: an evidence-backed document that makes the method transferable to other codebases instead of relying on a retrospective written from memory.
