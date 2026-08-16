# Changelog

User-visible changes after the 1.0.0 release.

## 1.0.1 - 2026-08-15

### Changed

- Editor text and the selection margin now paint directly into the window frame. The previous per-line offscreen copy avoided flicker on GDI surfaces and is unused on this EGL path. Body text now shares the frame's text-shaping cache with chrome. Indent-guide and fold-margin stamps are unchanged.

## 1.0.0 - 2026-08-15

First public release of the Wayland-only plain-text editor.
