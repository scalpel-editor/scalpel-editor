# Phase 6 platform boundary (frozen 2026-07-16)

Recorded before the concrete FreeType / HarfBuzz / Fontconfig platform, GL renderer, and Wayland shell exist. Later steps implement against this classification rather than re-deriving ownership from call sites. [ROADMAP.md](../ROADMAP.md) phase 6 lists the ordered steps; this document freezes **which `Platform.h` and editor-host operations each step owns**, what remains a popup stub, and what is deferred to phase 7.

There is no automated completion check for this freeze (unlike `tools/check-no-message-layer.sh` for phase 5). Step 1 is complete when the inventory below is accurate for the tree at freeze time and step 1 is marked done on the roadmap.

Owners used in the tables:

| Owner | Meaning |
| --- | --- |
| **Renderer** | Fonts, surface measure and draw, shaped-run layout, pixmaps, clip, geometry and text primitives. Implemented in steps 2–6; collapsed onto one concrete path in step 7. |
| **Minimal shell** | Main window identity and geometry, invalidation, show/destroy, cursor, client and monitor rects; application host hooks for scrollbars, capture, tickers, idle, size, focus, and notifications; Wayland/EGL loop and basic input in steps 8–11. |
| **Popup stub** | `ListBox`, `Menu`, and call-tip window. Core code stays compiled; the platform records the request or fails loudly and never reports success. Real popup windows are follow-on work after this roadmap. |
| **Phase 7** | Compose, key repeat, IME, clipboard and primary selection transfers, cursor themes, frame pacing, presentation feedback, optional-protocol fallback, scale and buffer-scale, robust global and seat removal, hot-plugged seats. |
| **Debug only** | Assert and debug-print helpers used by the core; not part of the user-visible editor surface. |

Until step 7 replaces it, `scintilla/test/editor/TestPlatform.cxx` remains the only implementation of `Platform.h` symbols linked by `editorTest`. Production symbols arrive with the concrete platform; step 7 swaps the editor fixture's recorded drawing surface for the renderer's offscreen target while keeping host observation.

## Opaque IDs (`Platform.h`)

| Type | Role today | Owner | Notes |
| --- | --- | --- | --- |
| `SurfaceID` | Handle passed to `Surface::Init` for drawing and to `FormatRange` | **Renderer** (step 7 may rename or drop when collapsing) | Opaque `void *`. Printing and paint pass it through `CreateDrawingSurface`. |
| `WindowID` | Handle behind `Window` and `Surface::Init` measure path | **Minimal shell** | Main editor window, margin pixmap window, call-tip and list-box windows when those exist. |
| `MenuID` | Handle behind `Menu` | **Popup stub** | No real menu in phase 6. |
| `TickerID` | Declared; fine tickers use virtuals on `Editor`, not this typedef | **Minimal shell** | Host implements `FineTickerStart` / `Cancel` / `Running` (step 8 / 10). |
| `IdlerID` | Declared; idle uses `SetIdle` virtual on `Editor` | **Minimal shell** | Step 8 / 10. |
| `Function` | Declared; unused by current core call sites inventoried here | **Minimal shell** or drop in step 7 | Revisit when collapsing the platform layer. |

## Multi-platform macros (`Platform.h`)

The `PLAT_GTK` / `PLAT_WIN` / … block and the default-to-Win32 `#else` branch are multi-platform leftovers. **Renderer / shell do not use them.** Step 7 deletes or replaces them when collapsing `Platform.h` onto one implementation. Until then production code must not introduce new `#if PLAT_*` branches.

## Font

| Operation | Used by | Owner | Step |
| --- | --- | --- | --- |
| `FontParameters` fields (`faceName`, `size`, `weight`, `italic`, `extraFontFlag`, `technology`, `characterSet`, `localeName`, `stretch`) | `ViewStyle::FontRealised::Realise` | **Renderer** | 2 (lookup / ownership), 3–4 (shape/measure); `technology` removed in step 7; `characterSet` is legacy Scintilla field (UTF-8-only project — ignore for face selection beyond face name / style) |
| `Font::Allocate` | `ViewStyle` when realising style fonts | **Renderer** | 2 |
| `Font` lifetime (`shared_ptr`, non-copyable base) | Styles hold shared fonts | **Renderer** | 2 |

## Surface lifecycle and device metrics

| Operation | Used by | Owner | Step |
| --- | --- | --- | --- |
| `Surface::Allocate(Technology)` | `Editor::CreateMeasurementSurface` / `CreateDrawingSurface` | **Renderer** | 5–7; technology argument removed in step 7 |
| `Init(WindowID)` | Measurement surface | **Renderer** | 5 |
| `Init(SurfaceID, WindowID)` | Drawing surface (window or print target) | **Renderer** | 5; print path step 7 |
| `AllocatePixMap` | Margin caching, buffered draw | **Renderer** | 5 |
| `SetMode` | Bidi flag on surface (LTR goal; mode retained for later) | **Renderer** | 5; mixed-direction ordering still out of scope |
| `Release` / `Initialised` | Surface teardown and readiness | **Renderer** | 5 |
| `SupportsFeature` | `LineDrawsFinal`, `ThreadSafeMeasureWidths`, and related | **Renderer** | 5; report honest capabilities |
| `LogPixelsY` / `PixelDivisions` / `DeviceHeightFont` | Font realise and drawing scale | **Renderer** | 2, 5 |

## Surface geometry drawing

All of the following are **Renderer**, steps 5–6 (primitives in 5, used heavily when painting the editor in 6–8). Core call sites include `EditView`, `MarginView`, `LineMarker`, `Indicator`, `CallTip`, and related paint paths.

| Operation | Notes |
| --- | --- |
| `LineDraw` | Carets, guides, edges |
| `PolyLine` | Markers, fold graphics |
| `Polygon` | Markers, arrows |
| `RectangleDraw` / `RectangleFrame` | Frames, carets, chrome |
| `FillRectangle` (solid) | Backgrounds, selections, margins (most frequent paint call) |
| `FillRectangleAligned` | Pixel-aligned fills |
| `FillRectangle` (pattern from another surface) | Patterned fills |
| `RoundedRectangle` | Markers |
| `AlphaRectangle` | Translucent rects |
| `GradientRectangle` | Gradients |
| `DrawRGBAImage` | RGBA markers / images |
| `Ellipse` | Markers |
| `Stadium` | Rounded ends (e.g. indicators) |
| `Copy` | Pixmap blit |

## Surface text measure and draw

| Operation | Used by | Owner | Step |
| --- | --- | --- | --- |
| `MeasureWidths` | `PositionCache`, `ViewStyle` (ASCII table) | **Renderer** via shaped runs | 3–4 |
| `WidthText` | Layout, fold text, call tips, markers, line numbers | **Renderer** via shaped runs | 3–4 |
| `Ascent` / `Descent` / `InternalLeading` / `Height` / `AverageCharWidth` | Font realise, call tips, autocomplete sizing | **Renderer** | 2, 4 |
| `Layout` → `IScreenLineLayout` | `EditView` for position-from-x, x-from-position, selection intervals when bidirectional or screen-line layout path is active | **Renderer** | 4; English LTR shaping; no other scripts or mixed-direction line ordering |
| `IScreenLine` (text, fonts, tabs, representations) | Fed into `Layout` by core | **Renderer** consumes; core supplies | 4 |
| `DrawTextNoClip` / `DrawTextClipped` / `DrawTextTransparent` | Text, control chars, call tips, line numbers | **Renderer** | 6 (must use shaped advances, not re-measure) |
| `SetClip` / `PopClip` | Nested paint clips | **Renderer** | 5 |
| `FlushCachedState` / `FlushDrawing` | End of paint batches | **Renderer** | 5 |

## Window

Non-owning handle wrapper. Production main window is created by the shell; the core assigns `wMain` / `wMargin` and calls methods below.

| Operation | Used by | Owner | Step |
| --- | --- | --- | --- |
| `operator=(WindowID)` / `GetID` / `Created` | Editor setup, surface init | **Minimal shell** | 8–9 |
| `Destroy` | Call tip, list box, finalise | **Minimal shell** for main; **Popup stub** for tip/list | 7 stubs; 8–9 main |
| `GetPosition` / `SetPosition` / `SetPositionRelative` | Call tip and autocomplete placement relative to `wMain` | **Popup stub** for those windows; main geometry **Minimal shell** | 7 stubs; 8–10 main |
| `GetClientPosition` | `Editor::GetClientRectangle`, call-tip paint | **Minimal shell** | 8–10 |
| `Show` | Call tip, list box | **Popup stub** | 7 |
| `InvalidateAll` / `InvalidateRectangle` | Redraw paths (`wMain`, `wMargin`, call tip) | **Minimal shell** (main/margin); **Popup stub** (call tip) | 7–10 |
| `SetCursor` | `Editor::DisplayCursor` | **Minimal shell** | 8; cursor **themes** are phase 7 — phase 6 may set a basic cursor or no-op with a recorded choice |
| `GetMonitorRect` | Autocomplete clamp to monitor | **Minimal shell** or **Popup stub** when list is stubbed | Stub path until real popups; shell can return client or display bounds |

`Window::Cursor` values used by the core: `text`, `arrow`, `hand`, `reverseArrow`, and the invalid sentinel. Mapping to Wayland cursor surfaces is phase 7; step 11 only needs pointer coordinates, not themed cursors.

## ListBox

Allocated by `AutoComplete` via `ListBox::Allocate`. Full virtual surface is used by `AutoComplete.cxx` and `EditorAutocomplete.cxx` (create, font, list content, selection, images, show/destroy, position).

| Operation group | Owner | Step |
| --- | --- | --- |
| `ListBox::Allocate` and all virtuals (`SetFont`, `Create`, `SetAverageCharWidth`, `SetVisibleRows`, `GetVisibleRows`, `GetDesiredRect`, `CaretFromEdge`, `Clear`, `Append`, `Length`, `Select`, `GetSelection`, `Find`, `GetValue`, image registration, `SetDelegate`, `SetList`, `SetOptions`) plus inherited `Window` methods | **Popup stub** | 7 (recorded-failure stubs); real windows are post-roadmap |
| `IListBoxDelegate` / `ListBoxEvent` | Core (`ScintillaBase`) | Unchanged; stubs never deliver success events unless a test injects them |

## Menu

| Operation | Used by | Owner | Step |
| --- | --- | --- | --- |
| `Menu` / `CreatePopUp` / `Destroy` / `Show` | `ScintillaBase::ContextMenu` | **Popup stub** | 7 |
| `ScintillaBase::AddToPopUp` (pure virtual host) | Context menu item population | **Popup stub** / host records items | 7–8 |

## Platform namespace (system parameters)

Declared in `Platform.h` (not `Debugging.h`):

| Operation | Used by | Owner | Step |
| --- | --- | --- | --- |
| `Chrome` / `ChromeHighlight` | Default style and selection-bar colours in `ViewStyle` | **Minimal shell** (or fixed theme constants on the renderer side) | 8; can be fixed colours for the vertical slice |
| `DefaultFont` / `DefaultFontSize` | `ViewStyle` default style, `Style` construction | **Renderer** / fixtures | 2 (tests use explicit paths; production may use Fontconfig + these names) |
| `DoubleClickTime` | Double-click detection in input handling | **Minimal shell** | 11 (or fixed test value) |
| `LongFromTwoShorts` | Header-only helper | Keep as utility | — |

## Debugging namespace (`Debugging.h`)

Implemented beside the platform layer today (`TestPlatform.cxx`).

| Operation | Owner | Notes |
| --- | --- | --- |
| `DebugDisplay` / `DebugPrintf` | **Debug only** | Optional stderr; not required for correctness |
| `Assert` / `ShowAssertionPopUps` / `PLATFORM_ASSERT` | **Debug only** | Abort or log; no GUI pop-ups in this project |

## Editor and ScintillaBase host hooks (not on `Platform.h`)

These pure or virtual methods are how the core talks to the application host. Step 8's production `ScintillaBase` subclass owns the vertical-slice set; the rest are unsupported (visible failure or no-op with observation) or phase 7.

### Required for the phase 6 vertical slice (minimal shell / host)

| Hook | Role | Step |
| --- | --- | --- |
| `Initialise` / `Finalise` | Host lifetime | 8 |
| `GetClientRectangle` / client size | Layout and paint bounds | 8–10 |
| `SetHorizontalScrollPos` / `SetVerticalScrollPos` / `ModifyScrollBars` / `ReconfigureScrollBars` | Scrollbar policy | 8; chrome scrollbars are phase 8 product UI — host may track values without drawing chrome |
| `SetMouseCapture` / `HaveMouseCapture` | Pointer capture during drag | 8, 11 |
| `FineTickerStart` / `Cancel` / `Running` / `TickFor` | Caret blink, dwell, scroll, wrap widen | 8, 10 |
| `SetIdle` / `IdleWork` / `QueueIdleWork` | Idle styling and deferred work | 8, 10 |
| `NotifyChange` / `NotifyParent` / `NotifyFocus` | Application notifications | 8 |
| `CreateMeasurementSurface` / `CreateDrawingSurface` | Defaults allocate `Surface`; override only if needed | 5–8 |
| `DisplayCursor` (default uses `wMain.SetCursor`) | Cursor shape | 8; themes phase 7 |
| `Paint` path driven by shell after invalidate | Redraw | 8–10 |
| Key → `InsertCharacter` / `EditorCommand` / `KeyDown` path | Keyboard | 11 |
| Pointer → `ButtonDownWithModifiers`, move, up, wheel | Pointer | 11 |

### Popup-related host hooks (stubs)

| Hook | Role | Step |
| --- | --- | --- |
| `CreateCallTipWindow` | Assign `ct.wCallTip` | **Popup stub** (step 7–8): record rectangle; do not fake a working tip window |
| `AddToPopUp` | Context menu entries | **Popup stub** |

### Deferred or unsupported in the vertical slice

| Hook | Disposition |
| --- | --- |
| `Copy` / `Paste` / `CopyToClipboard` / `ClaimSelection` / `CanPaste` | **Phase 7** for real transfers; step 8 keeps requests **visible as unsupported** (do not report success). Core `Cut`/`Copy`/`Paste` commands may still run editor-side selection logic where they do not need the host. |
| IME helpers (`MoveImeCarets`, `DrawImeIndicator`, `SetIMEInteraction`) | Core retained; **phase 7** text-input-v3 drives them. Step 11 does not implement IME. |
| `StartDrag` / full drag-drop | Out of vertical slice; leave default or unsupported. |
| `UpdateSystemCaret` / `NotifyCaretMove` | Optional; phase 7 or later if a system caret is wanted. |
| `CaseMapString` | Default UTF-8 case mapping in core is enough for the slice. |

## Technology and surface selection (collapse target)

| Item | Disposition |
| --- | --- |
| `Scintilla::Technology` on `Editor`, `FontParameters`, `Surface::Allocate`, `ListBox::Create`, `CreateDrawingSurface` | **Step 7**: remove renderer-selection arguments and multi-backend allocation; one surface and font implementation. |
| `SurfaceID` as opaque void pointer | **Step 7**: may become a typed renderer target or disappear from public print API shape as the print path is updated. |
| `TestPlatform` recorded draw log | **Step 7**: replace drawing assertions with offscreen pixel/readback checks; retain host observation (`TestEditor` notifications, tickers, capture, scrollbars). |

## Phase 7 cut line (do not implement in phase 6)

The following are **out of phase 6** even if seed code or protocols already exist for some of them:

- xkbcommon **compose** and **key repeat**
- **text-input-v3** IME (pre-edit and commit)
- **Clipboard** and **primary selection** data transfers
- **Cursor themes** (wayland-cursor)
- **`wl_surface.frame`** pacing and **presentation-time** feedback
- **Optional-protocol fallback** behavior when a bind fails (beyond failing clearly at startup for required phase 6 objects)
- **Fractional scaling**, **viewporter**, output and **buffer-scale** changes
- Robust **global and seat removal**, **hot-plugged** seats
- **xdg-decoration**, **xdg-foreign**, **portal** / D-Bus (phase 7–8)

Phase 6 step 11 only: build the **current** keymap from the seat, translate press/release into text insertion or `EditorCommand`, and route pointer motion, buttons, wheel, capture, and coordinates.

## Dependencies

Libraries discovered with pkg-config (versions below are what this development host had at freeze time; CMake should require the modules, not pin these exact numbers unless a known minimum is found later).

| Module / tool | Role in phase 6 | First step | Not for phase 6 |
| --- | --- | --- | --- |
| `freetype2` | Face open, metrics, glyph rasterization | 2, 6 | — |
| `harfbuzz` | Shape English LTR runs with fixed Latin and English properties; discretionary ligatures off | 3 | Other scripts and mixed-direction line ordering |
| `fontconfig` | Production family lookup and fallback | 2 | — |
| `wayland-client` | Display connection, registry, compositor, seat, surface | 9–11 | — |
| `wayland-protocols` + `wayland-scanner` | Generate **xdg-shell** client code only | 9 | decoration, presentation-time, foreign, viewporter, text-input (later phases) |
| `wayland-egl` | `wl_egl_window` for the EGL window surface | 9 | — |
| `egl` | Config, context, window surface, swap | 9–10 | — |
| OpenGL (`gl` on this host; seed renderer is GL3-style) | One drawing implementation for window and offscreen targets | 5–6 | Product abstraction / multi-backend |
| `xkbcommon` | Map seat keymap; key press/release → text or `EditorCommand` | 11 | Compose, repeat (phase 7) |

### Explicitly not required until later phases

| Module | Deferred to |
| --- | --- |
| `wayland-cursor` | Phase 7 (cursor themes) |
| `dbus-1` / portal helpers | Phase 7–8 (file dialogs and related watches) |
| xdg-decoration protocol | Phase 7 |
| presentation-time protocol | Phase 7 |
| xdg-foreign protocol | Phase 7–8 (parent handle for portals) |

Seed CMake (`seed/cmake/DependenciesForBackends.cmake`) still finds decoration, presentation-time, foreign, and dbus for the old OnlyWayUi backend. Phase 6 production CMake must **not** copy that whole list; generate and link only what the steps above need.

### Protocol generation

- **Required in phase 6:** `xdg-shell` stable XML from the wayland-protocols package data directory → client header and code via `wayland-scanner`.
- **Handshake:** bind compositor and seat; complete initial xdg-surface configure; create xdg-toplevel and `wl_egl_window` (step 9).
- **Cleanup:** destroy EGL and Wayland objects in reverse order of creation (step 9).
- **Not generated in phase 6:** xdg-decoration, presentation-time, xdg-foreign, text-input, primary-selection, viewporter, fractional-scale.

## Source references (mine, do not build on)

These paths teach techniques. Useful logic is rewritten as direct code under this project's layout; seed files are deleted only when step 12 (or phase 7) says so.

| Path | What to take | Absorbed by | Delete when |
| --- | --- | --- | --- |
| `seed/editor/PlatOWUI.cxx` / `.h` | LTR surface wiring over a foreign font engine; **not** HarfBuzz screen-line layout (unimplemented there) | Steps 2–7 | Step 12 |
| `seed/backends/OnlyWayUi_Renderer_GL3.cpp` / `.h` | GL setup, transforms, clipping, geometry, textures, layers — techniques only, not RmlUi interfaces | Steps 5–6 | Step 12 |
| `seed/backends/OnlyWayUi_Platform_Wayland.cpp` / `.h` | Connection, xdg-shell, seat, EGL window patterns; frame and presentation patterns for **phase 7** | Steps 9–11 (subset) | After phase 7 absorbs the rest |
| `seed/backends/OnlyWayUi_Backend*` / `OnlyWayUi_Include_GL3.h` | How the sample ties platform and renderer | Reference | With backends when unused |
| `seed/sample/` | Shape of a text-editor main loop over a hosted Scintilla | Steps 8–11 | When no longer needed as reference |
| `seed/cmake/DependenciesForBackends.cmake` | Example pkg-config and scanner wiring (trim to phase 6 set) | CMake in early code steps | Keep or replace when production CMake exists |
| OnlyWayUi `Samples/basic/harfbuzz/` (external tree; see [ORIGINS.md](../ORIGINS.md)) | FreeType + HarfBuzz integration ideas | Step 3 | External; not vendored |
| `scintilla/test/editor/TestPlatform.*` | Contract completeness and host-observation patterns | Steps 7–8 | Remains as test host; drawing side replaced in step 7 |

`ORIGINS.md` records that PlatOWUI does not provide per-input-byte measurements from shaped clusters. Phase 6 must build that mapping (step 3) and use it for measure, caret, selection, and draw (steps 4–6).

## Licenses

| Material | License location | Rule |
| --- | --- | --- |
| Scintilla core under `scintilla/` | `scintilla/License.txt` | Unchanged; core edits stay under that grant |
| Seed and any code derived from OnlyWayUi / RmlUi | `seed/LICENSE.txt` (MIT; CodePoint / Shift / RmlUi Team notices) | Keep the copyright and permission notice with every derived file for as long as derived code remains |
| Checked-in test fonts (step 2) | Notices checked in beside the font files | Compatibly licensed **primary and fallback** faces; system font packages must not be required for deterministic tests |
| FreeType, HarfBuzz, Fontconfig, Wayland, EGL, OpenGL, xkbcommon | System package licenses | Link only; do not vendor unless a later decision says so |

## Completion of step 1

Step 1 is done when:

1. Every live `Platform.h` operation and the editor host hooks listed above have an owner (renderer, minimal shell, popup stub, phase 7, or debug-only).
2. The phase 7 cut line is explicit so steps 9–11 do not re-litigate compose, IME, clipboard, themes, pacing, or scale.
3. Dependencies, protocol generation, seed sources, and license rules are recorded for steps 2 onward.

Implementation (trees, CMake targets, fonts, shaping code) starts at step 2.
