{
  description = "Wayland-only plain-text editor";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      scalpel-editor = pkgs.stdenv.mkDerivation {
        pname = "scalpel-editor";
        version = "1.0.0";
        src = ./.;

        strictDeps = true;
        nativeBuildInputs = with pkgs; [
          cmake
          ninja
          pkg-config
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

        postInstall = ''
          install -Dm644 "$src/LICENSE.md" \
            "$out/share/licenses/scalpel-editor/LICENSE.md"
          install -Dm644 "$src/scintilla/License.txt" \
            "$out/share/licenses/scalpel-editor/Scintilla-License.txt"
        '';

        doInstallCheck = true;
        nativeInstallCheckInputs = [ pkgs.desktop-file-utils ];
        installCheckPhase = ''
          runHook preInstallCheck

          version="$($out/bin/scalpel-editor --version)"
          test "$version" = "scalpel-editor 1.0.0"
          desktop-file-validate \
            "$out/share/applications/scalpel-editor.desktop"
          test -f \
            "$out/share/icons/hicolor/256x256/apps/scalpel-editor.png"

          runHook postInstallCheck
        '';

        meta = {
          description = "Wayland-only plain-text editor";
          homepage = "https://github.com/scalpel-editor/scalpel-editor";
          license = with pkgs.lib.licenses; [ blueOak100 hpnd ];
          mainProgram = "scalpel-editor";
          platforms = [ "x86_64-linux" ];
        };
      };
    in
    {
      packages.${system}.default = scalpel-editor;

      apps.${system}.default = {
        type = "app";
        program = "${scalpel-editor}/bin/scalpel-editor";
        meta.description = "Run scalpel-editor";
      };

      checks.${system}.package = scalpel-editor;

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
