#!/usr/bin/python3
"""Generate the committed GitHub shortcode tables for EmojiCatalogData.inc.

Source: github/gemoji db/emoji.json (MIT). Only Unicode emoji and their
aliases are kept; tags, categories, and skin-tone flags are ignored.

Download once:

  curl -fsSL -o /tmp/gemoji-emoji.json \\
    https://raw.githubusercontent.com/github/gemoji/master/db/emoji.json

Expected SHA-256 of that file:

  b174ae2aeb321b52f64adb9ff412f966a7f338839d780784dd15dcad702c2dd6

Print the include body to stdout:

  python3 tools/generate-emoji-catalog.py /tmp/gemoji-emoji.json \\
    > app/EmojiCatalogData.inc
"""

from __future__ import annotations

import json
from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path

EXPECTED_SHA256 = (
	"b174ae2aeb321b52f64adb9ff412f966a7f338839d780784dd15dcad702c2dd6"
)
SOURCE_URL = "https://raw.githubusercontent.com/github/gemoji/master/db/emoji.json"


def cxx_string(value: str) -> str:
	escaped = []
	for char in value:
		if char in '\\"':
			escaped.append("\\")
		escaped.append(char)
	return '"' + "".join(escaped) + '"'


def load_entries(text: str) -> tuple[list[tuple[str, str]], list[tuple[str, int]]]:
	payload = json.loads(text)
	emoji: list[tuple[str, str]] = []
	aliases: list[tuple[str, int]] = []
	seen_alias: dict[str, int] = {}
	for item in payload:
		glyph = item.get("emoji")
		names = item.get("aliases")
		if not isinstance(glyph, str) or not glyph:
			continue
		if not isinstance(names, list) or not names:
			continue
		preferred = None
		index = len(emoji)
		for name in names:
			if not isinstance(name, str) or not name:
				continue
			if name in seen_alias:
				continue
			if preferred is None:
				preferred = name
			seen_alias[name] = index
			aliases.append((name, index))
		if preferred is None:
			continue
		emoji.append((glyph, preferred))
	aliases.sort(key=lambda row: row[0])
	return emoji, aliases


def main() -> None:
	parser = ArgumentParser(description=__doc__)
	parser.add_argument("emoji_json", type=Path, help="path to gemoji emoji.json")
	args = parser.parse_args()
	data = args.emoji_json.read_bytes()
	digest = sha256(data).hexdigest()
	if digest != EXPECTED_SHA256:
		raise SystemExit(
			f"unexpected emoji.json SHA-256: {digest}\n"
			f"expected: {EXPECTED_SHA256}"
		)
	emoji, aliases = load_entries(data.decode("utf-8"))
	print("// GitHub shortcode catalog: Unicode emoji and aliases.")
	print(f"// Generated from gemoji {SOURCE_URL}")
	print(f"// SHA-256 {EXPECTED_SHA256}")
	print(f"// {len(emoji)} emoji, {len(aliases)} aliases.")
	print("// Regenerate: tools/generate-emoji-catalog.py <emoji.json>")
	print("constexpr std::string_view kEmojiGlyphs[] = {")
	for glyph, _preferred in emoji:
		print(f"\t{cxx_string(glyph)},")
	print("};")
	print("constexpr std::string_view kPreferredAliases[] = {")
	for _glyph, preferred in emoji:
		print(f"\t{cxx_string(preferred)},")
	print("};")
	print("struct EmojiAliasRow {")
	print("\tstd::string_view alias;")
	print("\tuint16_t emojiIndex;")
	print("};")
	print("constexpr EmojiAliasRow kAliases[] = {")
	for alias, index in aliases:
		print(f"\t{{{cxx_string(alias)}, {index}}},")
	print("};")


if __name__ == "__main__":
	main()
