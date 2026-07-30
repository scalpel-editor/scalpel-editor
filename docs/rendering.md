# Rendering

The editor uses one concrete rendering implementation for both the live Wayland window and deterministic offscreen tests. Fontconfig selects faces, FreeType loads and rasterizes glyphs, HarfBuzz shapes text, and OpenGL draws Scintilla content and application chrome through EGL contexts.

## Ownership

`GlContext` owns EGL and OpenGL context state. A window context draws to framebuffer 0 through the `wl_egl_window`; a headless context owns an offscreen target. `Renderer` owns drawing programs, glyph textures, and the active draw target. `DrawSurface` implements Scintilla's measurement and drawing surface contract over that renderer.

`ApplicationEditor` owns the production context, renderer, and frame surface. Its resource base is destroyed after `ScintillaBase` releases cached drawing objects, so OpenGL and font resources remain valid during editor teardown.

Pixmap surfaces and offscreen targets own texture-backed colour buffers. OpenGL objects are destroyed while their context is current.

## Text shaping

A `ShapedRun` stores the input UTF-8 bytes, HarfBuzz glyphs, per-byte end positions, valid caret stops, direction, and the font face used by each glyph. Measurement, wrapping, hit testing, selection, caret placement, and drawing consume the same cached run.

The per-byte positions satisfy Scintilla's `Surface::MeasureWidths` contract. Bytes in one UTF-8 character share its end position, and positions inside a merged shaping cluster are not caret stops. Invalid UTF-8 bytes follow the editor's byte-preserving policy.

The current shaper uses fixed Latin and English properties and supports left-to-right text only. Font fallback is selected per span. Discretionary ligatures are disabled so editor movement and display remain predictable. Other scripts and mixed-direction line ordering require extending this one shaped-run model rather than introducing a parallel layout path.

## Coordinates and pixels

Drawing accepts logical, top-left coordinates with half-open rectangles. The renderer maps that space onto a buffer-sized OpenGL viewport. Window buffers may have more pixels than the logical surface when integer or fractional scaling is active.

Colour attachments are linear `GL_RGBA8`. Internal alpha is premultiplied and blended with premultiplied source-over. Public offscreen pixel buffers are converted to straight alpha and returned in top-to-bottom order.

## Application composition

Scintilla paints the editor client. A permanent-chrome callback paints the menu bar, tab strip, scrollbars, and scrollbar junction without forcing full-frame damage. A post-paint overlay callback paints exactly one open menu, unsaved-changes card, or file-error card; transparent overlays expand painting to the full frame and use a full swap so preserved pixels cannot accumulate blending.

`ApplicationUi` owns the chrome models, painters, hover and press state, which overlay is selected, permanent-chrome and overlay composition, the current application pointer-cursor choice, and pointer and keyboard routing. It builds one `ApplicationLayout` snapshot (menu, tabs, scrollbars, client, cards) from frame size and editor metrics for each event or paint pass. `HandlePointer` resolves an explicit owner (file error, unsaved prompt, menu, scrollbar drag, editor capture, permanent chrome, or editor), applies application actions and editor invalidation, and returns consumption and cursor choice to the platform host. `CurrentPointerCursor` retains that choice and forces the arrow when a modal appears without a pointer event. `HandleKeyboard` routes modal cards, menu navigation, application shortcuts, tab cycling, and editor delivery with an explicit owner. Pointer, keyboard, close-request, dialog-result, and shell-effect entry points finish any scrollbar cleanup caused by a document or modal transition before returning. `HandleFocus` is one transition for focus loss (editor focus cancel including IME, menu close, scrollbar cancel, press clear). `ChromeOwnsInput` tells the host when to drop IME batches; protocol conversion stays in the platform adapter. `BindPainters` installs the permanent-chrome and overlay callbacks on `ApplicationEditor`, and destruction unbinds callbacks that capture the UI. `SynchronizeComposition` selects the active overlay by priority (file error, unsaved prompt, open menu, none), binds or clears the overlay painter, and invalidates the full frame when the overlay appears, changes, or disappears. `TakeShellEffects` consumes workspace requests and outcomes, applying prompt begin, tab refresh, recent-file record and persist, and file-error queueing directly; it returns only typed portal-dialog and accept-close effects for the host. Dialog effects carry an application identity and copied document path. Portal success, cancellation, and startup failure feed back through named methods using that identity, while the portal request ID stays in `main`. `RequestClose`, `HandleFrameSizeChange`, `SynchronizeDirtyTabs`, `RefreshOpenMenuActionState`, and `PrepareForExit` keep close, scrollbar, tab, menu, and exit transitions out of the platform pump. `BeginFrameLayout` finalizes menu enablement and tab-strip scroll, then retains one snapshot that both paint entry points read. Individual controls own their layout, hit testing, interaction state, and paint operations without a widget hierarchy.

Autocomplete lists, call tips, and the Scintilla context menu use explicit production stubs. Their core behavior remains compiled and testable, but real Wayland popup surfaces are not implemented.

## Testing

Offscreen renderer tests use the production OpenGL path with controlled fonts and compare exact geometry and pixels where stable. Editor tests paint through the same `DrawSurface` implementation while retaining host observations. Wayland frame and scale tests verify damage conversion separately from live compositor submission.
