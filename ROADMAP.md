# Roadmap

Phases run roughly in order; each lists its deliverable and how it is verified. Update this file when a phase completes or the plan changes.

A note on verification order: the editor will not be runnable in this repository until phase 6. Until then, the upstream unit tests are the only executable check, which is why they come first and why phases 2–5 are behavior-preserving refactors. The OnlyWayUi repository (tag `scintilla-seed`) keeps a runnable editor on the same unmodified 5.6.4 core and can serve as a visual cross-check if a question about original behavior comes up.

## Phase 1 — Stand up the harness ✅ (2026-07-12)

Create CMake for the Scintilla core and the upstream unit tests in `scintilla/test/unit`, and get them green under `ctest`. Document the build and test commands in AGENTS.md in the same change.

Deliverable: `cmake --preset dev && cmake --build build && ctest --test-dir build` works. This is the safety net every later phase relies on.

Done: the whole core compiles as static library `scintilla_core` (GCC 15, `-Wall -Wextra`, zero warnings); the upstream tests build against it and pass — 3,816 assertions in 52 test cases. `UnitTester.cxx` is excluded from the test build; it is an alternate `main()` for the Visual Studio build, matching the upstream GNU makefile.

## Phase 2 — Establish the dissolution pattern

Trace one message end to end — `SCI_SETWRAPMODE` — and dissolve it: a named public method on the editor, its `ScintillaDoc.html` prose rewritten as a doc comment, the switch case reduced to a forwarding call (deleted entirely once phase 4 removes the dispatch). Record what the exercise teaches as a short pattern description in this file, so the grind phase is mechanical.

Deliverable: one message fully dissolved, tests green, pattern written down.

## Phase 3 — Dissolve the dispatch

Apply the pattern across the `WndProc` switch in `Editor.cxx` and `ScintillaBase.cxx`, grouped by concern, splitting `Editor.cxx` into files named by what they do (wrap, caret, scrolling, selection, find, styling, ...). Work in small reviewable commits, tests green throughout. Doc comments migrate from `ScintillaDoc.html` as each group moves.

Deliverable: no behavior reachable only through a message number; `Editor.cxx` no longer exists as a 9,000-line file.

## Phase 4 — Delete the message layer

Remove `Scintilla.h` message constants, `ScintillaMessages.h`, `ScintillaCall.h`, the `.iface` file, and the remains of `WndProc` dispatch once nothing references them. Delete `ScintillaDoc.html` when its last live entry has migrated.

Deliverable: the name → number → switch indirection is gone from the tree.

## Phase 5 — UTF-8 only

Remove DBCS and code-page support: `DBCS.cxx`, code-page conditionals in `Document` and `EditView`, and the code-page parameters threaded through call chains. UTF-8 becomes an assumption, not a mode.

Deliverable: no code path that asks which encoding is in use; tests (adjusted where they exercised other encodings) green.

## Phase 6 — One renderer, one platform layer

Collapse the `Platform.h` abstraction into one concrete implementation, mining `seed/editor/PlatOWUI.cxx` and `seed/backends/OnlyWayUi_Renderer_GL3.cpp` for the working knowledge. Decide here how text is measured and drawn (FreeType directly, with or without HarfBuzz shaping). Delete `ListBox`, `Menu`, and the other platform widgets Scintilla's core drags along for its popups — chrome is the shell's job.

Deliverable: the core draws through one renderer with no indirection; absorbed seed files deleted.

## Phase 7 — Wayland shell

The window and input layer, mining `seed/backends/`: compositor connection, xdg-toplevel with decoration handling, frame pacing via presentation-time, xkbcommon keyboard input including compose, text-input-v3 for IME, pointer input with cursor themes, clipboard plus primary selection.

Deliverable: the refactored core running in its own Wayland window — the first runnable editor in this repository. Absorbed seed files deleted.

## Phase 8 — Chrome and the editor application

Bespoke, compile-time-known chrome: scrollbar, find bar, whatever else earns its place. File open/save through the desktop portal. Markdown-aware styling. This is where "editor I actually use" gets defined, deliberately late — the refactor phases should not anticipate features.

Deliverable: daily-usable markdown editing.

## Phase 9 — The case study

Write up the before/after: the same feature (wrap mode) traced through the original message architecture and the refactored one, what a model could and could not do with each, and what the refactor cost. This is the second half of the project's purpose, not an afterthought.

Deliverable: a document that makes the method transferable to other codebases.
