# Phase 7 Wayland shell boundary (frozen 2026-07-22)

Recorded before the minimal Phase 6 shell grows into the complete standalone shell. [ROADMAP.md](../ROADMAP.md) phase 7 lists the ordered work. This document fixes the globals, protocols, dependencies, application hooks, coordinate spaces, retained source references, and license rules that those steps must cover.

The current baseline is deliberately small. `WaylandWindow` owns one display, registry, compositor, xdg-toplevel, current seat, keyboard, pointer, surface, and `wl_egl_window`; its callbacks feed `WaylandLifecycle` and `WaylandInput`. `ApplicationEditor` owns Scintilla host state, drawing, invalidation, editor deadlines, idle work, and visible unsupported-service reports. `main.cxx` moves resize and input events from the shell to the editor and waits only on the Wayland display plus the next editor deadline.

Phase 7 keeps one direct shell. New state may be split by concern when that makes lifetime or testing clear, but it must not become a reusable window toolkit or protocol wrapper library.

## Step ownership

| Concern | Phase 7 step | Boundary |
| --- | --- | --- |
| Registry, output, seat, keyboard, and pointer object lifetime | 2 | Track registry names, handle addition and removal, and clear application-visible focus and input state before destroying an object. |
| Toplevel state, decoration, and pointer-axis frames | 3 | Retain compositor state separately from object ownership; translate one logical scroll event per pointer frame. |
| Compose and key repeat | 4 | Extend the existing xkbcommon input path; repeat deadlines join the main loop and never use a worker thread. |
| Cursor themes | 5 | The shell owns the theme, cursor surface, buffers, pointer serial, scale, and the mapping from every Scintilla cursor. |
| Clipboard | 6 | The shell owns Wayland offers and sources; the application host starts reads and writes and receives completion or failure. |
| Primary selection | 7 | Use the primary-selection protocol when advertised; reuse only the nonblocking byte-transfer machinery from clipboard. |
| text-input-v3 | 8 | The shell owns protocol enable/disable and batches; the application host supplies surrounding text and applies pre-edit, commit, and deletion. |
| Frame callbacks and presentation feedback | 9 | Frame callbacks control permission to submit another redraw; presentation feedback reports a submitted frame and does not pace drawing. |
| Output and surface scaling | 10 | One scale state converts editor and surface units into buffer pixels for rendering, damage, cursors, and protocol rectangles. |
| D-Bus and xdg-foreign readiness | 11 | The existing wait grows to accept file descriptors and deadlines from multiple sources; file chooser policy remains Phase 8. |
| Integration, documentation, and seed removal | 12 | Exercise combined changes and absent protocols, remove absorbed backend seed files, retain notices, and run the phase gate. |

## Registry globals

Only globals required to create and keep the main surface are fatal when absent or permanently removed. Input and optional services may appear after startup and disappear while the editor remains open.

| Global | Requirement | Use and fallback | Step |
| --- | --- | --- | --- |
| `wl_compositor` | Required | Creates the main and cursor surfaces. Permanent removal closes the shell cleanly because no replacement can repair existing surfaces. | 2, 5 |
| `xdg_wm_base` | Required | Creates and controls the xdg-toplevel. Permanent removal closes the shell cleanly. | 2, 3 |
| `wl_seat` | Optional and hot-pluggable | Each advertised seat is tracked by registry name. The shell selects the active seat deliberately; no seat means the editor stays visible without keyboard, pointer, clipboard device, primary-selection device, or text input. | 2 |
| `wl_output` | Optional and hot-pluggable | Track every output entered by the main surface. With no output information, retain scale 1 until the compositor supplies better information. | 2, 10 |
| `wl_shm` | Optional service | Needed by `wayland-cursor`. If absent, editing continues and cursor application reports an unavailable cursor service rather than failing startup. | 2, 5 |
| `wl_data_device_manager` | Optional service | Clipboard ownership and offers are unavailable when absent. Copy and paste report failure without changing the document or claiming ownership. | 2, 6 |
| `zxdg_decoration_manager_v1` | Optional protocol | Request server-side decoration when present; otherwise accept the compositor's xdg-shell decoration behavior. The project does not add client-side chrome in Phase 7. | 3 |
| `wp_presentation` | Optional protocol | Report presentation time and discard events when present. Frame pacing remains correct with only `wl_surface.frame`. | 9 |
| `zwp_primary_selection_device_manager_v1` | Optional protocol | Primary selection and middle-button paste are disabled when absent; ordinary clipboard behavior is unchanged. | 7 |
| `zwp_text_input_manager_v3` | Optional protocol | Direct key input remains available when absent; pre-edit and compositor IME operations are disabled. | 8 |
| `wp_viewporter` | Optional protocol paired with fractional scale | Sets the logical destination for a fractionally scaled buffer. Without the complete fractional-scale pair, use integer buffer scale. | 10 |
| `wp_fractional_scale_manager_v1` | Optional protocol paired with viewporter | Supplies preferred scale in 120ths. Ignore fractional-scale support unless `wp_viewporter` is also present. | 10 |
| `zxdg_exporter_v2` | Optional protocol | Exports the toplevel handle used to parent a later portal dialog. An absent exporter leaves Phase 8 able to request an unparented portal dialog. | 11 |

Registry removal is matched by the numeric name delivered at bind time, not only by interface. Removal first cancels dependent activity and clears queued application state, then destroys child objects and the bound global. A later advertisement creates fresh objects and listeners; stale serials, focus, pressed keys, repeat state, pointer coordinates, offers, sources, text-input batches, callbacks, and scale membership do not cross that boundary.

The implementation binds no version higher than both the advertised version and the newest request or event it handles. Phase 7 raises the Phase 6 `wl_seat` and xdg-shell bindings as needed for keyboard repeat information, pointer frame events, current axis values, release requests, and retained toplevel state; callbacks newer than the bound version are never assumed to arrive.

## Generated protocols

`wayland-scanner` continues to generate client headers and private code into the application build directory. Core Wayland interfaces come from `wayland-client` and are not generated here.

| XML from `wayland-protocols` | Interface used | Runtime rule | Step |
| --- | --- | --- | --- |
| `stable/xdg-shell/xdg-shell.xml` | `xdg_wm_base`, `xdg_surface`, `xdg_toplevel` | Required; already generated in Phase 6. | 2–3 |
| `unstable/xdg-decoration/xdg-decoration-unstable-v1.xml` | `zxdg_decoration_manager_v1`, `zxdg_toplevel_decoration_v1` | Optional. | 3 |
| `unstable/primary-selection/primary-selection-unstable-v1.xml` | `zwp_primary_selection_device_manager_v1` and its device, offer, and source | Optional. | 7 |
| `unstable/text-input/text-input-unstable-v3.xml` | `zwp_text_input_manager_v3`, `zwp_text_input_v3` | Optional. | 8 |
| `stable/presentation-time/presentation-time.xml` | `wp_presentation`, `wp_presentation_feedback` | Optional. | 9 |
| `stable/viewporter/viewporter.xml` | `wp_viewporter`, `wp_viewport` | Optional and useful only with fractional scale in this shell. | 10 |
| `staging/fractional-scale/fractional-scale-v1.xml` | `wp_fractional_scale_manager_v1`, `wp_fractional_scale_v1` | Optional and useful only with viewporter. | 10 |
| `unstable/xdg-foreign/xdg-foreign-unstable-v2.xml` | `zxdg_exporter_v2`, `zxdg_exported_v2` | Optional. | 11 |

Protocol support is compiled in when its XML is installed, but every extension is still conditional on its registry global. Generated code belongs to the build tree and is not checked in. CMake names each XML, generated header, generated source, and dependency explicitly rather than copying the broad retained seed target.

## Library dependencies

| pkg-config module or tool | Phase 7 role | Requirement |
| --- | --- | --- |
| `wayland-client` | Registry, core globals, surfaces, input objects, data-device clipboard, callbacks, and display dispatch. | Existing required build dependency. |
| `wayland-egl` | Resize the `wl_egl_window` as logical and buffer sizes change. | Existing required build dependency. |
| `wayland-protocols` and `wayland-scanner` | Supply and generate the protocol list above. | Existing required build tools; configure fails with a named missing XML. |
| `wayland-cursor` | Load themes, choose named cursors, and obtain shared-memory cursor buffers. | New required build dependency for step 5; runtime theme or `wl_shm` failure has an explicit fallback. |
| `xkbcommon` | Existing keymap translation plus locale-aware compose state and repeatability queries. | Existing required build dependency. |
| `egl` and OpenGL | Existing window rendering, resized buffers, damage-aware swaps when supported. | Existing required build dependencies. |
| `dbus-1` | Expose session-bus watches and timeouts to the event loop in preparation for portal file dialogs. | New required build dependency in step 11. |

The versions installed at the freeze are `wayland-client` 1.24.0, `wayland-egl` 18.1.0, `wayland-cursor` 1.24.0, `wayland-protocols` 1.45, `xkbcommon` 1.8.0, and `dbus-1` 1.14.10. CMake discovers modules rather than pinning these host versions. A later implementation may state a lower bound only when the source actually uses a feature introduced at that bound.

No thread library or generic asynchronous library is added. Wayland, transfer file descriptors, D-Bus watches, D-Bus timeouts, key repeat, editor tickers, and frame safety deadlines share the one application loop.

## Application host hooks

| Existing hook or state | Phase 7 connection | Step |
| --- | --- | --- |
| `ApplicationWindowData::cursor` and `Window::SetCursor` | Preserve the requested Scintilla cursor even before pointer entry or cursor-service availability, then apply it whenever pointer, serial, theme, or scale changes. | 5 |
| `ApplicationEditor::Copy`, `CopyToClipboard`, and `RequestClipboardCopy` | Publish UTF-8 selection text through a data source and report whether ownership was accepted or cancelled. | 6 |
| `ApplicationEditor::Paste`, `CanPaste`, and `ClipboardPasteAvailable` | Start an asynchronous preferred-MIME read; call `InsertPaste` only after successful completion against the still-live document. | 6 |
| `ApplicationEditor::ClaimSelection` | Publish the current selection through primary selection when supported; selection changes do not overwrite the ordinary clipboard. | 7 |
| Pointer middle-button input | Request primary-selection text and insert it at the editor's middle-click position when available. | 7 |
| `ScintillaBase::MoveImeCarets`, `DrawImeIndicator`, `Editor::SetIMEInteraction`, tentative input, and `InsertCharacter` | Apply text-input-v3 delete, pre-edit, and commit batches in protocol order; cancellation removes tentative input without recording an edit. | 8 |
| `Editor::NotifyCaretMove`, `UpdateSystemCaret`, selection, and client geometry | Refresh surrounding text, cursor position, and the surface-local cursor rectangle before text-input commits that need them. | 8 |
| `ApplicationEditor::NeedsRedraw`, invalidation rectangles, `PaintFrame`, and `PresentFrame` | Retain damage while a frame callback is outstanding, paint only when submission is allowed, and preserve invalidation that arrives during paint or swap. | 9 |
| `ApplicationEditor::Resize`, renderer target size, and `WaylandLifecycle` configure state | Apply one coherent logical size and buffer scale before painting the next frame. | 10 |
| `ApplicationEditor::TimeUntilNextWork` and `WaylandWindow::WaitForEvents` | Merge editor, repeat, D-Bus, and safety deadlines and monitor all enabled file descriptors without busy waits. | 4, 6, 11 |

Clipboard, primary-selection, and IME completion must not hold a raw editor pointer past editor destruction or silently apply to a replacement document. The host owns cancellation tokens or equivalent lifetime state and turns cancellation, unavailable services, invalid MIME data, read or write failure, and superseded requests into observable results.

Popup windows, drag and drop, a system accessibility caret, file chooser choices, file loading and saving, and product chrome are not added by Phase 7. Existing popup stubs remain explicit; Phase 8 owns file workflow and dialog policy.

## Coordinate spaces

| Space | Unit and origin | Uses and conversion rule |
| --- | --- | --- |
| Editor | Logical pixels, top-left origin, half-open `PRectangle` bounds | Scintilla layout, pointer hit testing, caret, selection, invalidation, and application client size. |
| Wayland surface | Logical surface units, top-left origin | Pointer coordinates and text-input cursor rectangles map directly to editor coordinates after clamping and integer rounding required by a protocol request. |
| Buffer | Integer pixels, top-left origin | Size is the logical surface size multiplied by the selected scale, rounded up so the full logical extent is covered. Integer scale uses `wl_surface.set_buffer_scale`; fractional scale uses a scaled buffer plus a viewporter destination in logical units. |
| Renderer and OpenGL viewport | Buffer pixels; drawing accepts top-down rectangles while OpenGL's framebuffer origin is bottom-left | Scale editor geometry once at the renderer boundary. Existing clip and transform code performs the vertical conversion; editor layout does not adopt framebuffer coordinates. |
| Wayland buffer damage | Buffer pixels, top-left origin | Scale editor invalidation outward with floor for left/top and ceiling for right/bottom, clip to the buffer, and submit with `wl_surface.damage_buffer` when supported. |
| EGL swap damage | Buffer pixels, bottom-left origin | Convert scaled top-down damage by the current buffer height immediately before the damage-swap call. Fall back to a full swap when the extension is unavailable. |
| Cursor image | Buffer pixels with an image-pixel hotspot; cursor surface destination is logical | Load at the effective output scale, set the cursor surface buffer scale, and convert the hotspot consistently. Rebuild on pointer-seat or scale changes. |
| Output membership and scale | Set of entered `wl_output` objects plus integer output scale and optional preferred scale in 120ths | Choose one effective scale for the main surface, then update EGL buffer size, renderer target, damage, cursor, and protocol rectangles as one state change. |

Configure width and height remain logical surface sizes. A scale change does not change Scintilla's logical client rectangle or multiply pointer input twice. Fractional preferred scale never goes directly into an integer `wl_surface.set_buffer_scale`; viewporter supplies the logical destination.

## Retained source references

The seed tree is reference material and does not build. Code is rewritten for this application's direct ownership model; files are deleted once their remaining useful behavior has been absorbed.

| Retained path | Useful Phase 7 material | Missing or unsuitable material | Delete when |
| --- | --- | --- | --- |
| `seed/backends/OnlyWayUi_Platform_Wayland.cpp` and `.h` | Clipboard offers and sources, nonblocking file descriptors, cursor theme loading, keymap handling, and presentation clock reporting. | No compose, primary selection, text-input-v3, output tracking, fractional scale, viewporter, or robust global removal; interfaces are tied to OnlyWayUi. | Step 12 after direct replacements exist. |
| `seed/backends/OnlyWayUi_Backend_Wayland_GL3.cpp` | Decoration, presentation, xdg-foreign, frame callback, buffer-age damage, blocked-flush recovery, D-Bus watch and timeout, and portal-parent patterns. | Registry removal is empty; one seat is assumed; scale is fixed; portal calls include blocking setup that must not enter this event loop. | Step 12 after direct replacements exist. |
| `seed/backends/OnlyWayUi_Backend.h` | Observable close and asynchronous dialog contract ideas. | Generic backend interface and file-dialog policy do not belong in the shell. | Step 12 with the backend snapshot. |
| `seed/backends/OnlyWayUi_Include_GL3.h` | Historical include connection only. | Names the already removed renderer and has no behavior to absorb. | Step 12 with the backend snapshot. |
| `seed/backends/OnlyWayUi_Portal_Uri.cpp` and `.h` | File-URI conversion needed by Phase 8 dialog results. | Not needed for Phase 7 shell behavior. | Retain through Phase 8 unless replaced earlier. |
| `seed/backends/CMakeLists.txt` and `seed/cmake/DependenciesForBackends.cmake` | Protocol-generation paths and dependency names. | Broad historical target names removed files and must not be copied. | Remove when no retained seed file needs them as context. |
| `seed/editor/` and `seed/sample/` | Historical asynchronous clipboard callback and editor-loop behavior. | RmlUi ownership and product UI are not the project architecture. | Remove at the Phase 7 gate if no Phase 8 work still needs them; otherwise record the remaining owner. |

## License rules

`ORIGINS.md` identifies the seed as OnlyWayUi tag `scintilla-seed`, commit `5e373e9e8fd3d83c7f514f029a2299df9c1face2`. `seed/LICENSE.txt` contains its MIT notice. Production files already derived from that backend name the retained notice in their source comments.

When a Phase 7 implementation derives a substantial part from the seed, keep the existing source comment and retain the full OnlyWayUi notice in the distributed project after deleting the seed tree. Step 12 must move or copy the notice to a durable license location before deleting `seed/LICENSE.txt`; it must not discard the notice because the reference files are gone.

Generated Wayland protocol files remain build artifacts. Preserve the notices emitted from their installed XML inputs and do not edit generated files. New code based only on installed Wayland, xkbcommon, EGL, or D-Bus declarations follows those local headers and protocol descriptions and does not copy seed implementation text.
