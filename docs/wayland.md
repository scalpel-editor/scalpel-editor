# Wayland shell

The application uses one direct Wayland shell rather than a window toolkit or general protocol-wrapper library. `WaylandWindow` owns the display connection, registry, surfaces, xdg-shell objects, active input devices, EGL native window, cursor resources, selection protocols, text input, frame callbacks, presentation feedback, scale objects, optional D-Bus connection, file dialogs, and portal parent export.

Thin Wayland callbacks copy protocol events into state objects grouped by concern. Those state objects own no proxies and can be driven deterministically in tests. `WaylandWindow` applies their requested lifecycle actions and exposes copied input, resize, transfer, text-input, dialog, scale, and presentation results to the application loop.

## Application boundary

`ApplicationEditor` owns Scintilla state, drawing, invalidation, editor deadlines, clipboard values, surrounding text, cursor rectangles, and tentative IME edits. `DocumentWorkspace` owns tabs, file paths, open and save policy, stable application file-dialog intents, and dirty-close transitions.

`WaylandApplicationRunner` is the platform adapter and event pump: construction, Wayland event collection, external-service transport, frame submission, and waiting. `ApplicationUi` is the Wayland-free application coordinator. It owns composition and input priority, consumes workspace requests and outcomes, and returns only typed portal-dialog and accepted-close effects to the host. The runner maps portal request IDs to application dialog identities, feeds copied results back through `ApplicationUi`, copies the active-tab window title onto `xdg_toplevel`, submits editor damage, waits for the next external event or application deadline, and returns a typed session termination reason. `main.cxx` only parses the invocation, reports help, version, or usage errors, invokes the runner, and maps the termination reason to a process status.

The full ownership, event flow, interaction priority, layout authority, host-effect boundary, and application UI non-goals are described in [application-ui.md](application-ui.md). Wayland transport does not decide document-close or presentation policy.

## Global and service behavior

Only `wl_compositor` and `xdg_wm_base` are required to create and keep the main surface. Losing either forces shutdown. Seats, outputs, and optional services may appear or disappear while the application remains open.

| Global or service | Current behavior |
| --- | --- |
| `wl_seat` | One stable active seat supplies keyboard and pointer devices. A remaining seat is promoted after removal. Missing devices leave the editor usable without that input path. |
| `wl_output` | Every announced output and the surface's entered set are tracked for scale selection. No output is required at startup. |
| `wl_shm` | Enables themed cursor buffers. Missing shared memory or cursor-theme failure falls back without disabling the editor. |
| `wl_data_device_manager` | Enables ordinary clipboard ownership and paste. Unavailability is reported without changing the document. |
| `zwp_primary_selection_device_manager_v1` | Enables primary selection and middle-button paste independently of the ordinary clipboard. |
| `zwp_text_input_manager_v3` | Enables compositor IME pre-edit, commit, and surrounding-text operations. Direct keyboard input remains available without it. |
| `zxdg_decoration_manager_v1` | Requests server-side decoration when available. Otherwise xdg-shell decoration behavior is accepted. |
| `wp_presentation` | Adds presentation timestamps and discard reports. Frame pacing continues with `wl_surface.frame` alone. |
| `wp_viewporter` and `wp_fractional_scale_manager_v1` | Enable fractional scaling only when both are present. Integer buffer scaling is the fallback. |
| `zxdg_exporter_v2` | Supplies a parent token for desktop-portal dialogs. Dialogs may be requested without a parent when it is absent. |
| Session D-Bus and the desktop portal | Connected lazily for open and save dialogs. Failure leaves an explicit unavailable result. |

The shell never binds above both the advertised protocol version and the newest request or event it handles.

## Object and result lifetimes

Protocol objects are destroyed from child to parent. Removing a seat or manager clears offers, serials, queued device events, composition, transfers, and other state that cannot remain valid.

Asynchronous operations do not retain an editor pointer:

- Clipboard paste results carry a request identity and apply only to the current document generation. Empty completed text is reported as no text; text that cannot land on a read-only or protected selection is reported as not applied.
- Primary-selection paste retains its requested document position and applies only while the document revision remains current (any text change or document switch).
- Clipboard and primary ownership are reported as published, cancelled, unavailable, or create failure. Peer source reads use the shared write transfer path but do not produce Copy or Publish completion results and do not end local ownership tracking.
- Text-input state uses protocol commit serials and publishes copied batches only at `done`.
- File-dialog results carry a stable portal request ID. The Wayland runner maps it to the application dialog identity that retains the original open or save intent, including the initiating tab.
- Portal parent handles are accepted only from the current xdg-foreign export.
- Teardown closes each pending portal `Request` object so open dialogs do not outlive the process. Close does not invent an accepted result.

Cancellation, unavailable services, invalid MIME data, invalid UTF-8, I/O failure, size limits, timeouts, and superseded requests are observable results.

## Event loop

The application is single-threaded. One poll snapshot combines:

- the Wayland display descriptor;
- active clipboard and primary-selection transfer descriptors;
- enabled D-Bus watches;
- editor ticker and idle deadlines;
- compositor-configured key-repeat deadlines;
- transfer deadlines; and
- D-Bus timeouts.

Each descriptor retains the callback for its concern. The timeout is the earliest eligible deadline. A blocked Wayland flush is recovered without allowing already-due editor work to spin. Interrupted polls capture the poll errno before cancelling a prepared Wayland read so `EINTR` is not misread as a hard failure.

Session shell mapping (portal request IDs to application dialog IDs, dialog startup failure, and accept-close) lives in `WaylandApplicationAdapter` and is covered by deterministic tests without a display. Context-menu popup create, paint, and destroy remain in the runner beside EGL. Force-close, accepted close, and quit-from-input all call `PrepareForExit` before leaving the loop so menus and context popups are dismissed consistently.

## Frame lifecycle

Damage accumulation is separate from compositor permission to submit. `WaylandFrameState` retains pending invalidation while a frame callback is outstanding, captures the damage used by an active paint, and preserves invalidation raised during painting for the next frame.

Buffer-age history expands repaint damage when preserved buffers are available. Missing buffer age or damage-swap support selects a full repaint or full swap. Presentation feedback reports a submitted frame but does not grant permission for the next one; only the frame callback controls pacing.

## Coordinate spaces

| Space | Unit and conversion |
| --- | --- |
| Editor and Wayland surface | Logical pixels, top-left origin, half-open bounds. Pointer input, editor layout, caret rectangles, and text-input rectangles remain in this space. |
| Buffer | Integer pixels, top-left origin. Its size is derived from the logical size and active integer or fractional scale, rounded to cover the complete surface. |
| Nominal raster scale | Exact rational `scaleNumerator / 120` from the preferred Wayland scale (`WaylandScaleConfiguration::scaleNumerator`). Owned by `ApplicationEditor` as `RasterScale` and applied with `Renderer::SetOutputRasterScale` from the frame path (external window surface or offscreen frame bind). It is not `bufferWidth / logicalWidth`: buffer dimensions are rounded upward and can change on ordinary resizes without a real scale change. |
| Renderer viewport | Buffer pixels. Drawing still accepts logical top-left coordinates; the renderer maps them onto the viewport. |
| Wayland buffer damage | Buffer pixels, top-left origin. Logical damage is rounded outward and clipped before `wl_surface.damage_buffer`. |
| EGL swap damage | Buffer pixels, bottom-left origin. Scaled damage is vertically converted immediately before the EGL swap request. |
| Cursor image | Theme-buffer pixels with a scaled hotspot; the cursor surface destination remains logical. |

A scale change updates the surface buffer scale or viewport destination, EGL window size, renderer target, damage history, cursor resources, and full-frame invalidation before the next paint. It also replaces the editor's nominal raster scale and calls `Renderer::SetOutputRasterScale`, which retires outline glyph textures rasterized for the previous scale immediately. Fixed bitmap and colour glyph textures are not treated as scale-independent: shrinking variants are keyed by the active `RasterScale` and retained under a three-generation least-recently-used bound, while non-shrinking requests share one full-strike generation (see [rendering.md](rendering.md)). It does not change the editor's logical client rectangle or scale pointer coordinates twice. Resizing the logical or buffer size while the preferred scale is unchanged keeps the same `RasterScale` identity and leaves the glyph texture cache in place.

## Generated protocols

CMake locates protocol XML through `wayland-protocols` and generates private client code with `wayland-scanner`. Generated headers and C files are build artifacts and are not edited or committed. The notices generated from the installed protocol descriptions remain with those artifacts.

## Verification

Lifecycle, input, cursor, transfer, clipboard, primary-selection, text-input, frame, scale, polling, D-Bus, portal, renderer, and application-host behavior have separate deterministic test targets. Cross-concern tests exercise removal and cancellation during active work. Live compositor checks confirm initialization and integration but do not stand in for deterministic tests of exact timing, partial I/O, global removal, or coordinate rounding.
