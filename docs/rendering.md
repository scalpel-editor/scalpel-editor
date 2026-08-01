# Rendering

The editor uses one concrete rendering implementation for both the live Wayland window and deterministic offscreen tests. Fontconfig selects faces, FreeType loads and rasterizes glyphs, HarfBuzz shapes text, and OpenGL draws Scintilla content and application chrome through EGL contexts.

## Ownership

`GlContext` owns EGL and OpenGL context state. A window context draws to framebuffer 0 through the `wl_egl_window`; a headless context owns an offscreen target. `Renderer` owns drawing programs, glyph textures, and the active draw target. `DrawSurface` implements Scintilla's measurement and drawing surface contract over that renderer.

`ApplicationEditor` owns the production context, renderer, and frame surface. Its resource base is destroyed after `ScintillaBase` releases cached drawing objects, so OpenGL and font resources remain valid during editor teardown.

Pixmap surfaces and offscreen targets own texture-backed colour buffers. OpenGL objects are destroyed while their context is current.

## Font selection

`FontCache::Match` builds an `FcPattern` and adds `FontParameters::faceName` as a literal `FC_FAMILY` string with `FcPatternAddString`. It does not parse the name with `FcNameParse`. Generic menu and default faces therefore use the canonical family strings `monospace`, `serif`, `sans-serif`, and `system-ui` exactly as stored; the active Fontconfig configuration chooses the concrete file.

`system-ui` is a single family name whose hyphen is not a Fontconfig size separator. The diagnostic commands `fc-match system-ui` and `fc-match 'system\-ui'` can disagree for that reason; production code always passes the C string `"system-ui"` and never a backslash-escaped form.

Application chrome (`UiStyle::fontName`) stays on `system-ui` independently of the editor body face. The line-number gutter stays on `monospace` when the body face changes.

## Text shaping

A `ShapedRun` stores the input UTF-8 bytes, HarfBuzz glyphs, per-byte end positions, valid caret stops, direction, and the font face used by each glyph. Measurement, wrapping, hit testing, selection, caret placement, and drawing consume the same cached run.

The per-byte positions satisfy Scintilla's `Surface::MeasureWidths` contract. Bytes in one UTF-8 character share its end position, and positions inside a merged shaping cluster are not caret stops. Invalid UTF-8 bytes follow the editor's byte-preserving policy.

The current shaper uses fixed Latin and English properties and supports left-to-right text only. Font fallback is selected per span. Discretionary ligatures are disabled so editor movement and display remain predictable. Other scripts and mixed-direction line ordering require extending this one shaped-run model rather than introducing a parallel layout path.

## Coordinates and pixels

Drawing accepts logical, top-left coordinates with half-open rectangles. The renderer maps that space onto a buffer-sized OpenGL viewport. Window buffers may have more pixels than the logical surface when integer or fractional scaling is active.

Colour attachments are linear `GL_RGBA8`. Internal alpha is premultiplied and blended with premultiplied source-over. Public offscreen pixel buffers are converted to straight alpha and returned in top-to-bottom order.

## Application composition

Scintilla paints the editor client. A permanent-chrome callback paints the menu bar, tab strip, scrollbars, and scrollbar junction without forcing full-frame damage. A post-paint overlay callback paints exactly one open menu, unsaved-changes card, or file-error card; transparent overlays expand painting to the full frame and use a full swap so preserved pixels cannot accumulate blending.

`ApplicationUi` owns the chrome models and painters, binds both application paint callbacks, selects the active overlay, and unbinds callbacks that capture it when it is destroyed. Individual controls own their layout, hit testing, interaction state, and paint operations without a widget hierarchy. The complete composition and input boundary is described in [application-ui.md](application-ui.md).

`ApplicationEditor` supplies its client rectangle and scrollbar metrics. `ApplicationUi::BeginFrameLayout` refreshes the model values that affect geometry and retains one `ApplicationLayout` snapshot containing the menu, tabs, optional find bar, scrollbars, client, and modal cards. Permanent-chrome and overlay painters read that same snapshot until `EndFrameLayout`.

Autocomplete lists, call tips, and the Scintilla context menu use explicit production stubs. Their core behavior remains compiled and testable, but real Wayland popup surfaces are not implemented.

## Testing

Offscreen renderer tests use the production OpenGL path with controlled fonts and compare exact geometry and pixels where stable. Editor tests paint through the same `DrawSurface` implementation while retaining host observations. Wayland frame and scale tests verify damage conversion separately from live compositor submission.
