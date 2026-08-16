# Installing on NixOS

The flake provides an x86-64 Linux package containing `scalpel-editor`, its desktop entry, icon, and the Blue Oak, Scintilla, and Lexilla license files. Other architectures, including AArch64, are not currently supported by this package. The packaged executable links Lexilla statically and does not depend on a Lexilla shared library.

The package uses the host's EGL vendor selection during normal application runs. It does not force the Mesa test configuration used by the development test presets.

## Build from a source checkout

From the repository root, build the default Release package with:

```sh
nix build .
```

The installed package is available through the `result` link created in the checkout:

```sh
./result/bin/scalpel-editor
```

No GitHub release is involved. Nix builds the files in the checkout using the exact Nixpkgs revision recorded in `flake.lock`.

## Run without installing

Build and run directly from the source checkout with:

```sh
nix run .
nix run . -- notes.txt
```

## Install into a user profile

Build the source checkout and install it for the current user with:

```sh
nix profile install .
```

This adds the executable to the profile and makes the packaged desktop entry and icon available through the profile's shared-data directories.

## Add to a NixOS configuration

Add the source checkout as a path input to the system flake and include its default package in `environment.systemPackages`:

```nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    scalpel-editor.url = "path:/absolute/path/to/scalpel-editor";
  };

  outputs = { nixpkgs, scalpel-editor, ... }: {
    nixosConfigurations.hostname = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        {
          environment.systemPackages = [
            scalpel-editor.packages.x86_64-linux.default
          ];
        }
      ];
    };
  };
}
```

Replace the example path and `hostname` with the checkout path and configuration name used by the system. When the checkout changes, update the system flake's lock entry with `nix flake update scalpel-editor` before rebuilding the system. The desktop environment must provide a working Wayland session and desktop portal; scalpel-editor uses the portal for Open and Save dialogs.

## Use a published release instead

Once a release tag exists, replace `.` in the run or profile commands with `github:scalpel-editor/scalpel-editor/2.0.0`, or use that URL instead of the `path:` URL in a system flake. A published release is optional; it provides a stable remote source when a local checkout is not desired.

## Verify the package

From a checkout, evaluate and build all flake checks with:

```sh
nix flake check
```

The Release check builds and runs the complete optimized test suite, then validates the executable's reported version, desktop entry, installed icon, installed license files, and the absence of a Lexilla shared-library dependency. This check is separate from the default package, so ordinary `nix build` and `nix run` do not compile tests. The broader Debug, AddressSanitizer, and UndefinedBehaviorSanitizer development matrix remains `nix develop --command ./check.sh nixos`.
