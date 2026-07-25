# Building scalpel-editor

The checkout is shared between openSUSE and NixOS. Each operating system must use its own CMake build trees because CMake caches absolute compiler, header, library, and pkg-config paths.

## NixOS

Enter the pinned development environment and configure the NixOS development tree:

```sh
nix develop
cmake --preset dev-nixos
cmake --build --preset dev-nixos --target scalpel-editor
```

The Nix shell supplies CMake, GCC, Clang, Python, Ninja, pkg-config, wayland-scanner, and all library development outputs required by the project and its verification scripts. It does not require project-specific additions to `/etc/nixos/configuration.nix`.

The sanitizer presets use `build-asan-nixos/` and `build-ubsan-nixos/`. Run the full NixOS matrix only at a phase or release gate:

```sh
./check.sh nixos
```

`.clangd` continues to use the openSUSE `build/` tree by default. On NixOS, configure the editor's clangd invocation with `--compile-commands-dir=build-nixos`.

## openSUSE

The existing presets remain the openSUSE entry points:

```sh
cmake --preset dev
cmake --build build --target scalpel-editor
```

The openSUSE development, AddressSanitizer, and UndefinedBehaviorSanitizer trees are `build/`, `build-asan/`, and `build-ubsan/`.
