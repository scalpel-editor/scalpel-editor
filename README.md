# scalpel-editor

scalpel-editor is a Wayland-only plain-text editor built from the Scintilla 5.6.4 core. The core has been substantially refactored for a standalone application: application features use named typed operations instead of Scintilla's generated numeric message interface, and the platform layer is a direct Wayland, EGL, OpenGL, FreeType, HarfBuzz, and Fontconfig implementation.

The editor has no GTK, Qt, or general-purpose UI toolkit dependency. Its application chrome is a small fixed set of controls composed directly with the editor.

## Current scope

The application supports multiple tabs, desktop-portal open and save dialogs, atomic whole-file saves, dirty-buffer close prompts, recent files, menu and keyboard actions, two-axis scrollbars, clipboard and primary selection, text-input-v3 IME, compositor-driven key repeat, cursor themes, fractional scaling, damage-aware frame pacing, and optional presentation feedback.

With no arguments, `scalpel-editor` opens an untitled interactive workspace. With one or more paths, it opens those files as the initial tab set in argument order, reuses the first document for the first distinct path, activates the tab named by the last supplied path, binds each path for save, and does not add any of them to recent files. Startup is all-or-nothing: if any path is empty or unreadable, the process reports failure and does not enter the event loop. Use `--` before paths that begin with `-`. The process stays in the foreground until the window closes. A Wayland session is required for every launch form.

The project is still under active development. Text shaping currently targets left-to-right English. Autocomplete, call tips, and the Scintilla context menu remain compiled but do not yet have real Wayland popup surfaces. Lexilla integration, Markdown styling, bidirectional layout, drag and drop, and a system accessibility caret are outside the current application scope.

## Building

See [BUILDING.md](BUILDING.md) for the openSUSE and NixOS build environments. The normal development build is:

```sh
cmake --preset dev
cmake --build build --target scalpel-editor
```

## As a Git commit message editor

Git can open `scalpel-editor` for `COMMIT_EDITMSG` and similar one-file editor paths. Configure either:

```sh
git config --global core.editor scalpel-editor
```

or:

```sh
export GIT_EDITOR=scalpel-editor
```

Git supplies one path. That one-path launch stays compatible with the multi-path rules above: the editor loads the file into the sole initial tab, leaves Git's template comments unchanged, does not record the path as recent, and writes saves back to the same path (including Ctrl+S and the dirty-close Save choice). There is no `--wait` flag: the process remains in the foreground until the window closes.

Exit status:

| Outcome | Status |
| --- | --- |
| Accepted window close after a normal session | 0 |
| Invalid command line or failure to read any startup path | non-zero |
| Forced shell or compositor shutdown during a pathname edit | non-zero |
| Uncaught failure after a path or interactive launch is ready | non-zero |

Interactive (no-argument) forced shutdown still returns success, matching the previous process behavior. Git treats a non-zero editor status as an aborted commit.

## Installing on openSUSE Leap 16

See [packaging/rpm/README.md](packaging/rpm/README.md) to build an RPM and install it with `zypper`.

## Design documentation

- [Scintilla core boundary](docs/scintilla-core.md)
- [Application UI](docs/application-ui.md)
- [Rendering](docs/rendering.md)
- [Wayland shell](docs/wayland.md)
- [Design principles](docs/design-principles.md)
- [Lessons for custom Wayland UI applications](docs/custom-wayland-ui.md)

## License

Project-owned code, documentation, and artwork are licensed under the [Blue Oak Model License 1.0.0](LICENSE.md). The Scintilla-derived core remains under the [Scintilla license](scintilla/License.txt), identified by the SPDX short identifier `HPND`. Separately licensed test material retains the license identified alongside it.
