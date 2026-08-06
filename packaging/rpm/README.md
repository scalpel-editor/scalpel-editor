# Installing the RPM on openSUSE Leap 16

The RPM installs `scalpel-editor` under `/usr/bin` together with its desktop launcher and icon. Runtime library dependencies are generated from the linked executable, and the package requires the desktop portal service used by the editor's Open and Save dialogs.

## Install the package-building tools

On openSUSE Leap 16, install the compiler, RPM tools, and development packages used by the spec file:

```sh
sudo zypper install \
  git rpm-build cmake gcc-c++ make desktop-file-utils pkgconf-pkg-config \
  wayland-devel wayland-protocols-devel libxkbcommon-devel dbus-1-devel \
  freetype2-devel fontconfig-devel harfbuzz-devel \
  Mesa-libGL-devel Mesa-libEGL-devel
```

## Build and install

From the repository root, build both the binary RPM and its source RPM:

```sh
./packaging/rpm/build.sh
```

Install the locally built, unsigned package with `zypper`; the architecture directory is normally `x86_64` or `aarch64`:

```sh
sudo zypper install --allow-unsigned-rpm \
  .rpm-build/RPMS/*/scalpel-editor-0.1.0-1.*.rpm
```

`zypper` resolves and installs the runtime dependencies. Start the editor from the desktop application menu or run `scalpel-editor` in a Wayland session.

To remove every file owned by the RPM:

```sh
sudo zypper remove scalpel-editor
```

Rebuilding the same version and release does not make it newer from RPM's point of view. While iterating on packaging, reinstall it explicitly:

```sh
sudo zypper install --force --allow-unsigned-rpm \
  .rpm-build/RPMS/*/scalpel-editor-0.1.0-1.*.rpm
```

## Distribution metadata still needed

This checkout has no configured upstream URL, so the local-source spec intentionally has no `URL` tag. Add the project's canonical public URL and use its release archive for `Source0` before submitting the package to the Open Build Service.

The spec uses `BlueOak-1.0.0 AND HPND` because the package combines project-owned work under the Blue Oak Model License 1.0.0 with the Scintilla-derived core under the Scintilla license. Both license texts are installed in the binary RPM.
