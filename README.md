# scalpel-editor

scalpel-editor is a Wayland-only plain-text editor built from the Scintilla 5.6.4 core (markdown highlighting coming soon). The core has been substantially refactored for a standalone application: application features use named typed operations instead of Scintilla's generated numeric message interface, and the platform layer is a direct Wayland, EGL, OpenGL, FreeType, HarfBuzz, and Fontconfig implementation.

The editor has no GTK, Qt, or general-purpose UI toolkit dependency. Its application chrome is a small fixed set of controls composed directly with the editor.

The application supports multiple tabs, desktop-portal open and save dialogs, atomic whole-file saves with save-time external-change detection, dirty-buffer close prompts, recent files, menu and keyboard actions, two-axis scrollbars, clipboard and primary selection, text-input-v3 IME, compositor-driven key repeat, cursor themes, fractional scaling, damage-aware frame pacing, and optional presentation feedback.

## Compositior support

**This app will not appear with a titlebar on any compositor that does not support server-side decorations (e.g. GNOME/mutter).** It was developed on KDE Plasma (KWin), which does support server-side decorations. The app is also usable with compositors that feature automated window placement (e.g. hyprland). Pull-requests adding direct support for client-side decorations will be considered. **Pull-requests that add libdecor support will be rejected**, because using libdecor would add various dependencies and require a refactor that undermines this app's direct design.

## Building

See [BUILDING.md](BUILDING.md) for the openSUSE and NixOS build environments.

## Installing on openSUSE Leap 16

See [packaging/rpm/README.md](packaging/rpm/README.md) to build an RPM and install it with `zypper`.

For local installation you can use `./install.sh`.

## Design documentation

- [Scintilla core boundary](docs/scintilla-core.md)
- [Application UI](docs/application-ui.md)
- [Rendering](docs/rendering.md)
- [Wayland shell](docs/wayland.md)
- [Design principles](docs/design-principles.md)
- [Lessons for custom Wayland UI applications](docs/custom-wayland-ui.md)

## License

Project-owned code, documentation, and artwork are licensed under the [Blue Oak Model License 1.0.0](LICENSE.md). The Scintilla-derived core remains under the [Scintilla license](scintilla/License.txt), identified by the SPDX short identifier `HPND`. Separately licensed test material retains the license identified alongside it.
