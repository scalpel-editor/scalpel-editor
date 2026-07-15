#!/bin/sh

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
iface="$root/scintilla/include/Scintilla.iface"
guide="$root/MESSAGE_REMOVAL.md"
tmp=${TMPDIR:-/tmp}/scalpel-message-inventory.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

awk '/^(fun|get|set|evt) / {
	name = $3
	sub(/=.*/, "", name)
	print name
}' "$iface" | sort > "$tmp/interface"

sed -n '/^## Interface Inventory$/,$p' "$guide" |
	tr '`' '\n' |
	awk '/^(fun|get|set|evt) [A-Za-z0-9_]+=/ {
		name = $2
		sub(/=.*/, "", name)
		print name
	}' |
	sort > "$tmp/inventory"

if ! diff -u "$tmp/interface" "$tmp/inventory"; then
	echo "MESSAGE_REMOVAL.md inventory does not match Scintilla.iface" >&2
	exit 1
fi

count=$(wc -l < "$tmp/inventory")
if [ "$count" -ne 853 ]; then
	echo "expected 853 callable entries and notifications, found $count" >&2
	exit 1
fi

echo "message inventory: 853 entries match Scintilla.iface"
