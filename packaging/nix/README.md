# Installing on NixOS

The flake provides an x86-64 Linux package containing `scalpel-editor`, its desktop entry, icon, and license files. Other architectures, including AArch64, are not currently supported by this package.

The package uses the host's EGL vendor selection during normal application runs. It does not force the Mesa test configuration used by the development test presets.

## Run without installing

From a local checkout, build and run the default app with:

```sh
nix run .
```

After the `1.0.0` tag is published, run that release directly from GitHub with:

```sh
nix run github:scalpel-editor/scalpel-editor/1.0.0
```

Place arguments for scalpel-editor after `--`, for example:

```sh
nix run github:scalpel-editor/scalpel-editor/1.0.0 -- notes.txt
```

## Install into a user profile

Install the tagged release for the current user with:

```sh
nix profile install github:scalpel-editor/scalpel-editor/1.0.0
```

This adds the executable to the profile and makes the packaged desktop entry and icon available through the profile's shared-data directories.

## Add to a NixOS configuration

Add scalpel-editor as an input to the system flake and include its default package in `environment.systemPackages`:

```nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    scalpel-editor.url = "github:scalpel-editor/scalpel-editor/1.0.0";
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

Replace `hostname` with the configuration name used by the system. The desktop environment must provide a working Wayland session and desktop portal; scalpel-editor uses the portal for Open and Save dialogs.

## Verify the package

From a checkout, evaluate and build all flake checks with:

```sh
nix flake check
```

The package check builds the Release executable and validates its reported version, desktop entry, and installed icon. The broader Debug, AddressSanitizer, and UndefinedBehaviorSanitizer development matrix remains `nix develop --command ./check.sh nixos`.
