Name:           scalpel-editor
Version:        0.1.0
Release:        1
Summary:        Wayland-only plain-text editor
License:        BlueOak-1.0.0 AND HPND
Source0:        %{name}-%{version}.tar.gz
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

%files
%license LICENSE.md
%license scintilla/License.txt
%doc README.md
%{_bindir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/256x256/apps/%{name}.png

%changelog
* Thu Aug 06 2026 Third-Thing <219055174+Third-Thing@users.noreply.github.com> - 0.1.0-1
- Add the initial openSUSE Leap 16 package.
