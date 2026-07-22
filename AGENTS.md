# AGENTS

## What this project is

scalpel-editor is a Wayland-only text editor built from a refactored Scintilla core that transformed the message system and 9000 line switch statement into methods grouped in files by concern.

## Repository layout

- `scintilla/` — the Scintilla 5.6.4 core, imported verbatim. `scintilla/UPSTREAM.md` records the release identity and the byte-for-byte verification of the import. This code is now this project's to change; the verbatim import commit is the baseline, and git history from that commit is the record of divergence. Do not update `UPSTREAM.md` as the code diverges — it describes the import, not the current state.
- `app/` — the production editor host, application platform definitions, standalone `scalpel-editor` executable, and focused `applicationTest` coverage.
- `seed/` — working reference code copied from OnlyWayUi (see `ORIGINS.md`). It shows a Scintilla-on-Wayland editor built through RmlUi's abstractions. Mine it, do not build on it: absorb what a piece teaches into direct code, then delete the piece. It does not compile in this repository and that is expected.
- `ROADMAP.md` — the phase plan. Update it when a phase completes or the plan changes.

## Build and test

The build uses CMake with the Ninja generator. Always configure from the repository root, never from inside `scintilla/`: the root `CMakeLists.txt` is the only top-level project, and it pulls `scintilla/` in as a subdirectory. Running CMake inside `scintilla/` would spawn a duplicate build tree that the presets and `.gitignore` do not manage, so `scintilla/CMakeLists.txt` rejects that with a fatal error. From the repository root, configure the normal development tree once:

```
cmake --preset dev          # configure into build/ (Debug)
```

Use the smallest build and test scope that covers the code being changed. Ninja rebuilds the target's dependencies, and the Catch2 executables accept a test-name pattern:

```
cmake --build build --target editorTest
./build/scintilla/test/editor/editorTest "Wrap mode*"

cmake --build build --target unitTest
./build/scintilla/test/unit/unitTest "Document*"

cmake --build build --target applicationTest
./build/app/test/applicationTest "production editor host*"

cmake --build build --target scalpel-editor
```

Keep iteration checks narrow even when the final workflow will be broad. Build only the target under development and run only its focused tests; do not also build unrelated test targets in anticipation of the final workflow. A CMake dependency or compile-flag change may make Ninja rebuild much of the tree, so let the required final workflow pay that cost once unless a broader intermediate build is needed to diagnose the change.

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

For failure details, run a test binary directly: `./build/scintilla/test/unit/unitTest` for the upstream platform-free tests, `./build/scintilla/test/editor/editorTest` for the concrete editor tests, or `./build/app/test/applicationTest` for the production host and its real application `Window` and `Platform` objects (Catch2 v2; pass a test name pattern to run one case). The core builds as a static library, `scintilla_core`. The unit executable links only the platform-free objects it calls. `editorTest` links the editor concern translation units (`Editor*.cxx`, `ScintillaBase.cxx`, and their required core objects) against the deterministic test-only `Platform.h` implementation, so missing editor definitions fail the build. Named feature work lives in concern files such as `EditorWrapping.cxx` and `EditorDocument.cxx`; `Editor.cxx` keeps shared paint, geometry, notifications, and document-watcher work. The production application host lives in `app/` and is being completed during roadmap phase 6.

## Development

- If you spot a flaw or deficiency in the existing framework while implementing something, explicitly communicate its existence, rather than just working around it.
- Do not write code that calls an external API without reading the relevant source or local headers first. This includes Wayland, EGL, xkbcommon, FreeType, and the Scintilla core itself. If the needed source is not available, stop and ask for it. Do not guess signatures, types, ownership rules, or lifecycle rules.
- Prefer deleting indirection over adding abstraction. When tempted to make something more generally useful, stop and make it do its one job better instead.

## Documentation guidance

Do not leave documentation describing a state the code no longer has. When a change removes or alters something the docs describe, update or delete that description in the same commit.

In Markdown files, do not hard-wrap prose. Write each paragraph and each list item as a single line and let the editor soft-wrap it.

### C++ code discovery

See /.agents/skills/scalps-code-search/SKILL.md

## Creating plans

A roadmap phase should be broken down into sessions a coding agent will handle.

At the beginning of a session, a plan should be created as a sequence of commits. Those commits should be made at the appropriate points during implementation.

## Commits

The extended message should contain a concise description of what changed and why.

Commit messages should be hard-wrapped.
