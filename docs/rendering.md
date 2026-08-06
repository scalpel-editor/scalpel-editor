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

Each loaded `FontFace` keeps a stable copy of the requested family, size, weight, italic state, stretch, and the grayscale raster policy read from the Fontconfig match (or supplied for explicit-path loads). Production surfaces do not preload a fixed fallback-face list. When a primary face lacks a character or supported emoji sequence, or when presentation preference rejects the primary, `FontFallback::Production` asks the shared `FontCache` for ordered Fontconfig candidates for that primary request, checks FreeType coverage, shapes the sequence when needed, and caches the decision together with the presentation preference. Deterministic tests inject fixed fixture faces instead. Missing coverage keeps the primary face so HarfBuzz can emit `.notdef` without crashing paint. Chrome and body primaries resolve fallback independently, so they do not share incorrectly sized fallback faces.

Face selection is presentation-aware. Each shaping unit carries `EmojiPresentation` (`Unspecified`, `Text`, or `Emoji`) from explicit `U+FE0E` / `U+FE0F` selectors, supported constructed sequences (modifiers, ZWJ, flags, keycaps), or the Unicode `Emoji_Presentation` property for an unqualified single character. For `Emoji`, colour-capable faces (`FontFace::HasColor`, FreeType `FT_HAS_COLOR`) are tried first among the primary, fixed fixtures, and production candidates. For `Text`, non-colour faces are tried first. If the preferred class has no usable covering face, selection retries the unrestricted order so a monochrome glyph is preferred over avoidable `.notdef` when no colour face is available, and a colour face may still serve explicit text presentation when it is the only covering face. Ordinary text stays `Unspecified` and keeps the historical primary-first walk.

## Text shaping

A `ShapedRun` stores the input UTF-8 bytes, HarfBuzz glyphs, per-byte end positions, valid caret stops, direction, and the font face used by each glyph. Measurement, wrapping, hit testing, selection, caret placement, and drawing consume the same cached run.

The per-byte positions satisfy Scintilla's `Surface::MeasureWidths` contract. Bytes in one UTF-8 character share its end position, and positions inside a merged shaping cluster are not caret stops. Invalid UTF-8 bytes follow the editor's byte-preserving policy.

Shaping walks the input as UTF-8 characters and groups supported multi-code-point emoji sequences before face selection: presentation selectors (`U+FE0E` / `U+FE0F`), skin-tone modifiers (`U+1F3FB`..`U+1F3FF`), ZWJ-linked emoji, regional-indicator flag pairs, and keycap sequences. One face shapes each whole sequence; default-ignorable joiners and selectors may be ignored in coverage checks, but the complete sequence is passed to HarfBuzz. Byte-end positions and caret stops expose only the boundaries of that unit, even when the font emits more than one glyph. This is not a complete UAX #29 grapheme-boundary implementation: document cursor movement, deletion, selection expansion, and `SafeSegment` still use the broader unfinished grapheme rules and may not treat every emoji sequence as an editing atom.

HarfBuzz chooses script from the span contents. Direction stays left-to-right, discretionary ligatures (`liga`, `dlig`) stay off, and English remains the language when HarfBuzz leaves it unset. Other scripts and mixed-direction line ordering require extending this one shaped-run model rather than introducing a parallel layout path.

## Grayscale raster policy

Each loaded `FontFace` keeps a `FontRasterPolicy` that selects FreeType load flags for both HarfBuzz shaping and `RasterizeGlyph`. Production faces extract the policy from the prepared Fontconfig match: `FC_ANTIALIAS`, `FC_HINTING`, and `FC_HINT_STYLE`. Disabled hinting or `FC_HINT_NONE` maps to `FT_LOAD_NO_HINTING`; `FC_HINT_SLIGHT` maps to `FT_LOAD_TARGET_LIGHT`; medium, full, and unknown styles map to `FT_LOAD_TARGET_NORMAL`. Explicit-path loads (deterministic tests) default to normal hinting unless the test injects another policy. The face-cache key includes the policy so light and normal faces for the same file do not share an `FT_Face`. Host Fontconfig often selects slight (light) hinting for body faces such as Roboto; that policy is expected input, not a defect.

Load flags always include `FT_LOAD_COLOR` so CBDT/CBLC colour bitmaps remain available. Outlines are always rendered with `FT_RENDER_MODE_NORMAL` (8-bit gray coverage). FreeType's light hinting algorithm is selected by the load target, not by `FT_RENDER_MODE_LIGHT`, which produces the same coverage format. When Fontconfig reports `FC_ANTIALIAS=false`, the field is retained for identity but rasterization still uses gray coverage; packed monochrome output is not supported.

RGB, BGR, and vertical LCD component coverage are not consumed yet. Fontconfig may still report subpixel ordering and an LCD filter on the match; those properties are future input only. Correct LCD drawing needs per-channel destination attenuation and output-aware ordering, which the current single-alpha source-over path cannot do.

## Device-phase outline rasterization

Shaping, measurement, wrapping, hit testing, selections, and caret positions stay in logical coordinates on the logical-size HarfBuzz face. Outline painting uses a separate path:

1. The nominal output scale is an exact rational `RasterScale` (Wayland preferred scale numerator over 120, stored in lowest terms). It is not `bufferWidth / logicalWidth`.
2. `Renderer::DrawGlyph` converts each logical baseline origin through that scale, splits each axis with floor into an integer buffer origin and a normalized 26.6 phase in y-down device space, and builds a `GlyphRasterRequest`.
3. `FontFace::RasterizeGlyph` opens an independent FreeType face at the device ppem (keyed by file, Fontconfig face index, and height) so the HarfBuzz face is never resized or transformed. Phase is applied with `FT_Set_Transform` (identity matrix + 26.6 delta; vertical component negated into FreeType's y-up space) after hinting, then restored.
4. The returned gray mask has device-pixel width, height, and bearings. The renderer places it on integer buffer pixels with a buffer-coordinate textured quad and `GL_NEAREST`, so OpenGL copies coverage one-to-one rather than resampling a logical-resolution mask.

Texture-cache identity for outline glyphs includes face, glyph id, raster scale, and phase. Identical requests reuse an entry. A real output-scale change calls `SetOutputRasterScale` and retires all scale-dependent outline entries immediately. Fixed bitmap textures follow a separate three-generation bound described under colour emoji. Ordinary logical or buffer resizes at the same nominal scale do not retire entries. Deterministic renderer tests assemble expected coverage from the same phase-aware glyph images and compare destination pixels for phrases such as `high standards` and `honesty` at scales 1, 5/4, 3/2, and 2. Stability of repeated draws alone is not treated as proof of correct light-hinted placement.

## Colour emoji and fixed bitmap glyphs

CBDT/CBLC colour fonts (and other fixed bitmap strikes) cannot use `FT_Set_Char_Size`. The face loader selects the closest fixed strike and stores `metricsScale = requestedSize / strikePpem`. HarfBuzz advances, face metrics, and glyph bearings are converted into that logical size so measurement and drawing stay aligned.

Fixed bitmap and colour glyphs are not passed through outline phase transforms. Strike selection stays on the logical-size face. Rasterization returns the full fixed strike as gray coverage or premultiplied RGBA (`FT_PIXEL_MODE_BGRA` is converted from FreeType's premultiplied BGRA; pitch may be negative). Unsupported pixel modes yield an empty image. Layout, bearings, and the destination rectangle remain logical: the renderer multiplies the strike bitmap's integer extents and bearings by `metricsScale` so placement matches shaping, independent of the uploaded texture size.

When the uniform physical shrink ratio `metricsScale * RasterScale` is below one, the renderer area-reduces the strike on the CPU before upload. Each texture axis is `ceil(sourceExtent * reductionFactor)`, clamped into `[1, sourceExtent]`, so the texture is never smaller than one texel and never larger than the strike. Reduction is a separable box filter that keeps premultiplied RGBA channels premultiplied (no straight-alpha conversion) and gray coverage as one channel before the white-plus-alpha upload format. The implementation never CPU-upscales: when the ratio is at least one, the full strike is uploaded once and shared.

Drawing still maps that texture across the exact logical destination rectangle, so OpenGL `GL_LINEAR` only smooths the residual sub-texel adjustment rather than minifying a large strike. Gray outline glyphs continue to use `GL_NEAREST` and the device-phase path above. Colour glyphs skip RGB foreground tint and keep overall text-alpha modulation and premultiplied blending.

Cache identity for fixed bitmaps is face, glyph id, and either the active shrinking `RasterScale` or one shared full-strike generation when no reduction is needed. The renderer retains at most three recently used shrinking scale generations; a fourth distinct shrink scale deletes the least-recently-used generation as a group (textures deleted with the renderer context current). Repeated draws at one scale do not resample or re-upload after the first fill. Non-shrinking scales share one source-size texture so magnification does not invent detail or duplicate full-strike entries.

Representative diagnostic for the deterministic 16 px colour emoji fixture (source strike 136×128 = 17408 texels at 109 ppem, `metricsScale ≈ 16/109`). These numbers check memory and cache bounds only; wall-clock times are not test gates.

| Nominal scale | Reduction factor | Uploaded size | Texels | Share of full strike |
| --- | --- | --- | --- | --- |
| 1 | ≈ 0.147 | 20×19 | 380 | ≈ 2.2% |
| 5/4 | ≈ 0.183 | 25×24 | 600 | ≈ 3.4% |
| 3/2 | ≈ 0.220 | 30×29 | 870 | ≈ 5.0% |
| 2 | ≈ 0.294 | 40×38 | 1520 | ≈ 8.7% |

After cycling four distinct shrinking scales in order, the cache holds three fixed-bitmap entries (the least-recently-used generation is gone). First-fill CPU area reduction for these sizes is on the order of a millisecond per glyph on a typical development machine; exact wall-clock values vary and are not asserted.

COLRv1 paint graphs and SVG-in-font rendering are out of scope. FreeType does not provide general COLRv1 rendering; this path stops at colour formats FreeType can rasterize into a bitmap.

## Coordinates and pixels

Drawing accepts logical, top-left coordinates with half-open rectangles for fills, strokes, images, and gradients. The renderer maps that space onto a buffer-sized OpenGL viewport. Window buffers may have more pixels than the logical surface when integer or fractional scaling is active. OpenGL scissor rectangles stay in buffer pixels.

The active output scale is an exact rational `RasterScale`. `main` copies `WaylandScaleConfiguration::scaleNumerator` into `ApplicationEditor`. The frame path calls `Renderer::SetOutputRasterScale`; `SetDrawTarget` only selects the FBO and sizes so pixmap binds cannot thrash scale identity mid-paint. Outline text additionally uses buffer-pixel placement as described under device-phase outline rasterization. Both axes share the same nominal scale so buffer-dimension rounding does not create window-size-dependent font sizes.

Sibling pixmaps retain logical drawing dimensions but allocate their colour buffers at the active output scale. Copies and pattern fills map logical source coordinates across those scaled buffers, so buffered editor text uses the same device-pixel glyph placement as direct window drawing.

Colour attachments are linear `GL_RGBA8`. Internal alpha is premultiplied and blended with premultiplied source-over. Public offscreen pixel buffers are converted to straight alpha and returned in top-to-bottom order.

## Application composition

Scintilla paints the editor client. A permanent-chrome callback paints the menu bar, tab strip, scrollbars, and scrollbar junction without forcing full-frame damage. A post-paint overlay callback paints exactly one open menu, unsaved-changes card, or file-error card; transparent overlays expand painting to the full frame and use a full swap so preserved pixels cannot accumulate blending.

`ApplicationUi` owns the chrome models and painters, binds both application paint callbacks, selects the active overlay, and unbinds callbacks that capture it when it is destroyed. Individual controls own their layout, hit testing, interaction state, and paint operations without a widget hierarchy. The complete composition and input boundary is described in [application-ui.md](application-ui.md).

`ApplicationEditor` supplies its client rectangle and scrollbar metrics. `ApplicationUi::BeginFrameLayout` refreshes the model values that affect geometry and retains one `ApplicationLayout` snapshot containing the menu, tabs, optional find bar, scrollbars, client, and modal cards. Permanent-chrome and overlay painters read that same snapshot until `EndFrameLayout`.

Autocomplete lists and call tips use explicit production stubs. Their core behavior remains compiled and testable, but real Wayland popup surfaces for those features are not implemented. The application context menu paints into a separate grabbed `xdg_popup` EGL window surface that shares the editor GL context. Each renderer retains its selected EGL surface, and the popup renderer is destroyed before the context returns to the editor surface. The popup buffer scale and viewport match the toplevel so popup-local input and painted logical coordinates agree.

## Testing

Offscreen renderer tests use the production OpenGL path with controlled fixture fonts. Outline text checks include reference composition against phase-aware FreeType coverage (not merely repeated-draw stability). Glyph-cache tests cover outline scale and phase identity with immediate retirement on output-scale change, and fixed-bitmap physical texture sizes with three-generation shrink reuse and eviction. Colour emoji edge checks require soft coverage after opaque composition rather than reading source alpha from the blended target. Synthetic CPU reducer tests own exact mass and fringe expectations. Host Fontconfig checks remain non-pixel-exact. Editor tests paint through the same `DrawSurface` implementation while retaining host observations. Application-host scale tests own nominal `RasterScale` identity and outline retirement through `ApplicationEditor`; fixed-bitmap sizing is exercised on the renderer side of that handoff. Wayland frame and scale tests verify damage conversion and nominal raster-scale ownership separately from live compositor submission.
