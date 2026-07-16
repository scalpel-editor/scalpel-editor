# AGENTS

This document is the working guide for coding agents and contributors in this repository.

## What this project is

scalpel-editor is a Wayland-only text editor built from a refactored Scintilla core. It is not a general UI toolkit, not cross-platform, and not designed for embedding. It does one thing: edit text in one window on Wayland.

The editor itself is personal utility — SciTE already gives Wayland users the full Scintilla feature set through GTK, so a lightweight alternative is not the value here. The value beyond personal utility lies in two artifacts. First, a Scintilla core refactored until both humans and AI models can read it: every feature findable by name, documentation next to implementation, no layer that exists only to serve platforms or uses this project does not have. Second, a worked example of a C++ Wayland application built without a GUI toolkit. Judge changes against these.

## Direction principles

- One platform (Wayland), one encoding (UTF-8), one renderer. Delete layers and generality that serve absent platforms; keep Scintilla's editing features intact even when this editor does not use them yet, so the refactored core stays useful to others (see the roadmap scope principle).
- A feature's name, documentation, and implementation belong in one greppable place. No numeric message dispatch, no indirection that severs the link between a name and its code.
- Prefer deleting indirection over adding abstraction. When tempted to make something more generally useful, stop and make it do its one job better instead.
- Keep the tree small enough to hold in one head.
- Refactor in behavior-preserving steps with tests green, and make each step small enough to review as a diff. When deliberately reducing scope, state the removed behavior and test what remains.

## Repository layout

- `scintilla/` — the Scintilla 5.6.4 core, imported verbatim. `scintilla/UPSTREAM.md` records the release identity and the byte-for-byte verification of the import. This code is now this project's to change; the verbatim import commit is the baseline, and git history from that commit is the record of divergence. Do not update `UPSTREAM.md` as the code diverges — it describes the import, not the current state.
- `seed/` — working reference code copied from OnlyWayUi (see `ORIGINS.md`). It shows a Scintilla-on-Wayland editor built through RmlUi's abstractions. Mine it, do not build on it: absorb what a piece teaches into direct code, then delete the piece. It does not compile in this repository and that is expected.
- `ROADMAP.md` — the phase plan. Update it when a phase completes or the plan changes.

## Change rules for scintilla/

- Keep `scintilla/test/unit` green through every refactoring step. These upstream tests cover the platform-free document and container code; they are not evidence that `Editor` behavior is unchanged.
- Before dissolving a message, classify it as an application-facing method, private editor operation, keyboard command, retained type or notification, or feature to delete. Deletion is reserved for platform-only material and the message layer itself. Do not automatically turn messages into public methods.
- When a retained message becomes a named method, find its entry in `scintilla/ScintillaDoc.html`, condense the useful prose into a doc comment near the method, and remove the old prose. When a feature is deleted, delete its documentation in the same change. The goal state is that `ScintillaDoc.html` has no live content and is deleted.
- Add a focused editor test before moving behavior that the current tests do not exercise. Prefer assertions on visible effects such as document state, selection, invalidation, notifications, and scrollbar changes over relying on a mechanical-looking diff.
- The "concrete test editor" is the test-only `ScintillaBase` subclass and deterministic host described in roadmap phase 2. Keep its callbacks observable, keep its clock and external state under test control, and do not make it depend on the seed code or the real Wayland and rendering stack.

## Build and test

The build uses CMake with the Ninja generator. Always configure from the repository root, never from inside `scintilla/`: the root `CMakeLists.txt` is the only top-level project, and it pulls `scintilla/` in as a subdirectory. Running CMake inside `scintilla/` would spawn a duplicate build tree that the presets and `.gitignore` do not manage, so `scintilla/CMakeLists.txt` rejects that with a fatal error. From the repository root, configure the normal development tree once:

```
cmake --preset dev          # configure into build/ (Debug)
```

Use the smallest build and test scope that covers the code being changed. Ninja rebuilds the target's dependencies, and both Catch2 executables accept a test-name pattern:

```
cmake --build build --target editorTest
./build/scintilla/test/editor/editorTest "Wrap mode*"

cmake --build build --target unitTest
./build/scintilla/test/unit/unitTest "Document*"
```

Documentation-only changes do not require a build unless they alter build or test instructions. Before handing off a compiled-code change, run the normal configure/build/test workflow once:

```
cmake --workflow --preset dev
```

The full check matrix runs the same sequence for three trees — normal (`dev`, in `build/`), AddressSanitizer (`asan`, in `build-asan/`), and UndefinedBehaviorSanitizer (`ubsan`, in `build-ubsan/`):

```
./check.sh                    # all three trees
cmake --workflow --preset asan   # one tree
```

Run `./check.sh` at roadmap phase gates, before a release, when requested, and when a change affects memory lifetime, sanitizer or compiler settings, the test host, or broad shared-core behavior. Do not run it merely because a development session is ending. There is no hosted CI, so the matrix remains the final local gate.

The ASan test preset sets `ASAN_OPTIONS=detect_leaks=0` because this development runner uses `ptrace`, under which LeakSanitizer aborts before reporting results. AddressSanitizer's other checks remain enabled. The matrix therefore does not check for leaks; use a non-traced process for a separate leak check.

For failure details, run a test binary directly: `./build/scintilla/test/unit/unitTest` for the upstream platform-free tests or `./build/scintilla/test/editor/editorTest` for the concrete editor tests (Catch2 v2; pass a test name pattern to run one case). The core builds as a static library, `scintilla_core`. The unit executable links only the platform-free objects it calls. `editorTest` links `Editor.cxx`, `ScintillaBase.cxx`, and their required core objects against the deterministic test-only `Platform.h` implementation, so missing editor definitions fail the build. The first application with a real platform implementation arrives in roadmap phase 6.

## Source reading rule

Do not write code that calls an external API without reading the relevant source or local headers first. This includes Wayland, EGL, xkbcommon, FreeType, and the Scintilla core itself. If the needed source is not available, stop and ask for it. Do not guess signatures, types, ownership rules, or lifecycle rules.

## Style

Use simple, direct names and explanations. Favor code that shows lifetime, ownership, and control flow clearly. Avoid vague framework jargon in code, comments, commit messages, and docs — describe the concrete behavior.

New code follows the naming and layout of the file it lives in. The Scintilla core keeps its existing conventions; do not restyle code while refactoring it.

## Documentation guidance

Do not leave documentation describing a state the code no longer has. When a change removes or alters something the docs describe, update or delete that description in the same commit.

In Markdown files, do not hard-wrap prose. Write each paragraph and each list item as a single line and let the editor soft-wrap it.

## Tooling notes

- grepai (semantic search): make sure the index includes `.cxx` and `.iface` files — a missing extension silently returns no results for most of the Scintilla core.
- Use `grepai status --wait` instead of sleeps or watcher-log scraping. After changing a file that must be present in the durable index, run `grepai status --wait --steady --indexed path/to/file.cxx --after "$(stat -c %Y path/to/file.cxx)" --timeout 2m`; repeat for each changed file that matters to the next search or snapshot. `--indexed` polls the persisted store, while `--steady` also requires an idle watcher. Indexed mtimes have one-second resolution, so make sure the final edit has a different Unix-second mtime from the previously indexed version; otherwise an older version can satisfy `--after`. Plain `grepai status --no-ui` and `grepai status --json` report watcher state, pending work, durable save time and generation, effective configuration and match checks, project and index paths, and the last failure. Sandboxed status can read the host watcher's recorded state.
- Do not add a `.*/` pattern to `.gitignore`. grepai's ignore matcher mis-reads it as "every directory" and indexes nothing. The checked-in `.gitignore` avoids it deliberately; keep it that way or list hidden directories explicitly.
