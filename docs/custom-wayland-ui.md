# Lessons for custom Wayland UI applications

This document records lessons from building a custom Wayland user interface without GTK, Qt, or another general-purpose GUI toolkit. The goal is not to turn scalpel-editor into a reusable framework. It is to preserve constraints, patterns, tests, and warnings that can help when building other direct Wayland applications.

A useful lesson explains a recurring problem, identifies the rule it protects, states the conditions under which it applies, and is supported by difficult event ordering or failure cases. A project-specific class name is only an example; the protected rule is the reusable part.

The concrete implementation behind these observations is described in [wayland.md](wayland.md) and [rendering.md](rendering.md).

## Judge direct UI code by the right criteria

Generic reuse is only one possible measure of quality. Application-specific code can still demonstrate broadly useful design when it makes protocol ordering, ownership, failure, and change boundaries clear.

Useful criteria include:

- Protocol correctness: required request and event ordering is visible and testable.
- Ownership clarity: creation, mutation, invalidation, and destruction have identifiable owners.
- Control of asynchronous event ordering: late or superseded work cannot affect a new target.
- Failure behavior: removal, cancellation, malformed input, and unavailable services have deliberate outcomes.
- Testability: most lifecycle behavior can be driven without a live compositor.
- Locality of change: a product feature touches its component, one composition boundary, and focused tests.
- Single sources of truth: layout, focus, scale, document state, and frame permission are not calculated independently in several places.
- Proportional work: redraw, measurement, transfer, and deadline handling remain bounded by the event that caused them.
- Honest scope: unsupported features and assumptions are explicit.

## Organize around lifecycles, not protocol wrappers

Wrapping each Wayland proxy in a similarly shaped object does not by itself make lifecycle behavior clear. Different concerns have different invalidation rules: a seat may disappear during key repeat, an output may disappear during a frame, a clipboard offer may be replaced during a transfer, and a text-input object may become invalid on focus loss.

A useful split is:

- Thin callbacks translate protocol events into copied values.
- Plain state objects decide transitions without owning proxies.
- One shell owner creates and destroys proxies and applies the requested transitions.
- The application receives narrow values and results rather than protocol objects.

This keeps protocol ownership centralized while allowing lifecycle decisions to be tested as ordinary state transitions.

The rule being protected is: protocol objects and the state derived from them must become invalid together, in a defined order.

## Treat global removal as normal operation

Wayland globals are capabilities announced by the compositor, not permanent process-wide fixtures. Classify each global as required or optional and define removal behavior before relying on it.

- Losing a required global should lead to a controlled shutdown when the application can no longer keep its main surface valid.
- Losing an optional global should destroy its dependent objects, cancel affected work, clear derived state, and select a visible fallback.
- A replacement announcement should create fresh state rather than silently reusing serials, offers, focus, or callbacks from the previous object.
- Child proxies must be destroyed before their parent manager or seat.

This applies broadly to input, clipboard, text input, cursor services, decoration, presentation, scaling extensions, and portal parenting.

## Keep transport separate from product policy

The platform shell should move events and external-service data. It should not decide what the application means by close, save, dirty, active tab, modal interaction, or file failure.

For example, an xdg-toplevel close event is a transport request. Whether it immediately exits, starts a dirty-document prompt, or walks several documents is application policy. Required-global loss is different: the platform path is no longer usable, so forced shutdown may bypass normal product policy.

This separation makes product workflows testable without creating a Wayland connection and prevents platform callbacks from accumulating unrelated application decisions.

## Reject stale asynchronous results

Direct Wayland applications commonly combine protocol callbacks, file descriptors, D-Bus replies, timers, and application state. Results may arrive after focus, the active document, the selected tab, or the owning service changes.

Attach stable identity to work that outlives the initiating call:

- Use request IDs for portal and other external requests.
- Use document or model generations when a result must apply to the same content lifetime.
- Retain the target position or object identity when completion must not follow the current selection.
- Use protocol serials where the protocol defines ordering.
- Cancel work when its seat, manager, focus, surface, or consumer disappears.

Do not retain a raw pointer to an application object across asynchronous completion unless another object provides a clear lifetime guarantee.

The rule being protected is: a valid result for an old target must not be mistaken for a result for the current target.

## Use one explicit event wait

A custom application often needs to wait for more than the Wayland display:

- Wayland read and flush readiness;
- clipboard and primary-selection descriptors;
- D-Bus watches;
- editor or animation timers;
- compositor-configured key repeat;
- transfer deadlines; and
- D-Bus timeouts.

Build one poll snapshot containing active descriptors, their desired events, and their ready callbacks. Select the earliest active deadline as the timeout. Rebuild the snapshot after returning to the application loop.

Avoid nested event loops for dialogs or transfers. They hide ordering, complicate cancellation, and may allow application state to change while a caller assumes it is suspended.

Blocked Wayland flush recovery needs special attention: continue servicing the display and external descriptors, but do not let already-due application work create a zero-timeout busy loop.

## Separate invalidation from permission to submit

Application damage and compositor frame permission are different state:

- Invalidation says pixels need to change.
- Painting captures the damage for one attempted submission.
- A frame callback says the compositor permits another submission.
- Presentation feedback reports what happened to a submitted frame but does not grant permission for the next one.

Damage must continue accumulating while a frame callback is outstanding. Invalidation raised during painting belongs to the next frame. Cancelling a paint must restore its captured damage.

When buffer age is available, repaint damage includes the history needed to reconstruct the selected back buffer. Missing or unreliable buffer age should select a full repaint. Missing swap-with-damage support should select a full swap without changing the application's logical damage model.

The rule being protected is: compositor pacing may delay drawing, but it must never lose application invalidation.

## Keep coordinate spaces named and separate

Custom-rendered Wayland clients commonly cross several coordinate spaces:

| Space | Typical unit and origin |
| --- | --- |
| Application layout | Logical pixels, top-left origin |
| Wayland surface | Logical surface units, top-left origin |
| Render buffer | Integer pixels, top-left application view |
| OpenGL framebuffer | Integer pixels, bottom-left native origin |
| Wayland buffer damage | Buffer pixels, top-left origin |
| EGL swap damage | Buffer pixels, bottom-left origin |
| Cursor image | Theme-buffer pixels plus an image-pixel hotspot |

Name the space in types, functions, or variable names when values cross a boundary. Convert immediately before the API that requires the new space. Round damage outward so scaling cannot omit changed edge pixels, then clip it to the actual buffer.

Pointer input, application layout, and text-input cursor rectangles should remain logical. Scaling them early invites double scaling and makes hit testing disagree with painting.

## Apply scale changes as one configuration

Fractional scaling is not an integer `wl_surface.set_buffer_scale` value. It combines a buffer sized for the preferred fractional scale with a logical destination supplied through viewporter. Use fractional scaling only when the complete required protocol pair is available; otherwise use integer scaling.

A scale change should coherently update:

- logical and buffer dimensions;
- surface buffer scale or viewport destination;
- EGL native-window size;
- renderer viewport and projection;
- damage conversion;
- buffer-age history;
- cursor theme size and hotspot;
- protocol rectangles; and
- full-frame invalidation.

Apply the configuration before the next paint. A scale change should not independently multiply application layout, pointer coordinates, or caret geometry.

## Treat input protocol frames as transactions

Some input protocols deliver one logical event through several callbacks. Pointer-axis source, high-resolution values, compatibility values, stop events, and direction belong to one `wl_pointer.frame`. Accumulate them and emit one application scroll event at the frame boundary.

Prefer the most precise representation offered in the current frame and do not count compatibility values a second time. Retain the older immediate path only for protocol versions that do not provide framed events.

Keyboard composition and repeat also have lifecycles:

- Build compose state from the user's locale and retain direct text fallback.
- Bypass composition for command-modified keys.
- Cancel incomplete composition on focus or keymap replacement.
- Use the compositor's repeat rate and delay.
- Check key repeatability through xkbcommon.
- Cancel repeat on release, focus loss, device loss, keymap replacement, or seat removal.

## Keep clipboard and primary selection independent

Clipboard and primary selection can share stateless MIME ranking and bounded byte-transfer machinery, but they have different ownership, offers, serials, cancellation, and user interaction.

Each transfer should own one nonblocking descriptor, a byte limit, a deadline, and exactly one completion result. Partial reads and writes, `EAGAIN`, EOF, peer failure, cancellation, timeout, and excessive size are ordinary states.

Do not clear or replace primary ownership merely because a middle-button paste begins. Retain the requested paste position so delayed text does not follow later pointer movement.

## Batch text input at the protocol boundary

text-input-v3 delivers pre-edit, commit, surrounding deletion, and completion as a batch. Copy the individual events, validate their ordering and serial, and publish application operations only at `done`.

Surrounding text should be bounded, UTF-8 aligned, and associated with the current selection and focus. Tentative pre-edit text should remain distinguishable from committed document edits so cancellation and undo behave correctly.

Loss of keyboard focus, seat, surface focus, or the text-input manager must cancel stale composition and prevent a later batch from changing the document.

## Share text layout between measurement and drawing

A custom renderer must not let text measurement and text drawing form separate interpretations of the same bytes. Cache a shaped result that contains glyphs, clusters, advances, input-byte positions, caret stops, direction, and font fallback choices.

Use that result for width measurement, wrapping, hit testing, selection, caret placement, and drawing. This is especially important for UTF-8 editor APIs whose positions are byte offsets rather than code-point indices.

Extraction into a reusable text component is optional. The reusable lesson is that every consumer must agree on cluster boundaries and advances.

## Compose a small custom UI explicitly

A fixed application UI does not require a widget hierarchy. Concrete components can own layout, hit testing, interaction state, and paint operations while one coordinator owns composition and input priority.

Useful rules include:

- Build one layout snapshot from current size and model state.
- Use the same snapshot for painting and hit testing.
- Represent pointer capture or active interaction ownership explicitly.
- Define modal and overlay priority in one place.
- Treat dismissal clicks deliberately so they do not also activate underlying content.
- Keep permanent chrome damage separate from transparent overlays that require full-frame repaint.
- Route menu activation and keyboard shortcuts through the same action dispatcher.

If adding one control requires changing several unrelated priority ladders or duplicating layout calculations, the composition boundary needs attention.

## Make difficult behavior deterministic

Live compositor tests are useful integration checks, but they are poor tools for exhaustively inducing partial I/O, global removal, exact timeout edges, stale replies, scale changes during an outstanding frame, or focus loss during composition.

Move lifecycle decisions into plain state and inject clocks where timing matters. Exercise rendering offscreen through the production renderer. Use pipes for transfer tests. Compose concern-level states in a small number of cross-concern tests.

Live checks should report only behavior actually exercised by the available compositor and input environment. Protocol advertisement alone does not prove the path was driven.

## Decide what kind of reuse a lesson supports

There are three useful outcomes:

- Reusable code: a narrow component with a stable contract, such as bounded byte transfer or deadline selection.
- Reusable pattern: an application-specific implementation that demonstrates explicit lifecycle state, stable asynchronous identity, or coherent scale application.
- Reusable warning: a documented approach that creates invalid ordering, hidden failure, duplicated authority, or unnecessary framework code.

Before extracting code, imagine replacing the application's content model with another one, such as an image viewer or terminal. Protocol ordering, frame pacing, scaling, transfer, and input-lifecycle rules usually remain. Dirty-document policy, tab behavior, and product chrome usually do not.

Extract only when multiple real consumers need the behavior and the resulting contract is simpler than the separate implementations.

## Recording future lessons

Add a lesson when implementation or testing exposes a rule that is useful beyond this editor. Record:

1. The recurring problem.
2. The rule or invariant that prevents it.
3. The conditions under which the rule applies.
4. The failure or difficult event ordering that supports it.
5. Whether the result is reusable code, a reusable pattern, or a warning.

Keep phase chronology, commit sequences, and project history out of this document. The purpose is accumulated engineering knowledge that remains useful as both this editor and other custom Wayland applications evolve.
