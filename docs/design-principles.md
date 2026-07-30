# Design principles

scalpel-editor is a direct application, not a reusable GUI framework. Code is judged by whether it makes the editor correct, understandable, testable, and inexpensive to change under the constraints imposed by Wayland and Scintilla.

Transferable observations from this implementation are collected separately in [custom-wayland-ui.md](custom-wayland-ui.md).

## Prefer explicit protocol lifecycles

Wayland correctness depends on event ordering, proxy ownership, and capabilities that may appear or disappear. The shell keeps those transitions visible:

- Surfaces do not render before their initial configure.
- Child protocol objects are destroyed before their parents.
- Required-global loss closes the application; optional-global loss selects a tested fallback.
- Seat, output, focus, scale, transfer, frame, and text-input state can be cancelled or replaced without retaining stale work.

Plain state objects describe lifecycle decisions without owning Wayland proxies. Thin protocol callbacks translate events into those state objects, and `WaylandWindow` performs the resulting proxy operations.

## Give each value one owner

Important application state has one authority:

- Scintilla documents own text, selection, caret, undo history, and scroll positions.
- `ApplicationEditor` owns the production Scintilla host, retained documents, rendering, editor work deadlines, and editor-facing clipboard and text-input state.
- `DocumentWorkspace` owns tabs, paths, file operations, portal request intents, and dirty-close policy.
- `WaylandWindow` owns the display connection, Wayland and EGL objects, external services, input transport, scaling, and frame submission.
- `ApplicationUi` owns chrome and overlay selection state, builds one `ApplicationLayout` snapshot per event or paint pass, and routes pointer events with an explicit owner. The shell still applies keyboard routing and composition for now.

Components exchange copied values, stable identifiers, and explicit results instead of retaining pointers across unrelated lifetimes.

## Reject stale asynchronous results

Clipboard reads, primary-selection reads, IME batches, portal replies, and file-dialog results may arrive after focus, the active document, or the target tab changes. Requests carry an identity or generation that allows the receiving concern to reject a late result. Cancellation, unavailability, malformed data, size limits, timeouts, and superseded requests are explicit outcomes.

## Keep platform transport separate from product policy

The Wayland shell moves events and external-service data. It does not decide whether a dirty document may close, how tabs are ordered, which menu action is active, or how a file error is presented. Those decisions belong to application components that can be tested without a compositor.

## Use one event wait

The single-threaded application combines the Wayland display, selection transfers, D-Bus watches, key repeat, and editor work in one poll plan. Each source retains its ready callback, and the timeout is the earliest active deadline. This keeps ordering visible and avoids nested loops or busy waits.

## Keep coordinate spaces distinct

Editor layout remains in logical top-left coordinates. Scaling occurs at the rendering and Wayland boundaries. Buffer damage and EGL swap damage use different origins and are converted immediately before the relevant request. A scale change updates the buffer, viewport, renderer, damage history, cursor resources, and protocol rectangles as one coherent change.

## Share layout work

Text measurement, wrapping, hit testing, selection, caret placement, and drawing consume the same cached shaped runs. Painting does not independently measure text. Application chrome hit testing and painting share one `ApplicationLayout` snapshot for the frame size, chrome models, and editor-owned client and scrollbar metrics.

## Test adverse event order

Most behavior is exercised through deterministic state and application tests without a compositor. Tests cover removal, replacement, cancellation, stale replies, partial transfers, timeout, scaling, and frame ordering. Live Wayland checks confirm integration with a real compositor but do not replace deterministic coverage for cases that are difficult or unsafe to induce live.

## Keep unsupported behavior visible

The project does not make an unavailable operation appear successful. Optional services have explicit fallbacks, unsupported popup requests remain observable stubs, and current layout and product limitations are documented. New abstraction is justified only when it makes a current application boundary clearer.
