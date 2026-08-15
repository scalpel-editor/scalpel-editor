Name:           scalpel-editor
Version:        1.0.0
Release:        1
Summary:        Wayland-only plain-text editor
License:        BlueOak-1.0.0 AND HPND
URL:            https://github.com/scalpel-editor/scalpel-editor
Source0:        %{url}/archive/refs/tags/%{version}.tar.gz#/%{name}-%{version}.tar.gz
BuildRequires:  binutils
BuildRequires:  cmake >= 3.25
BuildRequires:  desktop-file-utils
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  pkgconfig
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(fontconfig)
BuildRequires:  pkgconfig(freetype2)
BuildRequires:  pkgconfig(gl)
BuildRequires:  pkgconfig(harfbuzz)
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-cursor)
BuildRequires:  pkgconfig(wayland-egl)
BuildRequires:  pkgconfig(wayland-protocols)
BuildRequires:  pkgconfig(xkbcommon)
Requires:       xdg-desktop-portal

%description
scalpel-editor is a Wayland-only plain-text editor built from a refactored
Scintilla core. It uses Wayland, EGL, OpenGL, FreeType, HarfBuzz, and
Fontconfig directly instead of a general-purpose user-interface toolkit.

%prep
%autosetup

%build
%cmake -DBUILD_TESTING=OFF
%cmake_build scalpel-editor

%install
%cmake_install

%check
desktop-file-validate \
  %{buildroot}%{_datadir}/applications/%{name}.desktop
if readelf -d %{buildroot}%{_bindir}/%{name} | grep NEEDED | grep -qi lexilla; then
  echo "installed executable links a Lexilla shared library" >&2
  exit 1
fi

%files
%license LICENSE.md
%license scintilla/License.txt
%license lexilla/License.txt
%doc README.md
%{_bindir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/256x256/apps/%{name}.png

%changelog
* Fri Aug 14 2026 Third-Thing <219055174+Third-Thing@users.noreply.github.com> - 1.0.0-1
- Add the initial openSUSE Leap 16 package.
