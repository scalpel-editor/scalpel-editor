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

Each loaded `FontFace` keeps a `FontRasterPolicy` that selects FreeType load flags for both HarfBuzz shaping and `RasterizeGlyph`. Production faces extract the policy from the prepared Fontconfig match: `FC_ANTIALIAS`, `FC_HINTING`, and `FC_HINT_STYLE`. Disabled hinting or `FC_HINT_NONE` maps to `FT_LOAD_NO_HINTING`; `FC_HINT_SLIGHT` maps to `FT_LOAD_TARGET_LIGHT`; medium, full, and unknown styles map to `FT_LOAD_TARGET_NORMAL`. Explicit-path loads (deterministic tests) default to normal hinting unless the test injects another policy. The face-cache key includes the policy so light and normal faces for the same file do not share an `FT_Face`.

Load flags always include `FT_LOAD_COLOR` so CBDT/CBLC colour bitmaps remain available. Outlines are always rendered with `FT_RENDER_MODE_NORMAL` (8-bit gray coverage). FreeType's light hinting algorithm is selected by the load target, not by `FT_RENDER_MODE_LIGHT`, which produces the same coverage format. When Fontconfig reports `FC_ANTIALIAS=false`, the field is retained for identity but rasterization still uses gray coverage; packed monochrome output is not supported.

RGB, BGR, and vertical LCD component coverage are not consumed yet. Fontconfig may still report subpixel ordering and an LCD filter on the match; those properties are future input only. Correct LCD drawing needs per-channel destination attenuation and output-aware ordering, which the current single-alpha source-over path cannot do.

## Colour emoji glyphs

CBDT/CBLC colour fonts (and other fixed bitmap strikes) cannot use `FT_Set_Char_Size`. The face loader selects the closest fixed strike and stores `metricsScale = requestedSize / strikePpem`. HarfBuzz advances, face metrics, and glyph bearings are converted into that logical size so measurement and drawing stay aligned.

`FontFace::RasterizeGlyph` loads with the face's shared FreeType load flags (`FT_LOAD_COLOR` plus the grayscale hint target). Gray coverage glyphs stay 8-bit masks and are tinted with the text foreground. `FT_PIXEL_MODE_BGRA` bitmaps are converted from FreeType's premultiplied BGRA into premultiplied RGBA (pitch may be negative). Unsupported pixel modes yield an empty image. The renderer caches colour glyphs as premultiplied textures, draws their RGB without foreground tint while applying overall text alpha, and uses linear filtering when a large colour strike is downscaled. Ordinary gray glyphs keep nearest filtering so existing pixel tests stay stable.

COLRv1 paint graphs and SVG-in-font rendering are out of scope. FreeType does not provide general COLRv1 rendering; this path stops at colour formats FreeType can rasterize into a bitmap.

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
