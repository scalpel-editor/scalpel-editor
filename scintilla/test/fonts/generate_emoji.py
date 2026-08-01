#!/usr/bin/python3.13

"""Generate the deterministic CBDT/CBLC emoji fixture from pinned Noto Color Emoji."""

from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path

import fontTools
from fontTools.subset import Options, Subsetter
from fontTools.ttLib import TTFont

EXPECTED_FONTTOOLS_VERSION = "4.53.1"
# Noto Color Emoji 2.042 as installed from the openSUSE noto-coloremoji-fonts package
# (file NotoColorEmoji.ttf). Recorded on the development host; regenerate only from
# a file with this digest.
SOURCE_SHA256 = "c2f19f6a404baa7da7a710b018c2892d7b51386983ddca146811f76aea0b6861"
EXPECTED_VERSION_PREFIX = "Version 2.042"
OUTPUT_SHA256 = "a4d97e4fdb97fa2c2e108d92b4d3ac6ab6fe9c787dcff8de000793a18f0775b7"
FAMILY = "Scalpel Emoji Fixture"
POSTSCRIPT = "ScalpelEmojiFixture"
RENAMED_NAME_IDS = (1, 2, 3, 4, 6, 16, 17, 21, 22)

# Minimal set for shaping tests: plain emoji, variation base, skin tone, ZWJ
# parts, one flag pair, keycap parts, and space.
COVERAGE = frozenset({
	0x0020,
	0x0023,
	0x002A,
	0x0030,
	0x0031,
	0x200D,
	0x20E3,
	0x263A,
	0x1F1F8,
	0x1F1FA,
	0x1F3FB,
	0x1F44D,
	0x1F469,
	0x1F600,
	0x1F680,
})


def digest(data: bytes) -> str:
	return sha256(data).hexdigest()


def rename(font: TTFont) -> None:
	name_table = font["name"]
	for name_id in RENAMED_NAME_IDS:
		name_table.removeNames(nameID=name_id)
	names = {
		1: FAMILY,
		2: "Regular",
		3: f"2.042;SCALPEL;{POSTSCRIPT}",
		4: f"{FAMILY} Regular",
		6: POSTSCRIPT,
		16: FAMILY,
		17: "Regular",
	}
	for name_id, value in names.items():
		name_table.setName(value, name_id, 3, 1, 0x0409)


def verify(font: TTFont) -> None:
	cmap = frozenset(font.getBestCmap() or {})
	if cmap != COVERAGE:
		raise ValueError(f"unexpected cmap: {sorted(cmap)}")
	if "CBDT" not in font or "CBLC" not in font:
		raise ValueError("fixture must retain CBDT/CBLC colour tables")
	if font["name"].getDebugName(1) != FAMILY:
		raise ValueError("unexpected family name")
	if not font["name"].getDebugName(0) or not font["name"].getDebugName(13):
		raise ValueError("missing copyright or license metadata")


def main() -> None:
	parser = ArgumentParser(description=__doc__)
	parser.add_argument("noto_color_emoji_ttf", type=Path)
	args = parser.parse_args()

	if fontTools.__version__ != EXPECTED_FONTTOOLS_VERSION:
		raise RuntimeError(
			f"fontTools {EXPECTED_FONTTOOLS_VERSION} is required for byte-for-byte output; "
			f"found {fontTools.__version__}"
		)

	source = args.noto_color_emoji_ttf.read_bytes()
	if digest(source) != SOURCE_SHA256:
		raise ValueError(f"unexpected source SHA-256: {digest(source)}")

	with TTFont(args.noto_color_emoji_ttf, recalcTimestamp=False) as source_font:
		version = source_font["name"].getDebugName(5) or ""
		if not version.startswith(EXPECTED_VERSION_PREFIX):
			raise ValueError(f"unexpected source version: {version!r}")
		missing = COVERAGE - set(source_font.getBestCmap() or {})
		if missing:
			raise ValueError(f"source lacks required code points: {sorted(missing)}")

	options = Options(
		layout_features=["*"],
		name_IDs=["*"],
		name_legacy=True,
		name_languages=["*"],
		recalc_timestamp=False,
	)
	font = TTFont(args.noto_color_emoji_ttf, recalcTimestamp=False)
	subsetter = Subsetter(options)
	subsetter.populate(unicodes=COVERAGE)
	subsetter.subset(font)
	rename(font)
	verify(font)

	output = Path(__file__).resolve().parent / "EmojiFixture.ttf"
	font.save(output, reorderTables=None)
	font.close()
	actual = digest(output.read_bytes())
	if actual != OUTPUT_SHA256:
		raise ValueError(f"unexpected output SHA-256: {actual}")
	with TTFont(output, recalcTimestamp=False) as saved:
		verify(saved)
	print(f"wrote {output} ({actual})")


if __name__ == "__main__":
	main()
