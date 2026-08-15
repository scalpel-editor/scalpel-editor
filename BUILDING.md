# Building scalpel-editor

The root `CMakeLists.txt` is the project's only top-level CMake project and includes `scintilla/` as a subdirectory. Always configure from the repository root. Configuring from inside `scintilla/` would create an unmanaged duplicate build tree and is rejected by `scintilla/CMakeLists.txt`.

The checkout is shared between openSUSE and NixOS. Each operating system must use its own CMake build trees because CMake caches absolute compiler, header, library, and pkg-config paths. Do not mix artifacts between the two environments, create temporary project build trees, use `--clean-first`, or manually delete build products. Ninja rebuilds the dependencies required by the requested target.

## openSUSE

Configure the normal development tree once, then build requested targets from it:

```sh
cmake --preset dev
cmake --build build --target scalpel-editor
```

The openSUSE development, AddressSanitizer, and UndefinedBehaviorSanitizer trees are `build/`, `build-asan/`, and `build-ubsan/`.

Run the full openSUSE matrix only when required by [TESTING.md](TESTING.md):

```sh
./check.sh
```

## NixOS

Enter the pinned development environment and configure the separate NixOS development tree:

```sh
nix develop
cmake --preset dev-nixos
cmake --build --preset dev-nixos --target scalpel-editor
```

The Nix shell supplies CMake, GCC, Clang, Python, Ninja, pkg-config, wayland-scanner, and all library development outputs required by the project and its verification scripts. The NixOS CTest presets select the pinned Mesa EGL vendor and drivers, and the headless context selects Mesa's software EGL device directly. Renderer-backed tests therefore use llvmpipe without probing host GPUs; normal application runs retain the host's EGL vendor and hardware selection. The shell does not require project-specific additions to `/etc/nixos/configuration.nix`.

The NixOS development, optimized Release, optimized Release test, AddressSanitizer, and UndefinedBehaviorSanitizer trees are `build-nixos/`, `build-release-nixos/`, `build-release-test-nixos/`, `build-asan-nixos/`, and `build-ubsan-nixos/`. The corresponding presets are `dev-nixos`, `release-nixos`, `release-test-nixos`, `asan-nixos`, and `ubsan-nixos`.

Build the optimized application without compiling tests with:

```sh
cmake --workflow --preset release-nixos
```

Use the separate Release test preset when investigating optimization-sensitive behavior or running the optimized release gate:

```sh
cmake --workflow --preset release-test-nixos
```

The default Nix package disables test targets so `nix build` and `nix run` only build the optimized production artifact and perform installed-artifact checks. `nix flake check` uses a separate Release derivation that builds and runs the complete test suite before performing the same installed-artifact checks.

Run the full NixOS matrix only when required by [TESTING.md](TESTING.md):

```sh
./check.sh nixos
```

`.clangd` uses the openSUSE `build/` tree by default. On NixOS, configure the editor's clangd invocation with `--compile-commands-dir=build-nixos`.

## Sanitizer runner limitation

The ASan test preset sets `ASAN_OPTIONS=detect_leaks=0` because this development runner uses `ptrace`, under which LeakSanitizer aborts before reporting results. AddressSanitizer's other checks remain enabled. The matrix does not check for leaks; use a non-traced process for a separate leak check.
