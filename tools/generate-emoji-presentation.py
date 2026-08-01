#!/usr/bin/python3
"""Generate the inclusive Emoji_Presentation range table for EmojiSequence.cxx.

Source: official Unicode emoji-data.txt, property Emoji_Presentation.
Pinned for this project: Unicode 16.0.0 (same major generation as
CharacterCategoryMap.cxx).

Download once:

  curl -fsSL -o /tmp/emoji-data-16.0.txt \\
    https://www.unicode.org/Public/16.0.0/ucd/emoji/emoji-data.txt

Expected SHA-256 of that file:

  f1365a5173eee18e1f98b240cdc492e84a25f1ce7e0c9d1094eb29c41a22696a

Print C++ ranges to stdout:

  python3 tools/generate-emoji-presentation.py /tmp/emoji-data-16.0.txt

Replace the kEmojiPresentationRanges body in scintilla/src/EmojiSequence.cxx
with the printed ranges, and keep the source version comment in sync.
"""

from __future__ import annotations

from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path

EXPECTED_SHA256 = "f1365a5173eee18e1f98b240cdc492e84a25f1ce7e0c9d1094eb29c41a22696a"
UNICODE_VERSION = "16.0.0"


def parse_ranges(text: str) -> list[tuple[int, int]]:
	ranges: list[tuple[int, int]] = []
	for line in text.splitlines():
		if not line or line.startswith("#"):
			continue
		if ";" not in line:
			continue
		left, rest = line.split(";", 1)
		prop = rest.split("#", 1)[0].strip()
		if prop != "Emoji_Presentation":
			continue
		cps = left.strip()
		if ".." in cps:
			start_s, end_s = cps.split("..")
			start, end = int(start_s, 16), int(end_s, 16)
		else:
			start = end = int(cps, 16)
		ranges.append((start, end))
	ranges.sort()
	merged: list[tuple[int, int]] = []
	for start, end in ranges:
		if merged and start <= merged[-1][1] + 1:
			merged[-1] = (merged[-1][0], max(merged[-1][1], end))
		else:
			merged.append((start, end))
	return merged


def main() -> None:
	parser = ArgumentParser(description=__doc__)
	parser.add_argument("emoji_data", type=Path, help="path to emoji-data.txt")
	args = parser.parse_args()
	data = args.emoji_data.read_bytes()
	digest = sha256(data).hexdigest()
	if digest != EXPECTED_SHA256:
		raise SystemExit(
			f"unexpected emoji-data.txt SHA-256: {digest}\n"
			f"expected Unicode {UNICODE_VERSION}: {EXPECTED_SHA256}"
		)
	ranges = parse_ranges(data.decode("utf-8"))
	total = sum(end - start + 1 for start, end in ranges)
	print(f"// Inclusive [lo, hi] ranges: Emoji_Presentation=Yes.")
	print(f"// Generated from Unicode {UNICODE_VERSION} ucd/emoji/emoji-data.txt")
	print(f"// SHA-256 {EXPECTED_SHA256}")
	print(f"// {len(ranges)} ranges, {total} code points.")
	print("// Regenerate: tools/generate-emoji-presentation.py <emoji-data.txt>")
	print("constexpr char32_t kEmojiPresentationRanges[][2] = {")
	for start, end in ranges:
		print(f"\t{{0x{start:04X}, 0x{end:04X}}},")
	print("};")


if __name__ == "__main__":
	main()
