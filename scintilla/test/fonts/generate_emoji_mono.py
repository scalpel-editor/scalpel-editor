#!/usr/bin/python3.13

"""Generate a monochrome fixture that overlaps the colour emoji repertoire.

Noto Sans Symbols 2 (the source of FallbackPrimary/FallbackSnowman) does not
cover U+1F600 or U+263A. This fixture is a DejaVu Sans subset used only so
presentation-aware fallback tests can give the primary a monochrome glyph for
those code points without relying on the host font set.
"""

from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path

import fontTools
from fontTools.subset import Options, Subsetter
from fontTools.ttLib import TTFont

EXPECTED_FONTTOOLS_VERSION = "4.53.1"
# DejaVu Sans 2.37 as installed from the openSUSE dejavu-fonts package.
SOURCE_SHA256 = "7da195a74c55bef988d0d48f9508bd5d849425c1770dba5d7bfc6ce9ed848954"
EXPECTED_VERSION_PREFIX = "Version 2.37"
OUTPUT_SHA256 = "1674a9274f5e1f546bec838b3c664b288cf59eb529f6ad8b193992f5b1b23eff"
FAMILY = "Scalpel Fallback Emoji Mono"
POSTSCRIPT = "ScalpelFallbackEmojiMono-Regular"
RENAMED_NAME_IDS = (1, 2, 3, 4, 6, 16, 17, 21, 22)

# Printable ASCII plus the colour-fixture overlap used by presentation tests.
PRINTABLE_ASCII = frozenset(range(0x20, 0x7F))
COVERAGE = PRINTABLE_ASCII | frozenset({0x263A, 0x1F600})


def digest(data: bytes) -> str:
	return sha256(data).hexdigest()


def rename(font: TTFont) -> None:
	name_table = font["name"]
	for name_id in RENAMED_NAME_IDS:
		name_table.removeNames(nameID=name_id)
	names = {
		1: FAMILY,
		2: "Regular",
		3: f"2.37;SCALPEL;{POSTSCRIPT}",
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
	for colour_table in ("CBDT", "CBLC", "COLR", "CPAL", "sbix", "SVG "):
		if colour_table in font:
			raise ValueError(f"fixture must stay monochrome; found {colour_table}")
	if font["name"].getDebugName(1) != FAMILY:
		raise ValueError("unexpected family name")
	if not font["name"].getDebugName(0):
		raise ValueError("missing copyright metadata")


def main() -> None:
	parser = ArgumentParser(description=__doc__)
	parser.add_argument("dejavu_sans_ttf", type=Path)
	args = parser.parse_args()

	if fontTools.__version__ != EXPECTED_FONTTOOLS_VERSION:
		raise RuntimeError(
			f"fontTools {EXPECTED_FONTTOOLS_VERSION} is required for byte-for-byte output; "
			f"found {fontTools.__version__}"
		)

	source = args.dejavu_sans_ttf.read_bytes()
	if digest(source) != SOURCE_SHA256:
		raise ValueError(f"unexpected source SHA-256: {digest(source)}")

	with TTFont(args.dejavu_sans_ttf, recalcTimestamp=False) as source_font:
		version = source_font["name"].getDebugName(5) or ""
		if not version.startswith(EXPECTED_VERSION_PREFIX):
			raise ValueError(f"unexpected source version: {version!r}")
		missing = COVERAGE - set(source_font.getBestCmap() or {})
		if missing:
			raise ValueError(f"source lacks required code points: {sorted(missing)}")

	options = Options(
		ignore_missing_unicodes=False,
		name_IDs=["*"],
		name_legacy=True,
		name_languages=["*"],
		recalc_timestamp=False,
	)
	font = TTFont(args.dejavu_sans_ttf, recalcTimestamp=False)
	subsetter = Subsetter(options)
	subsetter.populate(unicodes=COVERAGE)
	subsetter.subset(font)
	rename(font)
	verify(font)

	output = Path(__file__).resolve().parent / "FallbackEmojiMono.ttf"
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
