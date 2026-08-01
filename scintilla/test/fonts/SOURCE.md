# Font fallback fixtures

`FallbackPrimary.ttf` and `FallbackSnowman.ttf` are test-only subsets of Noto Sans Symbols 2 Regular. They deliberately use the same source font so a fallback test does not also change font metrics, hinting, or font format.

Upstream: `notofonts/symbols`

Release: `NotoSansSymbols2-v2.008` (`25b00f0d3f40873a005287514ba2a48558655314`)

Release archive: `NotoSansSymbols2-v2.008.zip`

Release archive SHA-256: `346c930bbe8eb946701a05c54e9c11a2094dee1d93c387bf1771c0a3e335688f`

Source member: `NotoSansSymbols2/full/ttf/NotoSansSymbols2-Regular.ttf`

Source font SHA-256: `3ce38effdb615dd929c8b0f52768dfa2cd21f206e3824bc7df61e4074b41ae52`

Generator: fontTools 4.53.1 from openSUSE package `python313-FontTools`

Generate the fixtures from the release archive with:

```sh
python3.13 scintilla/test/fonts/generate.py /path/to/NotoSansSymbols2-v2.008.zip
```

`FallbackPrimary.ttf` contains printable ASCII (`U+0020-007E`) and deliberately omits `U+2603`. `FallbackSnowman.ttf` contains only `U+0020` and `U+2603`. A fallback test should use text such as `A`, `U+2603`, `B` and assert that only `U+2603` uses the fallback face.

`FallbackPrimary.ttf` SHA-256: `0ea9689cfac6be740d234ff08e36291d3950ebc3e6af16767eabbe776a1ff99d`

`FallbackSnowman.ttf` SHA-256: `cbafa33e4bea5592e61dd11016663e6aa2da3dbe8756e3b0845c8a10c3ea2c5a`

Both subsets have project-specific family, full, unique, PostScript, and typographic family names so Fontconfig does not merge them with the upstream family or each other. The upstream copyright, version, and OFL metadata remain embedded in each font. The complete license is in [OFL.txt](OFL.txt).

## Colour emoji fixture

`EmojiFixture.ttf` is a minimal CBDT/CBLC subset of Noto Color Emoji used for deterministic emoji shaping and colour-raster tests. Rendering tests must use this file rather than the host's installed emoji font.

Upstream: `googlefonts/noto-emoji` (Noto Color Emoji)

Source version: `Version 2.042;GOOG;noto-emoji:20231129:7f49a00d523ae5f94e52fd9f9a39bac9cf65f958`

Source file: `NotoColorEmoji.ttf` (openSUSE package `noto-coloremoji-fonts` on the development host)

Source file SHA-256: `c2f19f6a404baa7da7a710b018c2892d7b51386983ddca146811f76aea0b6861`

Generator: fontTools 4.53.1 from openSUSE package `python313-FontTools`

Generate the fixture with:

```sh
python3.13 scintilla/test/fonts/generate_emoji.py /path/to/NotoColorEmoji.ttf
```

Coverage is limited to space, keycap bases (`0` `1` `#` `*`), ZWJ, combining enclosing keycap, white smiling face, regional indicators for the US flag, one skin-tone modifier, thumbs-up, woman, grinning face, and rocket. GSUB is retained so ZWJ, flag, keycap, and modifier sequences still ligate. The family is renamed to `Scalpel Emoji Fixture` so Fontconfig does not merge it with the installed Noto Color Emoji family. Upstream copyright and OFL metadata remain embedded; the complete license is in [OFL.txt](OFL.txt).

`EmojiFixture.ttf` SHA-256: `a4d97e4fdb97fa2c2e108d92b4d3ac6ab6fe9c787dcff8de000793a18f0775b7`
