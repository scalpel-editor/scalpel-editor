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
          mesa
          pcre2
          systemd
          wayland
          wayland-protocols
        ];

        SCALPEL_TEST_EGL_VENDOR_LIBRARY_FILENAMES =
          "${pkgs.mesa}/share/glvnd/egl_vendor.d/50_mesa.json";
        SCALPEL_TEST_LIBGL_DRIVERS_PATH = "${pkgs.mesa}/lib/dri";
      };
    };
}
