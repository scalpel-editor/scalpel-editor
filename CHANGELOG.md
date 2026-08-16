# Changelog

User-visible changes after the 1.0.0 release.

## 2.0.0 - 2026-08-15

### Added

- Markdown files now receive token highlighting. A bound path whose last suffix is `.md` or `.markdown` (ASCII, case-insensitive) uses the bundled Lexilla Markdown lexer. Untitled buffers and every other suffix stay plain text.
- Highlighted Markdown uses a fixed presentation that survives Font menu changes: ordinary text stays on the chosen body face, strong text and complete ATX headings are bold, emphasis is italic, and code uses a monospace face with a restrained background. Links, quotes, list markers, rules, and strikeout tokens have distinct colours. Point size is unchanged. The bundled lexer styles only the underline marker of a Setext heading, not the heading text.
- Language follows the bound path after Open, Recent, startup load, and a successful Save As. Saving to the same path, reloading after an external change, selecting a tab that already has that path, and cancelled or failed operations leave the existing language.
- Opening a Markdown file styles only the range the next paint needs, not the whole document at once.
- Packages install Lexilla's license notice next to Scintilla's. The executable links the lexer statically and does not depend on a Lexilla shared library.

### Changed

- Editor text and the selection margin now paint directly into the window frame. The previous per-line offscreen copy avoided flicker on GDI surfaces and is unused on this EGL path. Body text now shares the frame's text-shaping cache with chrome. Indent-guide and fold-margin stamps are unchanged.

## 1.0.0 - 2026-08-15

First public release of the Wayland-only plain-text editor.
