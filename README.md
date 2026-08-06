# scalpel-editor

scalpel-editor is a Wayland-only plain-text editor built from the Scintilla 5.6.4 core. The core has been substantially refactored for a standalone application: application features use named typed operations instead of Scintilla's generated numeric message interface, and the platform layer is a direct Wayland, EGL, OpenGL, FreeType, HarfBuzz, and Fontconfig implementation.

The editor has no GTK, Qt, or general-purpose UI toolkit dependency. Its application chrome is a small fixed set of controls composed directly with the editor.

## Current scope

The application supports multiple tabs, desktop-portal open and save dialogs, atomic whole-file saves, dirty-buffer close prompts, recent files, menu and keyboard actions, two-axis scrollbars, clipboard and primary selection, text-input-v3 IME, compositor-driven key repeat, cursor themes, fractional scaling, damage-aware frame pacing, and optional presentation feedback.

The project is still under active development. Text shaping currently targets left-to-right English. Autocomplete, call tips, and the Scintilla context menu remain compiled but do not yet have real Wayland popup surfaces. Lexilla integration, Markdown styling, bidirectional layout, drag and drop, and a system accessibility caret are outside the current application scope.

## Building

See [BUILDING.md](BUILDING.md) for the openSUSE and NixOS build environments. The normal development build is:

```sh
cmake --preset dev
cmake --build build --target scalpel-editor
```

## Installing on openSUSE Leap 16

See [packaging/README.md](packaging/README.md) to build an RPM and install it with `zypper`.

## Design documentation

- [Scintilla core boundary](docs/scintilla-core.md)
- [Application UI](docs/application-ui.md)
- [Rendering](docs/rendering.md)
- [Wayland shell](docs/wayland.md)
- [Design principles](docs/design-principles.md)
- [Lessons for custom Wayland UI applications](docs/custom-wayland-ui.md)

## License

Project-owned code, documentation, and artwork are licensed under the [Blue Oak Model License 1.0.0](LICENSE.md). The Scintilla-derived core remains under the [Scintilla license](scintilla/License.txt), identified by the SPDX short identifier `HPND`. Separately licensed test material retains the license identified alongside it.
