# Application UI

The application UI is a fixed composition of the Scintilla editor, a menu bar, a tab strip, scrollbars, modal cards, and one active overlay. `ApplicationUi` coordinates those concrete parts without depending on Wayland and without introducing a general control framework.

## Ownership

| Owner | Responsibility |
| --- | --- |
| `WaylandWindow` | Display connection, Wayland and EGL objects, external services, input transport, scaling, frame submission, and waiting. |
| `ApplicationEditor` | Scintilla documents, editor input, rendering, damage, editor work deadlines, editor client geometry, scrollbar visibility and ranges, clipboard values, text-input state, and the process-wide generic editor text face. |
| `DocumentWorkspace` | Tabs, paths, file operations, application dialog intents, recent-path outcomes, and dirty-close policy. |
| `ApplicationUi` | Chrome models and painters, modal-card and error state, hover and press state, scrollbar interaction, input priority, cursor choice, overlay selection, application layout snapshots, recent-file updates, and conversion of workspace work into host effects. |
| `main.cxx` | Construction and the platform pump. It moves copied events and external-service results across the boundary, performs host effects, submits frames, and waits. |

`ApplicationUi` receives references to `ApplicationEditor`, `DocumentWorkspace`, and `RecentFiles`; it does not replace their ownership. `main.cxx` keeps those objects alive in dependency order and contains no application input-priority or overlay-selection policy.

## Event flow

One platform-loop iteration has this application order:

1. `main.cxx` takes copied presentation, clipboard, primary-selection, text-input, and portal results from `WaylandWindow`.
2. Portal results are translated from platform request IDs to application dialog IDs and delivered through `ApplicationUi`. Clipboard, primary-selection, and permitted text-input batches go to `ApplicationEditor`.
3. Window-close, size, focus, pointer, and keyboard changes enter `ApplicationUi`. It applies application transitions immediately. Only an unconsumed pointer event crosses from `ApplicationUi` to `ApplicationEditor`; keyboard delivery is completed inside `ApplicationUi`.
4. `ApplicationUi::TakeShellEffects` drains workspace requests and outcomes. UI-local work is completed there, while portal-dialog and accepted-close work is returned to `main.cxx`.
5. `main.cxx` starts requested portal dialogs, records the platform-to-application dialog ID mapping, and feeds startup failure back through `ApplicationUi`.
6. After editor work and application synchronization, `ApplicationUi` selects the overlay and cursor. `main.cxx` transfers editor damage to `WaylandWindow`.
7. When the compositor permits a frame, `ApplicationUi` retains one frame layout while `ApplicationEditor` paints the editor, permanent chrome, and active overlay. `main.cxx` then submits the frame and returns to the shared event wait.

Focus loss is one `ApplicationUi` transition: it cancels editor focus and tentative IME, closes the menu, cancels scrollbar interaction, and clears modal press state. Opening a menu or modal card also cancels tentative IME. While a modal card or menu owns input, `ChromeOwnsInput` tells the platform adapter to discard compositor IME batches; protocol conversion remains outside the UI.

## Interaction and overlay priority

`ApplicationUi::HandlePointer` is the only application pointer-priority decision. It resolves one owner in this order:

1. File-error card.
2. Unsaved-changes prompt.
3. Active scrollbar drag.
4. Editor selection capture.
5. Open menu, including an outside click that dismisses it.
6. Permanent chrome: menu bar, tab strip, or a scrollbar hit.
7. Editor client.

Modal owners consume pointer input. Editor selection capture and surface leave remain deliverable to `ApplicationEditor` so Scintilla can finish its interaction. A click that dismisses a menu does not also activate the control underneath it. Each routing entry point applies actions, invalidation, and interaction cleanup before returning.

The same coordinator chooses the pointer cursor. Chrome and modal interaction select the arrow; editor interaction defers to the Scintilla cursor. `CurrentPointerCursor` also forces the arrow when a modal appears without a pointer event, so `main.cxx` only applies the resolved choice.

Keyboard priority is file-error card, unsaved-changes prompt, menu navigation or accelerator, application shortcut or tab cycle, then editor input. Overlay paint priority is file-error card, unsaved-changes prompt, open menu, then no overlay. A higher-priority modal closes an open menu, and every overlay change invalidates the full frame.

## Layout and paint authority

`ApplicationEditor` remains the authority for the Scintilla client rectangle and scrollbar visibility, ranges, and positions. Individual concrete components calculate their own rectangles from their models. `ApplicationUi` combines those values with the logical frame size into `ApplicationLayout`, which contains menu, tab, scrollbar, client, and modal-card layouts.

Every pointer event builds one immutable layout value after refreshing model values used by hit testing. All owners considered for that event read that value. Painting uses a separate frame snapshot: `BeginFrameLayout` refreshes menu enablement and the selected Font radio, clamps tab scrolling, and retains one `ApplicationLayout`; both permanent-chrome and overlay painters read it until `EndFrameLayout`. Hit testing and painting therefore never run separate component layout calculations within the same event or frame.

## Editor font menu

The permanent menu bar headings are File, Edit, Font, and Recent in that order. Font opens with Alt+T so it does not conflict with Alt+F for File. Left and Right cycle all four headings.

Font offers exactly four process-lifetime choices. The menu stores and displays a typed generic selection, never a concrete installed family or file path:

| Menu label | Canonical family value |
| --- | --- |
| Monospace | `monospace` |
| Serif | `serif` |
| Sans | `sans-serif` |
| System | `system-ui` |

Startup default is System, matching `Platform::DefaultFont()`. `ApplicationEditor` owns the choice as view state: changing it updates `STYLE_DEFAULT`, copies it through plain-text styles, and restores the monospace line-number gutter. It does not alter document bytes, save-point state, undo history, selection, or per-document switching state. Styles belong to the one editor view, so the face applies across every retained document for the process.

Display size is adjusted with view zoom, not a separate font-size setting. When the editor owns the keyboard event, `Ctrl+=` increases zoom by one point, `Ctrl+-` decreases it by one point, and `Ctrl+0` resets it to zero. The same commands also bind to the keypad keys `Ctrl+Add`, `Ctrl+Subtract`, and `Ctrl+Divide`. Zoom is view state: it applies across every retained document for the process, does not change document bytes, undo history, save state, selection, or the chosen generic font family, and is not persisted across process restarts. Shortcut zoom is clamped to about -10 through +60 points.

Fontconfig resolves the canonical family through the host configuration when the renderer loads a face. Preference persistence, arbitrary family entry, installed-font discovery, and per-document fonts are out of scope.

`ApplicationUi` binds the permanent-chrome and overlay painter callbacks and unbinds callbacks that capture it when it is destroyed. Permanent chrome can use bounded damage. A transparent menu or modal overlay expands damage to the full frame and selects a full swap so old blended pixels cannot remain.

## Document open and save

`DocumentWorkspace` opens and saves document files as raw bytes. A readable file succeeds even when its contents are not valid UTF-8: it becomes a normal clean document, is recorded as a successful recent path, and does not enqueue a file error or warning. Open and save do not validate, replace, transcode, or normalize invalid sequences. Valid UTF-8 and invalid bytes may coexist in one document.

Saving an unchanged document writes its content bytes exactly. After an edit, bytes outside the edited range remain exact; normal text input continues to insert UTF-8. How invalid bytes behave under caret movement, deletion, and painting is the editor core rule in [scintilla-core.md](scintilla-core.md) (Text and layout contract); the workspace does not reimplement that logic.

Invalid UTF-8 in file contents is separate from path encoding, I/O failures, line-ending policy, external-change conflicts, oversized-file limits, and the clipboard, primary-selection, and IME paths, which reject malformed UTF-8 at those external text-transfer boundaries.

## Workspace work and host effects

`DocumentWorkspace` records product decisions without calling the platform. `ApplicationUi::TakeShellEffects` consumes them according to where the work belongs:

| Workspace work or outcome | `ApplicationUi` action |
| --- | --- |
| Prompt began | Activates modal input state, closes the menu, cancels IME and scrollbar interaction, and resets card focus. |
| Refresh tabs | Rebuilds the tab-strip model and invalidates top chrome. |
| Recent path | Records and persists the recent-file list, then refreshes the menu model. |
| File error | Queues and activates the file-error card. |
| Show Open or Show Save As | Returns a typed dialog effect containing a stable application dialog ID and copied document path. |
| Accept close | Returns a typed close effect. |

Only `main.cxx` turns those typed effects into portal requests or process-loop exit. Portal request IDs remain platform details: `main.cxx` maps each one to the application dialog ID and returns only that application ID with success, cancellation, or startup failure. `ApplicationUi` passes the result to the captured workspace intent, so a late result cannot silently target whichever tab is active at that time.

## Scope and extension

The boundary is intentionally application-specific:

- There is no widget tree, control base class, registration system, generic event dispatcher, or reusable GUI toolkit.
- `ApplicationUi` does not own Wayland objects, portal request IDs, input-protocol conversion, frame submission, or waiting.
- It does not recompute Scintilla client geometry or scrollbar metrics.
- Fixed `UiStyle` values are immutable painter-owned snapshots; runtime theme discovery is outside the current product.
- Scintilla autocomplete, call-tip, and context-menu popup surfaces remain explicit unsupported production paths.

A new find bar should add one concrete model/layout/painter component and one rule at the `ApplicationUi` composition boundary. It should not require new priority checks in `main.cxx`, `ApplicationEditor`, or several existing controls.

Production composition is exercised through `applicationUiTest` with an offscreen `ApplicationEditor`, so input, layout, overlays, portal workflows, and rendering can be tested without opening a Wayland connection. Live Wayland runs check the platform integration rather than replacing those deterministic cases.
