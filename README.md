# scalpel-editor

scalpel-editor is a Wayland-only plain-text editor built from the Scintilla 5.6.4 core. The core has been substantially refactored for a standalone application: application features use named typed operations instead of Scintilla's generated numeric message interface, and the platform layer is a direct Wayland, EGL, OpenGL, FreeType, HarfBuzz, and Fontconfig implementation.

The editor has no GTK, Qt, or general-purpose UI toolkit dependency. Its application chrome is a small fixed set of controls composed directly with the editor.

The application supports multiple tabs, desktop-portal open and save dialogs, file-manager open for plain text and Markdown, Markdown token highlighting for `.md` and `.markdown` files, atomic whole-file saves with save-time external-change detection, dirty-buffer close prompts, recent files, menu and keyboard actions, two-axis scrollbars, clipboard and primary selection, text-input-v3 IME, compositor-driven key repeat, cursor themes, fractional scaling, damage-aware frame pacing, and optional presentation feedback.

## Compositor support

The app requests server-side decorations when the compositor provides them. It does not draw a titlebar, window buttons, or resize borders of its own, and it does not start a move or resize from its chrome.

It was developed on KDE Plasma (KWin). KWin draws the titlebar and resize borders, so the window can be moved, resized, and closed in the usual way.

On a tiling compositor such as Hyprland, the compositor assigns size and position, so the missing titlebar is not a barrier to use.

GNOME (Mutter) does not draw server-side decorations. The window still appears: on Wayland the compositor always chooses the initial position. GNOME is a floating desktop, not a tiling layout, so after that first placement you move and size the window with compositor controls rather than a titlebar. Super+drag moves it, Super+middle-click drag or Alt+F8 resizes it, Super+arrows tile or maximize it, and Alt+F4 or File → Quit closes it. Mutter can center new windows (`gsettings set org.gnome.mutter center-new-windows true`); it has no built-in always-tile mode.

Pull requests that add direct client-side decorations will be considered. **Pull requests that add libdecor will be rejected**, because libdecor would add dependencies and force a refactor that works against this app's direct design.

## Building

See [BUILDING.md](BUILDING.md) for the openSUSE and NixOS build environments.

## Installing on NixOS

See [packaging/nix/README.md](packaging/nix/README.md) to run scalpel-editor directly, install it into a user profile, or add it to a declarative NixOS configuration.

## Installing on openSUSE Leap 16

See [packaging/rpm/README.md](packaging/rpm/README.md) to build an RPM and install it with `zypper`.

For local installation you can use `./install.sh`.

## Design documentation

- [DeepWiki](https://deepwiki.com/scalpel-editor/scalpel-editor)
- [Scintilla core boundary](docs/scintilla-core.md)
- [Application UI](docs/application-ui.md)
- [Rendering](docs/rendering.md)
- [Wayland shell](docs/wayland.md)
- [Design principles](docs/design-principles.md)
- [Lessons for custom Wayland UI applications](docs/custom-wayland-ui.md)

## License

Project-owned code, documentation, and artwork are licensed under the [Blue Oak Model License 1.0.0](LICENSE.md). The Scintilla-derived core and the in-tree [Lexilla Markdown extract](lexilla/License.txt) remain under the Scintilla/Lexilla license, identified by the SPDX short identifier `HPND`. Separately licensed test material retains the license identified alongside it.

**As far as the law allows, this software comes as is, without any warranty or condition, and no contributor will be liable to anyone for any damages related to this software or this license, under any kind of legal claim.**
