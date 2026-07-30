# Wayland shell

The application uses one direct Wayland shell rather than a window toolkit or general protocol-wrapper library. `WaylandWindow` owns the display connection, registry, surfaces, xdg-shell objects, active input devices, EGL native window, cursor resources, selection protocols, text input, frame callbacks, presentation feedback, scale objects, optional D-Bus connection, file dialogs, and portal parent export.

Thin Wayland callbacks copy protocol events into state objects grouped by concern. Those state objects own no proxies and can be driven deterministically in tests. `WaylandWindow` applies their requested lifecycle actions and exposes copied input, resize, transfer, text-input, dialog, scale, and presentation results to the application loop.

## Application boundary

`ApplicationEditor` owns Scintilla state, drawing, invalidation, editor deadlines, clipboard values, surrounding text, cursor rectangles, and tentative IME edits. `DocumentWorkspace` owns tabs, file paths, open and save policy, stable file-dialog intents, and dirty-close transitions.

`main.cxx` is the platform adapter and current event pump. `ApplicationUi` owns chrome models, painters, overlay selection and composition, supplies one `ApplicationLayout` snapshot per event or paint pass, and applies pointer and keyboard priority plus focus-loss transitions; `main` delivers platform events and unconsumed pointer input, applies the returned cursor choice, drops IME batches while `ChromeOwnsInput` is true, calls `SynchronizeComposition` before paint, moves values among the window, editor, workspace, and UI state, submits frames, and waits for the next external event or application deadline. Wayland transport does not decide document-close or presentation policy.

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

- Clipboard paste results carry a request identity and apply only to the current document generation.
- Primary-selection paste retains its requested document position and applies only while the document revision remains current.
- Text-input state uses protocol commit serials and publishes copied batches only at `done`.
- File-dialog results carry a stable request ID and are matched to the original open or save intent, including the initiating tab.
- Portal parent handles are accepted only from the current xdg-foreign export.

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

Each descriptor retains the callback for its concern. The timeout is the earliest eligible deadline. A blocked Wayland flush is recovered without allowing already-due editor work to spin.

## Frame lifecycle

Damage accumulation is separate from compositor permission to submit. `WaylandFrameState` retains pending invalidation while a frame callback is outstanding, captures the damage used by an active paint, and preserves invalidation raised during painting for the next frame.

Buffer-age history expands repaint damage when preserved buffers are available. Missing buffer age or damage-swap support selects a full repaint or full swap. Presentation feedback reports a submitted frame but does not grant permission for the next one; only the frame callback controls pacing.

## Coordinate spaces

| Space | Unit and conversion |
| --- | --- |
| Editor and Wayland surface | Logical pixels, top-left origin, half-open bounds. Pointer input, editor layout, caret rectangles, and text-input rectangles remain in this space. |
| Buffer | Integer pixels, top-left origin. Its size is derived from the logical size and active integer or fractional scale, rounded to cover the complete surface. |
| Renderer viewport | Buffer pixels. Drawing still accepts logical top-left coordinates; the renderer maps them onto the viewport. |
| Wayland buffer damage | Buffer pixels, top-left origin. Logical damage is rounded outward and clipped before `wl_surface.damage_buffer`. |
| EGL swap damage | Buffer pixels, bottom-left origin. Scaled damage is vertically converted immediately before the EGL swap request. |
| Cursor image | Theme-buffer pixels with a scaled hotspot; the cursor surface destination remains logical. |

A scale change updates the surface buffer scale or viewport destination, EGL window size, renderer target, damage history, cursor resources, and full-frame invalidation before the next paint. It does not change the editor's logical client rectangle or scale pointer coordinates twice.

## Generated protocols

CMake locates protocol XML through `wayland-protocols` and generates private client code with `wayland-scanner`. Generated headers and C files are build artifacts and are not edited or committed. The notices generated from the installed protocol descriptions remain with those artifacts.

## Verification

Lifecycle, input, cursor, transfer, clipboard, primary-selection, text-input, frame, scale, polling, D-Bus, portal, renderer, and application-host behavior have separate deterministic test targets. Cross-concern tests exercise removal and cancellation during active work. Live compositor checks confirm initialization and integration but do not stand in for deterministic tests of exact timing, partial I/O, global removal, or coordinate rounding.
