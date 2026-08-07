# AGENTS

## What this project is

scalpel-editor is a Wayland-only plain-text editor built from a substantially refactored Scintilla 5.6.4 core. Application features use named typed operations, and the platform layer is implemented directly with Wayland, EGL, OpenGL, FreeType, HarfBuzz, and Fontconfig.

## Repository layout

- `scintilla/` — the project-owned Scintilla-based editor core, renderer, platform definitions, and core tests.
- `app/` — the production editor host, application platform definitions, standalone `scalpel-editor` executable, and focused application test targets.
- `docs/` — current product, architecture, and design documentation.
- `.plans/` — ignored cross-session development plans plus a tracked README and template.

## Build and test

The build uses CMake with the Ninja generator. Always configure from the repository root, never from inside `scintilla/`: the root `CMakeLists.txt` is the only top-level project, and it pulls `scintilla/` in as a subdirectory. Running CMake inside `scintilla/` would spawn a duplicate build tree that the presets and `.gitignore` do not manage, so `scintilla/CMakeLists.txt` rejects that with a fatal error. From the repository root, configure the normal development tree once:

```
cmake --preset dev          # configure into build/ (Debug)
```

The checkout is shared with NixOS, whose compiler and dependency paths must not be mixed into the openSUSE build trees above. On NixOS, enter `nix develop` and use the parallel `dev-nixos`, `asan-nixos`, and `ubsan-nixos` presets, which configure into `build-nixos/`, `build-asan-nixos/`, and `build-ubsan-nixos/`. See `BUILDING.md`.

### Default verification (almost every change)

Use only the smallest build and test scope that covers the code being changed. Build one target. Run one Catch2 name pattern that matches the concern under edit. Do not build other test targets "just in case." Do not run an entire test binary without a pattern when a pattern exists. Do not reconfigure, rebuild sanitizer trees, or run the full matrix as routine session work.

```
cmake --build build --target editorTest
./build/scintilla/test/editor/editorTest "Wrap mode*"

cmake --build build --target unitTest
./build/scintilla/test/unit/unitTest "Document*"

cmake --build build --target applicationHostTest
./build/app/test/applicationHostTest "production editor host*"

cmake --build build --target waylandFrameTest
./build/app/test/waylandFrameTest "Wayland frame*"

cmake --build build --target scalpel-editor
```

Do not use `--clean-first` or manually delete build products.

Do not use tmp folders to rebuild the project.

Ninja rebuilds that target's dependencies. A CMake dependency or compile-flag change may rebuild more of the tree; still request only the needed target and let Ninja decide.

Documentation-only changes do not require a build unless they alter build or test instructions.

### Wider checks (only when the default is not enough)

Widen only for a concrete reason:

- The whole suite of the one relevant test binary (no Catch pattern), when the change crosses several cases in that binary or a focused pattern is too narrow to trust.
- `cmake --workflow --preset dev` once, when handing off compiled code that touches shared infrastructure, CMake or compiler settings, multiple concerns, or the link surface of more than one test binary. Ordinary single-concern work does not need this workflow; focused build and pattern are enough for the commit and for ending a session.
- One sanitizer tree (`cmake --workflow --preset asan` or `ubsan`) only when the change is about memory lifetime, undefined behavior, or sanitizer/compiler settings and a single tree is enough to check it.

Do not run wider checks in anticipation of a later gate. Do not stack default, full-suite, and workflow checks for the same edit when the smaller one already covers it.

### Full matrix (rare)

The full check matrix configures, builds, and tests three trees — normal (`dev`, in `build/`), AddressSanitizer (`asan`, in `build-asan/`), and UndefinedBehaviorSanitizer (`ubsan`, in `build-ubsan/`):

```
./check.sh                    # all three trees
```

Run `./check.sh` only at roadmap phase gates, before a release, when the user explicitly asks, or when a change clearly needs every tree (for example sanitizer or compiler settings that must pass in all three). Do not run it because a session is ending, because a handoff feels large, or as a habit after ordinary feature work. There is no hosted CI; the matrix is the deliberate local gate, not the daily loop.

The ASan test preset sets `ASAN_OPTIONS=detect_leaks=0` because this development runner uses `ptrace`, under which LeakSanitizer aborts before reporting results. AddressSanitizer's other checks remain enabled. The matrix therefore does not check for leaks; use a non-traced process for a separate leak check.

### Where tests live

For failure details, run a test binary directly: `./build/scintilla/test/unit/unitTest` for the upstream platform-free tests, `./build/scintilla/test/editor/editorTest` for the concrete editor tests, or the concern-named target under `./build/app/test/` for application and Wayland behavior (Catch2 v2; pass a test name pattern to run one case). Application targets separate host, IME, transfer, and direct-input behavior. Wayland targets separate cursor, scale, frame, window, registry, text-input, keyboard, pointer, byte-transfer, clipboard, primary-selection, poll, D-Bus, portal, and cross-concern integration behavior. `fontTest` and `rendererTest` retain one component executable each, but their sources are split by concern so editing one case does not rebuild the entire suite translation unit. The core builds as a static library, `scintilla_core`. The unit executable links only the platform-free objects it calls. `editorTest` links the editor concern translation units (`Editor*.cxx`, `ScintillaBase.cxx`, and their required core objects) against the deterministic test-only `Platform.h` implementation, so missing editor definitions fail the build. Named feature work lives in concern files such as `EditorWrapping.cxx` and `EditorDocument.cxx`; `Editor.cxx` keeps shared paint, geometry, notifications, and document-watcher work. The production application host, Wayland shell, and standalone executable live in `app/`.

## Development

- If you spot a flaw or deficiency in the existing framework while implementing something, explicitly communicate its existence, rather than just working around it.
- Do not write code that calls an external API without reading the relevant source or local headers first. This includes Wayland, EGL, xkbcommon, FreeType, and the Scintilla core itself. If the needed source is not available, stop and ask for it. Do not guess signatures, types, ownership rules, or lifecycle rules.
- Prefer deleting indirection over adding abstraction. When tempted to make something more generally useful, stop and make it do its one job better instead.

### Includes

Production headers under `app/`, `scintilla/src/`, and `scintilla/include/` must compile alone. Do not run the full header scan as routine session work. When a session changes production headers or their includes, check only those paths — either pass them explicitly or use `tools/check-self-contained-headers.sh --changed` (dirty production headers vs `HEAD`). The no-argument full scan is for occasional audits and phase gates; it uses the configured development tree's compile database to select flags for the target that owns each header.

## Documentation guidance

Do not leave documentation describing a state the code no longer has. When a change removes or alters something the docs describe, update or delete that description in the same commit.

Documentation describes the current system, its constraints, and its design choices. Do not retain phase logs, completed implementation plans, source-history narratives, or verification snapshots as public documentation. Promote lasting information from a working plan into current documentation, then delete the completed plan.

In Markdown files, do not hard-wrap prose. Write each paragraph and each list item as a single line and let the editor soft-wrap it.

### C++ code discovery

See /.agents/skills/scalps-code-search/SKILL.md

## Creating plans

At the beginning of a substantial session, create an ignored `.plans/<topic>.md` working plan as a sequence of commits. Make those commits at the appropriate points during implementation and update the plan when discoveries change the approach.

Plans are local working state, not authoritative design documentation. At completion, promote lasting facts into the current documentation, tests, comments, or this file, then delete the completed plan. See `.plans/README.md`.

## Commits

Run `git add`, validation commands, and `git commit` as separate shell calls. Do not combine them with `&&`. Do not use `$'...'` shell quoting for commit messages. Invoke `git` directly so existing `["git", "add"]` and `["git", "commit"]` command rules apply.

The extended message should contain a concise description of what changed and why.

Hard-wrap commit messages at 68 characters. The commit hook enforces an absolute maximum of 72 characters.
