#!/usr/bin/python3.13

"""Generate the deterministic font fallback fixtures from the pinned release."""

from argparse import ArgumentParser
from hashlib import sha256
from io import BytesIO
from pathlib import Path
from zipfile import ZipFile

import fontTools
from fontTools.subset import Options, Subsetter
from fontTools.ttLib import TTFont


EXPECTED_FONTTOOLS_VERSION = "4.53.1"
ARCHIVE_SHA256 = "346c930bbe8eb946701a05c54e9c11a2094dee1d93c387bf1771c0a3e335688f"
SOURCE_MEMBER = "NotoSansSymbols2/full/ttf/NotoSansSymbols2-Regular.ttf"
SOURCE_SHA256 = "3ce38effdb615dd929c8b0f52768dfa2cd21f206e3824bc7df61e4074b41ae52"
PRINTABLE_ASCII = frozenset(range(0x20, 0x7F))
SNOWMAN = 0x2603
RENAMED_NAME_IDS = (1, 2, 3, 4, 6, 16, 17, 21, 22)


def digest(data: bytes) -> str:
	return sha256(data).hexdigest()


def source_font(archive_path: Path) -> bytes:
	archive = archive_path.read_bytes()
	if digest(archive) != ARCHIVE_SHA256:
		raise ValueError(f"unexpected archive SHA-256: {digest(archive)}")

	with ZipFile(BytesIO(archive)) as release:
		font_data = release.read(SOURCE_MEMBER)
	if digest(font_data) != SOURCE_SHA256:
		raise ValueError(f"unexpected source font SHA-256: {digest(font_data)}")

	with TTFont(BytesIO(font_data), recalcTimestamp=False) as font:
		version = font["name"].getDebugName(5)
		coverage = set(font.getBestCmap())
	if not version or not version.startswith("Version 2.008"):
		raise ValueError(f"unexpected source font version: {version!r}")
	missing = (PRINTABLE_ASCII | {SNOWMAN}) - coverage
	if missing:
		raise ValueError(f"source font lacks required code points: {sorted(missing)}")
	return font_data


def rename(font: TTFont, family: str, postscript_name: str) -> None:
	name_table = font["name"]
	for name_id in RENAMED_NAME_IDS:
		name_table.removeNames(nameID=name_id)

	names = {
		1: family,
		2: "Regular",
		3: f"2.008;SCALPEL;{postscript_name}",
		4: f"{family} Regular",
		6: postscript_name,
		16: family,
		17: "Regular",
	}
	for name_id, value in names.items():
		name_table.setName(value, name_id, 3, 1, 0x0409)


def verify(font: TTFont, family: str, postscript_name: str, coverage: frozenset[int]) -> None:
	actual_coverage = frozenset(font.getBestCmap())
	if actual_coverage != coverage:
		raise ValueError(f"unexpected cmap for {family}: {sorted(actual_coverage)}")
	expected_names = {
		1: family,
		2: "Regular",
		3: f"2.008;SCALPEL;{postscript_name}",
		4: f"{family} Regular",
		6: postscript_name,
		16: family,
		17: "Regular",
	}
	for name_id, expected in expected_names.items():
		actual = font["name"].getDebugName(name_id)
		if actual != expected:
			raise ValueError(f"unexpected name ID {name_id} for {family}: {actual!r}")
	if not font["name"].getDebugName(0) or not font["name"].getDebugName(13) or not font["name"].getDebugName(14):
		raise ValueError(f"missing copyright or license metadata for {family}")


def generate(
	font_data: bytes,
	output: Path,
	family: str,
	postscript_name: str,
	coverage: frozenset[int],
	expected_sha256: str,
) -> None:
	font = TTFont(BytesIO(font_data), recalcTimestamp=False)
	options = Options(
		ignore_missing_unicodes=False,
		name_IDs=["*"],
		name_legacy=True,
		name_languages=["*"],
		recalc_timestamp=False,
	)
	subsetter = Subsetter(options)
	subsetter.populate(unicodes=coverage)
	subsetter.subset(font)
	rename(font, family, postscript_name)
	verify(font, family, postscript_name, coverage)
	font.save(output, reorderTables=None)
	font.close()
	actual_sha256 = digest(output.read_bytes())
	if actual_sha256 != expected_sha256:
		raise ValueError(f"unexpected output SHA-256 for {output.name}: {actual_sha256}")
	with TTFont(output, recalcTimestamp=False) as saved_font:
		verify(saved_font, family, postscript_name, coverage)


def main() -> None:
	parser = ArgumentParser(description=__doc__)
	parser.add_argument("release_archive", type=Path)
	args = parser.parse_args()

	if fontTools.__version__ != EXPECTED_FONTTOOLS_VERSION:
		raise RuntimeError(
			f"fontTools {EXPECTED_FONTTOOLS_VERSION} is required for byte-for-byte output; "
			f"found {fontTools.__version__}"
		)

	font_data = source_font(args.release_archive)
	output_directory = Path(__file__).resolve().parent
	generate(
		font_data,
		output_directory / "FallbackPrimary.ttf",
		"Scalpel Fallback Primary",
		"ScalpelFallbackPrimary-Regular",
		PRINTABLE_ASCII,
		"0ea9689cfac6be740d234ff08e36291d3950ebc3e6af16767eabbe776a1ff99d",
	)
	generate(
		font_data,
		output_directory / "FallbackSnowman.ttf",
		"Scalpel Fallback Snowman",
		"ScalpelFallbackSnowman-Regular",
		frozenset({0x20, SNOWMAN}),
		"cbafa33e4bea5592e61dd11016663e6aa2da3dbe8756e3b0845c8a10c3ea2c5a",
	)


if __name__ == "__main__":
	main()
