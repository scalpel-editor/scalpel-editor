# Testing scalpel-editor

## Default verification

Use only the smallest build and test scope that covers the changed code. Build one target and run one Catch2 name pattern matching the concern under edit. Do not build unrelated test targets, run a whole test binary when a focused pattern exists, reconfigure the tree, rebuild sanitizer trees, or run the full matrix as routine session work.

Typical focused commands are:

```sh
cmake --build build --target editorTest
./build/scintilla/test/editor/editorTest "Wrap mode*"

cmake --build build --target unitTest
./build/scintilla/test/unit/unitTest "Document*"

cmake --build build --target applicationHostTest
./build/app/test/applicationHostTest "production editor host*"

cmake --build build --target waylandFrameTest
./build/app/test/waylandFrameTest "Wayland frame*"

cmake --build build --target lexillaFactoryTest
./build/lexilla-compat/lexillaFactoryTest

cmake --build build --target scalpel-editor
```

Ninja rebuilds the requested target's dependencies. A CMake dependency or compile-flag change may therefore rebuild more of the tree, but still request only the needed target and let Ninja decide what must be rebuilt.

Documentation-only changes do not require a build unless they alter build or test instructions.

## Wider checks

Widen verification only for a concrete reason:

- Run the whole suite of one relevant test binary when a change crosses several cases in that binary or no focused pattern is broad enough.
- Run `cmake --workflow --preset dev` once when handing off compiled code that changes shared infrastructure, CMake or compiler settings, multiple concerns, or the link surface of more than one test binary.
- On NixOS, run `cmake --workflow --preset release-test-nixos` when a change concerns optimization-sensitive behavior or as part of the release gate. The `release-nixos` workflow builds only the optimized application.
- Run one sanitizer tree with `cmake --workflow --preset asan` or `cmake --workflow --preset ubsan` only when the change concerns memory lifetime, undefined behavior, or sanitizer/compiler settings and one tree covers the risk.

Do not widen checks in anticipation of a later gate. Do not stack focused, full-suite, and workflow checks for the same edit when the smaller check already covers it.

## Full matrix

The full matrix configures, builds, and tests the normal, AddressSanitizer, and UndefinedBehaviorSanitizer trees. Run it only at roadmap phase gates, before a release, when explicitly requested, or when a change clearly needs all three trees, such as sanitizer or compiler settings that must pass in every configuration. The commands and OS-specific tree names are documented in [BUILDING.md](BUILDING.md).

There is no hosted CI. The matrix is a deliberate local gate, not the daily development loop. Do not run it merely because a session is ending or a handoff is large.

For a NixOS release gate, also run `nix flake check --print-build-logs`. Its Release check builds and runs the complete suite in a separate Release derivation and then validates the installed artifact; the default package does not compile tests.

## Test targets

For failure details, run the relevant test binary directly with a Catch2 v2 name pattern:

- `build/scintilla/test/unit/unitTest` contains upstream platform-free tests.
- `build/scintilla/test/editor/editorTest` contains concrete editor tests, including focused cases that attach the in-tree Lexilla Markdown lexer through `SetILexer`.
- `build/lexilla-compat/lexillaFactoryTest` creates the in-tree Markdown lexer through `CreateLexer` and releases it.
- Concern-named executables under `build/app/test/` cover application and Wayland behavior. Application targets separate host, IME, transfer, and direct-input behavior. Wayland targets separate cursor, scale, frame, window, registry, text-input, keyboard, pointer, byte-transfer, clipboard, primary-selection, poll, D-Bus, portal, and cross-concern integration behavior.
- `fontTest` and `rendererTest` each retain one component executable, but their sources are split by concern so editing one case does not rebuild the entire suite translation unit.

The core builds as the static library `scintilla_core`. `unitTest` links only the platform-free objects it calls. `editorTest` links the editor concern translation units, including `Editor*.cxx` and `ScintillaBase.cxx`, and their required core objects against the deterministic test-only `Platform.h` implementation, so missing editor definitions fail the build. Named feature work lives in concern files such as `EditorWrapping.cxx` and `EditorDocument.cxx`; `Editor.cxx` keeps shared paint, geometry, notifications, and document-watcher work. The production application host, Wayland shell, and standalone executable live in `app/`.

## Header changes

Production headers under `app/`, `scintilla/src/`, and `scintilla/include/` must compile alone. When changing production headers or their includes, check only the affected paths by passing them explicitly or by running `tools/check-self-contained-headers.sh --changed`, which checks dirty production headers against `HEAD`.

Do not run the no-argument full header scan as routine session work. It is intended for occasional audits and phase gates and uses the configured development tree's compile database to select flags for the target that owns each header.
