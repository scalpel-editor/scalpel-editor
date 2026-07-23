# Phase 7 Wayland shell boundary (frozen 2026-07-22)

Recorded before the minimal Phase 6 shell grows into the complete standalone shell. [ROADMAP.md](../ROADMAP.md) phase 7 lists the ordered work. This document fixes the globals, protocols, dependencies, application hooks, coordinate spaces, retained source references, and license rules that those steps must cover.

The shell now has robust core-global, output, and seat lifetimes plus composed and repeating keyboard input, themed cursors, asynchronous clipboard and primary-selection transfers, text-input-v3 IME batches, compositor-paced damaged frames with optional presentation reports, coherent scaling, and portal-ready event sources and parent state. `WaylandWindow` owns the display, registry, required globals, xdg-toplevel, bound outputs, active seat devices, main and cursor surfaces, cursor theme, transfer and text-input managers, frame callback and presentation objects, optional D-Bus connection, xdg-foreign export, and `wl_egl_window`; thin callbacks feed deterministic registry, output-membership, seat-capability, window, cursor, input, offer, transfer, IME, frame, presentation, scale, event-loop, and portal-parent state. `ApplicationEditor` owns Scintilla host state, drawing, invalidation, editor deadlines, idle work, transfer values, surrounding text, cursor rectangles, and tentative IME edits. `main.cxx` moves resize, input, transfer, IME, and frame damage values between the shell and editor and returns the retained cursor request, while one poll snapshot combines Wayland, transfer, and enabled D-Bus descriptors with editor, key-repeat, transfer, and D-Bus deadlines.

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
| Integration, documentation, and seed removal | 12 | Exercise combined changes and absent protocols, remove absorbed backend seed files, audit remaining license requirements, and run the phase gate. |

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

Registry removal is matched by the numeric name delivered at bind time, not only by interface. The earliest advertised seat stays active until removal, when the earliest remaining seat is bound as a fresh replacement; capability loss alone does not switch seats. Removal first cancels dependent activity and clears queued application state, then destroys child objects and the bound global. A later advertisement creates fresh objects and listeners; stale serials, focus, pressed keys, repeat state, pointer coordinates, offers, sources, text-input batches, callbacks, and scale membership do not cross that boundary.

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

## Deterministic test boundary

`waylandCursorTest`, `waylandScaleTest`, `waylandFrameTest`, `waylandWindowTest`, and `waylandRegistryTest` cover their corresponding lifecycle concerns; `waylandTextInputTest`, `waylandKeyboardTest`, and `waylandPointerTest` cover their corresponding input concerns; `waylandByteTransferTest`, `waylandClipboardTest`, and `waylandPrimarySelectionTest` cover their corresponding transfer concerns; `waylandPollTest`, `waylandDbusTest`, and `waylandPortalTest` cover their corresponding event-loop concerns; `applicationHostTest`, `applicationImeTest`, `applicationTransferTest`, and `applicationInputTest` cover their corresponding production-host concerns; and `waylandIntegrationTest` composes those public state boundaries with the production editor host for the six frozen cross-concern sequences.

Protocol listeners remain thin adapters. They validate and copy callback arguments into plain application-owned state, ask that state for the next actions, and perform the resulting Wayland create, destroy, acknowledge, or commit calls. Tests drive the plain state and inspect its actions; they do not construct fake Wayland proxy objects or duplicate generated listener structures. A live compositor check covers the small adapter layer and real object ordering.

Time-based state receives a controlled monotonic clock. Byte transfers use real nonblocking pipes in tests so short reads, short writes, `EAGAIN`, EOF, peer closure, cancellation, and descriptor cleanup execute without a clipboard manager. Event-loop scheduling separates collection of file descriptors and deadlines from the one production `poll` call, allowing readiness and timeout decisions to be tested without waiting on wall time.

| Step | Deterministic seam and cases | Focused target |
| --- | --- | --- |
| 2 — globals, outputs, and seats | A registry and device-lifecycle state consumes plain global additions, removals, and capability sets keyed by registry name. Cover duplicate announcements, multiple seats and outputs, active-seat removal, capability loss and regain, focus clearing before teardown, fresh replacement state, required-global loss, and no-seat/no-output startup. | Extend `waylandRegistryTest`. |
| 3 — toplevel and pointer | Toplevel state consumes copied configure bounds, state values, and capabilities; a pointer-axis accumulator consumes legacy values and framed source, stop, discrete, value120, and relative-direction events. Cover decoration absence and removal, retained maximize/fullscreen/activation state, zero-size configure, one scroll result per frame, legacy compositor fallback, and no double count. | Extend `waylandWindowTest` for toplevel state and `waylandPointerTest` for pointer frames. |
| 4 — keyboard | `WaylandInput` owns compose state and a repeat scheduler driven by the controlled clock. Cover locale creation failure, dead-key composition, cancelled and multi-byte composition, command keys bypassing text composition, compositor rate/delay changes, non-repeatable keys, deadline catch-up limits, and cancellation on release, focus loss, capability loss, and seat removal. | Extend `waylandKeyboardTest`; use Catch test-name patterns for compose and repeat while iterating. |
| 5 — cursors | A total cursor-choice function maps every `Window::Cursor` value to ordered Wayland names and a final fallback; cursor state retains request, pointer serial, entry, theme availability, and scale. Cover missing requested names, missing theme or `wl_shm`, request before pointer entry, leave and re-entry, seat replacement, and scaled hotspot and buffer size. | Extend `waylandCursorTest`; keep theme loading itself in the live check. |
| 6 — clipboard | A transfer owns one offer/source transaction and exposes its desired poll direction and one completion result. Drive it with nonblocking pipes through absent or removed data-device manager, MIME preference, empty data, invalid or absent UTF-8 MIME, partial I/O, `EAGAIN`, EOF, peer failure, source send and cancellation, superseded paste, seat removal, and editor or document destruction. | Add `waylandByteTransferTest` and `waylandClipboardTest`; cover final insertion and visible failure in `applicationTransferTest`. |
| 7 — primary selection | Primary ownership state uses the same byte-transfer object but distinct offers, sources, serials, and cancellation. Cover absent or removed manager, independent clipboard and primary ownership, selection changes, middle-button request position, stale offer replacement, and seat removal. | Extend `waylandPrimarySelectionTest` and the middle-button host cases in `applicationTransferTest`. |
| 8 — text input | A text-input batch copies pre-edit, commit, delete-surrounding, and done events and emits ordered editor operations only at `done`. Cover enable/disable around keyboard focus, serial ordering, replacement pre-edit, commit with deletion, UTF-8 byte boundaries, surrounding-text limits, cursor rectangle conversion, cancellation, manager removal or seat loss, and no-protocol direct input. | Extend `waylandTextInputTest` for batches and `applicationImeTest` for tentative text, indicators, commit, deletion, and undo behavior. |
| 9 — frames | Frame state distinguishes invalidated, callback-outstanding, painting, submitted, presented, and discarded state; pure damage conversion handles top-down surface, buffer, and EGL coordinates. Cover invalidation before and during an outstanding frame, invalidation during paint, callback cancellation on teardown, absent or removed presentation, out-of-order feedback, discarded feedback, buffer-age history, damage extension fallback, and no empty redraw spin. | Extend `waylandFrameTest`; keep renderer damage bounds in `applicationHostTest` or the existing renderer tests. |
| 10 — scaling | Scale state consumes output enter/leave, output scale, preferred fractional scale, and protocol availability, then emits one logical size, buffer size, integer buffer scale, viewport destination, and effective cursor scale. Cover multiple outputs, removal of the highest-scale output, live integer and fractional changes, rounding, missing or removed viewporter or fractional manager, pointer stability, caret and text-input rectangles, damage conversion, cursor hotspots, and EGL resize ordering. | Extend `waylandScaleTest` and focused renderer coordinate checks. |
| 11 — event loop | A poll-source plan merges the Wayland descriptor, transfer descriptors, enabled D-Bus watches, and the earliest editor, repeat, D-Bus, and safety deadline. Cover read/write interest, blocked Wayland flush recovery, source enable/disable, simultaneous readiness, timeout update and removal, interrupted poll, no-source behavior, fairness, and no busy wait. xdg-foreign state covers handle delivery, replacement, destruction, and absent or removed exporter. | Add `waylandPollTest`, `waylandDbusTest`, and `waylandPortalTest`; use controlled readiness and clocks rather than elapsed-time assertions. |
| 12 — integration | Drive sequences that cross the seams: seat removal during repeat and compose, scale change during an outstanding frame, output removal while a cursor is entered, clipboard transfer during blocked flush, IME cancellation followed by direct input, and shutdown with every optional service absent. | Run all application targets, the normal workflow, live checks, then the sanitizer matrix. |

Test state through public concern methods and observable results, not by making listener callbacks public. Keep proxy allocation and destruction in the shell adapter and keep editor mutations in `ApplicationEditor`; state objects return narrow actions rather than calling either side through a general interface.

## Focused and final verification

During implementation, build only the target owned by the row above and run a matching Catch pattern. When production headers or their includes change, check only the affected paths with `tools/check-self-contained-headers.sh --changed` or explicit header arguments; reserve the no-argument full scan for phase gates. Every compiled-code handoff ends with `cmake --workflow --preset dev`; the full `./check.sh` matrix is reserved for step 12 or an earlier change with the risk categories named in `AGENTS.md`.

Optional-protocol absence is an automated state case for every extension. The live check records which globals the compositor advertised and verifies the paths available in that environment: initial configure and resize, keyboard and pointer focus, compose and repeat, cursor update, clipboard and primary selection, IME when offered, live scale change when available, frame callbacks, presentation feedback when offered, clean global or seat loss when the compositor can exercise it, and clean close. A live compositor is not used for exact timing, partial-transfer, or coordinate-rounding assertions.

Step 12 must not claim an optional live path was exercised when the compositor did not advertise it. The deterministic fallback test remains required, and the phase-gate record distinguishes automated coverage, supported live coverage, and protocols unavailable in the live environment.

## Application host hooks

| Existing hook or state | Phase 7 connection | Step |
| --- | --- | --- |
| `ApplicationWindow::cursor` and `Window::SetCursor` | Preserve the requested Scintilla cursor even before pointer entry or cursor-service availability, then apply it whenever pointer, serial, theme, or scale changes. | 5 |
| `ApplicationEditor::Copy`, `CopyToClipboard`, and `RequestClipboardCopy` | Publish UTF-8 selection text through a data source and report whether ownership was accepted or cancelled. | 6 |
| `ApplicationEditor::Paste`, `ClipboardPasteAvailable`, and `Editor::InsertPaste` | Start an asynchronous preferred-MIME read; call `InsertPaste` only after successful completion against the still-live document. | 6 |
| `ApplicationEditor::ClaimSelection` | Publish the current selection through primary selection when supported; selection changes do not overwrite the ordinary clipboard. | 7 |
| Pointer middle-button input | Request primary-selection text and insert it at the editor's middle-click position when available. | 7 |
| `ScintillaBase::MoveImeCarets`, `DrawImeIndicator`, `Editor::SetIMEInteraction`, tentative input, and `InsertCharacter` | Apply text-input-v3 delete, pre-edit, and commit batches in protocol order; cancellation removes tentative input without recording an edit. | 8 |
| `Editor::NotifyCaretMove`, `UpdateSystemCaret`, selection, and client geometry | Refresh surrounding text, cursor position, and the surface-local cursor rectangle before text-input commits that need them. | 8 |
| `ApplicationEditor::NeedsRedraw`, invalidation rectangles, `RenderFrame`, and `PresentFrame` | Retain damage while a frame callback is outstanding, paint only when submission is allowed, and preserve invalidation that arrives during paint or swap. | 9 |
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

## Source reference disposition

The Phase 7 backend, editor-host, sample, and build references were removed after their useful behavior was represented by direct production code. The unabsorbed portal URI converter remains for Phase 8 and does not build in this repository.

| Original path | Useful Phase 7 material | Missing or unsuitable material | Disposition |
| --- | --- | --- | --- |
| Removed `seed/backends/OnlyWayUi_Platform_Wayland.cpp` and `.h` | Clipboard offers and sources, nonblocking file descriptors, cursor theme loading, keymap handling, and presentation clock reporting. | No compose, primary selection, text-input-v3, output tracking, fractional scale, viewporter, or robust global removal; interfaces are tied to OnlyWayUi. | Deleted in step 12 after direct replacements were complete. |
| Removed `seed/backends/OnlyWayUi_Backend_Wayland_GL3.cpp` | Decoration, presentation, xdg-foreign, frame callback, buffer-age damage, blocked-flush recovery, key-repeat scheduling and cancellation, D-Bus watch and timeout, and portal-parent patterns. | Registry removal is empty; one seat is assumed; scale is fixed; portal calls include blocking setup that must not enter this event loop. | Deleted in step 12 after direct replacements were complete. |
| Removed `seed/backends/OnlyWayUi_Backend.h` | Observable close and asynchronous dialog contract ideas. | Generic backend interface and file-dialog policy do not belong in the shell. | Deleted in step 12. |
| Removed `seed/backends/OnlyWayUi_Include_GL3.h` | Historical include connection only. | Leftover OpenGL function-loader include for the removed renderer path, with no behavior to absorb. | Deleted in step 12. |
| `seed/backends/OnlyWayUi_Portal_Uri.cpp` and `.h` | File-URI conversion needed by Phase 8 dialog results. | Not needed for Phase 7 shell behavior. | Retained through Phase 8 unless replaced earlier. |
| Removed `seed/backends/CMakeLists.txt` and `seed/cmake/DependenciesForBackends.cmake` | Protocol-generation paths and dependency names. | Broad historical target names removed files and must not be copied. | Deleted in step 12. |
| Removed `seed/editor/` and `seed/sample/` | Historical asynchronous clipboard callback and editor-loop behavior. | RmlUi ownership and product UI are not the project architecture. | Deleted in step 12; Phase 8 has no owner for them. |

## License rules

`ORIGINS.md` identifies the seed as OnlyWayUi tag `scintilla-seed`, commit `5e373e9e8fd3d83c7f514f029a2299df9c1face2`. That snapshot included an RmlUi-derived GL3 renderer under RmlUi's MIT notice, along with the author's later Wayland backend and portal URI converter.

The production shell was implemented directly from the installed Wayland and library sources, using the author's earlier Wayland backend only as a source of techniques. Step 12 deleted that backend, and the retained portal URI converter was also written by the repository author after the OnlyWayUi fork diverged from RmlUi. A later source audit found no RmlUi code in the current tree and removed the obsolete copy of its notice.

Generated Wayland protocol files remain build artifacts. Preserve the notices emitted from their installed XML inputs and do not edit generated files. New code based only on installed Wayland, xkbcommon, EGL, or D-Bus declarations follows those local headers and protocol descriptions and does not copy seed implementation text.

## Step 1 completion

This freeze inventories every Phase 7 roadmap concern, assigns its globals and protocols, fixes the application and coordinate boundaries, records dependencies and retained source obligations, and gives steps 2–12 deterministic and live verification paths. Later steps update this document when an implementation changes a recorded choice; they do not leave it describing an earlier shell.

## Step 2 completion

Registry, output, and active-seat decisions now live in `WaylandLifecycle` and return narrow actions executed by `WaylandWindow`. The state ignores duplicate announcements, tracks every output and its surface membership, closes on active required-global loss, keeps one stable active seat, promotes a remaining seat after removal, and recreates keyboard and pointer objects after capability regain. `WaylandInput` discards queued events from a disappearing device, reports focus or pointer loss, and resets modifier state before the proxy is released. Deterministic tests cover startup without seats or outputs, duplicates, multiple devices, removal, replacement, capability loss and regain, and stale-event clearing.

## Step 3 completion

`WaylandLifecycle` now retains xdg-toplevel bounds, maximize, fullscreen, resize, activation, window-manager capabilities, and decoration mode at the `xdg_surface.configure` boundary. The shell generates xdg-decoration, requests server-side decoration before the initial commit when the optional manager is present, preserves an existing decoration across manager removal, and safely tracks a replacement manager without remapping an existing toplevel. `WaylandInput` now accumulates pointer-axis source, stop, discrete, value120, relative-direction, and continuous values through `wl_pointer.frame`, prefers current high-resolution wheel values without counting their compatibility values again, emits one combined scroll event, and retains immediate axis delivery for pre-v5 pointers. Deterministic lifecycle and input tests cover absent and removed decoration support, state and capability changes, zero-size configure, framed diagonal scrolling, compatibility priority, stop-only frames, legacy fallback, and teardown with an unfinished frame.

## Step 4 completion

`WaylandInput` now builds compose state from the user's locale and retains direct xkb text when the locale has no compose table. Command-modified keys bypass composition, while focus and keymap changes discard unfinished sequences. The active compositor rate and delay configure a controlled-clock repeat scheduler that checks keymap repeatability, limits delayed catch-up, and cancels on release, focus or capability loss, keymap replacement, and seat removal. `WaylandWindow` gives repeat deadlines their own path through blocked-flush recovery without allowing already-due editor work to spin. Deterministic input tests cover multi-byte composition, cancellation, missing locale data, command keys, timing changes, non-repeatable keys, catch-up, and every cancellation path without a compositor.

## Step 5 completion

`WaylandCursorState` retains the requested Scintilla cursor, current pointer-entry serial, cursor-service availability, and effective scale, then emits an apply action only when all required state is present. Every `Window::Cursor` value has ordered standard and compatibility theme names ending in the default arrow fallbacks. `WaylandWindow` binds hot-pluggable `wl_shm`, loads the environment or KDE-configured cursor theme and size through `wayland-cursor`, owns the cursor surface and theme resources, applies scaled buffers and hotspots, and rebuilds after shared-memory replacement or an effective-scale change. The application loop returns the cursor retained by `ApplicationWindow`, while deterministic tests cover missing preferred names, unavailable themes, requests before entry, leave and re-entry, pointer replacement, and scaled geometry; real theme loading remains part of the live Wayland check.

## Step 6 completion

`WaylandTransfer` owns one nonblocking read or write descriptor, a controlled deadline, byte limits, and exactly one completion result. `WaylandClipboardState` retains service, seat, input serial, ownership, offer, and preferred UTF-8 MIME choices without protocol objects, while `WaylandClipboard` owns the thin core-protocol adapters, current offer and source, seat-bound data device, and active transfers. Transfer descriptors and deadlines join `WaylandWindow::WaitForEvents`; manager or seat loss cancels stale work and clears serials and offers. `ApplicationEditor` emits value requests and accepts value results without leaving a callback or editor pointer in the shell, applies paste text only when its request and document generation remain current, and records unavailable, invalid, cancelled, failed, oversized, and timed-out operations. Deterministic tests cover pipe I/O, MIME choice, UTF-8 validation, optional-service fallback, manager replacement, successful insertion, visible failure, superseded requests, and document replacement.

## Step 7 completion

`WaylandPrimarySelectionState` and `WaylandPrimarySelection` keep the optional manager, seat device, current source and offer, input serial, ownership, cancellation, and results separate from the ordinary clipboard. They share only the stateless text MIME checks and `WaylandTransfer` descriptor, limit, and deadline handling. Primary descriptors and deadlines join the same window poll without changing clipboard availability or ownership, and manager or seat loss destroys child protocol objects before their parents. `ApplicationEditor::ClaimSelection` publishes keyboard-driven changes immediately, retains the latest pointer-driven value until mouse release, and sends a null selection only after this editor owned a nonempty selection. A middle-button press retains its editor position, requests primary text without clearing the current primary source, and applies only a current result for the same document revision. Deterministic lifecycle, transfer, and application tests cover optional-manager absence and replacement, UTF-8 offer choice, independent clipboard ownership, stale offers, serial and seat reset, drag completion, retained paste position, and results superseded by document replacement or editing.

## Step 8 completion

`WaylandTextInputState` retains optional-protocol availability, surface and keyboard focus, editor state, client commit serials, and copied pre-edit, commit, and byte-deletion events without protocol objects. It emits enable, disable, state, and commit requests in protocol order, publishes editor batches only at `done`, withholds state after a mismatched serial, and cancels stale composition on leave, manager removal, or seat loss. `WaylandTextInput` owns the active seat's protocol object and keeps its listener thin. `ApplicationEditor` supplies at most 4,000 bytes of UTF-8 surrounding the complete main selection, omits oversized selections, excludes tentative text from that value, reports a logical surface-local caret rectangle, and applies deletion, committed text, indicators, tentative replacement, caret movement, cancellation, and undo through retained Scintilla operations. Deterministic lifecycle, input, and application tests cover manager replacement, focus-gated enablement, serial ordering, replacement pre-edit, commit with deletion, invalid UTF-8 boundaries, surrounding limits, cursor rectangles, tentative undo, cancellation, and direct keyboard input without the optional protocol.

## Step 9 completion

`WaylandFrameState` now separates queued invalidation, active painting, callback permission, submitted damage history, and presentation completion without protocol objects. The application loop captures editor invalidation before painting, submits only when no frame callback is outstanding, and leaves invalidation raised during paint queued for the next callback. Buffer-age history expands repaint damage, buffer and EGL conversions preserve their respective top-left and bottom-left origins, and missing age or damage-swap extensions select full repaint or swap fallbacks. `WaylandWindow` owns and cancels one frame callback plus bounded presentation feedback objects, while presentation timestamps and discards report the matching submission without releasing pacing permission. The live path remains at scale 1 until step 10 selects a coherent buffer scale; the damage helpers already accept explicit logical-to-buffer scaling. Deterministic frame and application tests cover invalidation at each stage, cancellation, idle behavior, clipping, conversion, history bounds, extension fallback, optional presentation replacement, out-of-order completion, discards, and bounded paint rectangles.

## Step 10 completion

`WaylandScaleState` selects one configuration from entered output scales, the core surface preference, optional fractional preference, protocol availability, and logical size. Fractional scaling is active only with both viewporter and fractional-scale-v1; older surfaces and removed managers fall back to supported integer or scale-1 paths. `WaylandWindow` applies each change before the next paint by cancelling stale frame permission, setting surface scale or viewport destination, resizing the EGL native window in buffer pixels, rebuilding cursor resources, clearing buffer-age history, and requesting a full logical repaint. `ApplicationEditor` retains logical client, pointer, caret, font, and text-input geometry while its renderer maps projection and clips into a separately sized framebuffer. Frame painting remains logical, while Wayland and EGL damage is rounded outward against the actual buffer dimensions and uses the correct top-left or bottom-left origin. Deterministic lifecycle, renderer, and application tests cover multiple outputs, highest-scale removal, live integer and fractional changes, rounded dimensions, missing and replaced protocol managers, old-surface fallback, scaled clips, cursor geometry, damage conversion, logical pointer behavior, and logical IME rectangles.

## Step 11 completion

`WaylandEventLoop` builds one poll snapshot whose descriptors retain their ready callbacks and whose timeout is the earliest eligible editor, repeat, transfer, or D-Bus deadline. Editor work is suppressed only while a blocked Wayland flush is being recovered; ready services and expired deadlines return control without starving application work. `WaylandDbus` lazily owns a private session-bus connection, registers and safely tears down libdbus watch and timeout callbacks, services every ready watch, rearms enabled timeouts from a controlled clock, and drains pending dispatch without adding portal request policy. `WaylandLifecycle` treats `zxdg_exporter_v2` as an optional replaceable global, while `WaylandPortalParentState` accepts a handle only from the current export and clears it before proxy destruction. The shell exposes the resulting `wayland:` token and lazy bus connection for Phase 8; exporter or session-bus absence leaves an explicit unparented, unavailable fallback. Deterministic event-loop and lifecycle tests cover source interest, simultaneous readiness, deadline selection, blocked flush recovery, interrupted poll, empty sources, D-Bus watch enablement and removal, timeout update and removal, callback teardown, exporter replacement, stale handle delivery, and parent-token destruction.

## Step 12 completion

Automated coverage: `waylandIntegrationTest` passes 68 assertions across the six frozen sequences: seat removal during repeat and compose, a scale change during an outstanding frame, output removal while a cursor is entered, clipboard transfer readiness during blocked-flush recovery, IME cancellation followed by direct input, and shutdown with every optional service absent. Existing lifecycle, input, transfer, event-loop, frame, renderer, and application tests retain the individual unavailable-service, manager-removal, stale-state, timing, and coordinate cases. At the Phase 7 gate, the full self-contained-header audit passed 75 production headers, the normal workflow and all six application CTest entries passed, and `./check.sh` passed all 11 tests in each of the `dev`, AddressSanitizer, and UndefinedBehaviorSanitizer trees.

Live coverage: `wayland-info` on the current KWin session advertised the required compositor and xdg-shell globals plus `wl_shm`, `wl_data_device_manager`, xdg-decoration, primary-selection, text-input-v3, presentation-time, viewporter, fractional-scale-v1, and xdg-foreign-v2. A five-second launch completed initial configure and EGL setup, stayed live, and reported 11 compositor-presented frames on `CLOCK_MONOTONIC`, covering real frame-callback and presentation-feedback ordering at the current scale-1 configuration.

Not claimed as live coverage: this runner had no keyboard or pointer injection tool and did not safely control KWin globals, seats, input-method activation, clipboard ownership, or output scale. Compose and repeat, cursor changes, clipboard and primary selection, IME, live scale changes, clean compositor close, and global or seat removal therefore remain deterministic coverage rather than claimed live exercise. No Phase 7 optional global was unavailable in the observed KWin registry.

The absorbed Wayland backend, historical editor host, sample, and seed-build files were removed. `seed/backends/OnlyWayUi_Portal_Uri.*`, written by the repository author after the OnlyWayUi fork diverged from RmlUi, remains for Phase 8.
