#!/usr/bin/env bash
# Remove the local ~/.local install created by install.sh.
set -euo pipefail

PREFIX="${HOME}/.local"

BINARY="${PREFIX}/bin/scalpel-editor"
ICON="${PREFIX}/share/icons/hicolor/256x256/apps/scalpel-editor.png"
DESKTOP="${PREFIX}/share/applications/scalpel-editor.desktop"

removed=0
for path in "${BINARY}" "${ICON}" "${DESKTOP}"; do
	if [[ -e "${path}" ]]; then
		rm -f "${path}"
		echo "Removed ${path}"
		removed=1
	fi
done

if command -v update-desktop-database >/dev/null 2>&1; then
	update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
	gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" 2>/dev/null || true
fi

if [[ "${removed}" -eq 0 ]]; then
	echo "Nothing to remove under ${PREFIX}."
else
	echo "Uninstalled scalpel-editor from ${PREFIX}."
fi
