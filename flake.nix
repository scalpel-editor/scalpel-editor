{
  description = "Development environment for scalpel-editor";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        nativeBuildInputs = with pkgs; [
          clang
          cmake
          gcc
          ninja
          pkg-config
          python3
          wayland-scanner
        ];

        buildInputs = with pkgs; [
          dbus
          expat
          fontconfig
          freetype
          glib
          harfbuzz
          libffi
          libglvnd
          libsysprof-capture
          libxkbcommon
          pcre2
          systemd
          wayland
          wayland-protocols
        ];
      };
    };
}
