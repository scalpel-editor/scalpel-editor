#!/usr/bin/env bash
# Build a Release binary and install it for the current user under ~/.local.
# Installs the binary, app icon, and desktop entry.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${HOME}/.local"
BUILD_DIR="${ROOT}/build-release"

echo "Configuring Release build in ${BUILD_DIR}"
cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF

echo "Building scalpel-editor"
cmake --build "${BUILD_DIR}" --target scalpel-editor

echo "Installing to ${PREFIX}"
cmake --install "${BUILD_DIR}" --prefix "${PREFIX}"

if command -v update-desktop-database >/dev/null 2>&1; then
	update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
	gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" 2>/dev/null || true
fi

echo "Installed:"
echo "  ${PREFIX}/bin/scalpel-editor"
echo "  ${PREFIX}/share/icons/hicolor/256x256/apps/scalpel-editor.png"
echo "  ${PREFIX}/share/applications/scalpel-editor.desktop"
echo
if [[ ":${PATH}:" != *":${PREFIX}/bin:"* ]]; then
	echo "Note: ${PREFIX}/bin is not on PATH; add it so menus and the shell can find scalpel-editor."
fi
