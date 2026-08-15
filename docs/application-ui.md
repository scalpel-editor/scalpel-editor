# Application UI

The application UI is a fixed composition of the Scintilla editor, a menu bar, a tab strip, an optional find bar, scrollbars, modal cards, and one active overlay. `ApplicationUi` coordinates those concrete parts without depending on Wayland and without introducing a general control framework.

## Ownership

| Owner | Responsibility |
| --- | --- |
| `WaylandWindow` | Display connection, Wayland and EGL objects, external services, input transport, scaling, frame submission, toplevel title, and waiting. |
| `ApplicationEditor` | Scintilla documents, editor input, rendering, damage, editor work deadlines, editor client geometry, scrollbar visibility and ranges, clipboard values, text-input state, wrapped plain-text find, and the process-wide generic editor text face. |
| `DocumentWorkspace` | Tabs, paths, file operations, application dialog intents, recent-path outcomes, startup path-list loading, and dirty-close policy. |
| `ApplicationSession` | Process-lifetime startup and exit-status policy for interactive and pathname launches, independent of the compositor. |
| `ApplicationUi` | Chrome models and painters (including the find bar and context menu), top-chrome inset, modal-card and error state, hover and press state, scrollbar interaction, input priority, cursor choice, overlay selection, window title from the active tab label, application layout snapshots, recent-file updates, and conversion of workspace work into host effects (including context-popup show/close/invalidate). |
| `WaylandApplicationRunner` | Construction and the platform pump. It applies session startup, moves copied events and external-service results across the boundary, performs host effects (including the grabbed context-menu `xdg_popup`), copies the current window title onto the toplevel, submits frames, waits, and returns a typed termination reason. |
| `main.cxx` | Command-line parsing, help, version, and usage reporting, invoking the Wayland runner, mapping the termination reason through session policy, and returning the process status. |

`ApplicationUi` receives references to `ApplicationEditor`, `DocumentWorkspace`, and `RecentFiles`; it does not replace their ownership. The Wayland runner keeps those objects alive in dependency order and contains no application input-priority or overlay-selection policy. The top-chrome inset (menu plus tab strip, plus the find bar when visible) is established by `ApplicationUi`, not by the runner or `main.cxx`.

## Event flow

One platform-loop iteration has this application order:

1. The Wayland runner takes copied presentation, clipboard, primary-selection, text-input, and portal results from `WaylandWindow`.
2. Portal results are translated from platform request IDs to application dialog IDs and delivered through `ApplicationUi`. Clipboard results and text-input batches also enter through `ApplicationUi`, which chooses the editor or find field as owner. Primary-selection traffic still targets `ApplicationEditor` directly.
3. Window-close, size, focus, pointer, and keyboard changes enter `ApplicationUi`. It applies application transitions immediately. Only an unconsumed pointer event crosses from `ApplicationUi` to `ApplicationEditor`; keyboard delivery is completed inside `ApplicationUi`.
4. `ApplicationUi::TakeShellEffects` drains workspace requests and outcomes plus queued context-menu popup effects. UI-local work is completed there, while portal-dialog, accepted-close, and context-popup work is returned to the runner.
5. The runner starts requested portal dialogs, records the platform-to-application dialog ID mapping, feeds startup failure back through `ApplicationUi`, and creates, paints, or destroys the context-menu popup.
6. After editor work and application synchronization, `ApplicationUi` selects the overlay and cursor. The runner copies `CurrentWindowTitle` (the active tab label, including the dirty marker) to `xdg_toplevel.set_title`, transfers editor damage to `WaylandWindow`, and may paint an independent popup surface.
7. When the compositor permits a frame, `ApplicationUi` retains one frame layout while `ApplicationEditor` paints the editor, permanent chrome, and active overlay. The runner then submits the frame and returns to the shared event wait.

Focus loss is one `ApplicationUi` transition: it cancels editor focus and tentative IME, closes the menu bar and context menu, cancels scrollbar interaction, clears modal press state, and blurs the find field without closing the bar or discarding its query. Opening a menu, context menu, or modal card also cancels tentative IME and blurs the find field. While a modal card, menu bar, or context menu owns input, `ChromeOwnsInput` tells the platform adapter to discard compositor IME batches; protocol conversion remains outside the UI.

## Interaction and overlay priority

`ApplicationUi::HandlePointer` is the only application pointer-priority decision. It resolves one owner in this order:

1. File-error card.
2. External-change confirmation.
3. Large-file confirmation.
4. Unsaved-changes prompt.
5. Active scrollbar drag.
6. Editor selection capture.
7. Open menu bar, including an outside click that dismisses it.
8. Open context menu (popup-local coordinates, or a toplevel outside press that dismisses without click-through).
9. Visible find bar band (field, Previous, Next, Close, or empty chrome).
10. Permanent chrome: menu bar, tab strip, or a scrollbar hit.
11. Editor client (including a right press that opens the context menu when eligible).

Modal owners consume pointer input. Editor selection capture and surface leave remain deliverable to `ApplicationEditor` so Scintilla can finish its interaction. A click that dismisses a menu does not also activate the control underneath it. A pointer press in the editor client while the find bar is visible leaves the bar open but transfers keyboard focus back to the editor. Each routing entry point applies actions, invalidation, and interaction cleanup before returning.

The same coordinator chooses the pointer cursor. Chrome and modal interaction select the arrow; editor interaction defers to the Scintilla cursor. `CurrentPointerCursor` also forces the arrow when a modal appears without a pointer event, so the Wayland runner only applies the resolved choice.

Keyboard priority is file-error card, external-change confirmation, large-file confirmation, unsaved-changes prompt, open context menu, Shift+F10 context-menu open, menu-bar navigation or accelerator, the global Find action (`Ctrl+F` / Edit > Find), a focused find field for query editing and field-local clipboard or select-all keys, other application shortcuts or tab cycle, then editor input. Unmatched application shortcuts (for example Save or Close Tab) and Ctrl+Tab still run while the find field is focused; field-local Ctrl+A/C/X/V stay on the query. Overlay paint priority is file-error card, external-change confirmation, large-file confirmation, unsaved-changes prompt, open menu bar dropdown, then no overlay. The context menu paints on its own popup surface rather than the in-window overlay. A higher-priority modal closes an open menu or context menu, and every overlay change invalidates the full frame. When a higher-priority card becomes active, any armed press on a lower-priority card is cleared so a later release cannot activate it.

## Layout and paint authority

`ApplicationEditor` remains the authority for the Scintilla client rectangle and scrollbar visibility, ranges, and positions. Individual concrete components calculate their own rectangles from their models. `ApplicationUi` combines those values with the logical frame size into `ApplicationLayout`, which contains menu, tab, find-bar, scrollbar, client, and modal-card layouts. Modal cards compress into short or narrow frames and keep their button rectangles inside the card. The top-chrome inset is `MenuBarHeight + TabStripHeight`, plus `FindBarHeight` while the find bar is visible; `ApplicationUi` applies that inset through `ApplicationEditor::SetTopChromeInset`.

Every pointer event builds one immutable layout value after refreshing model values used by hit testing. All owners considered for that event read that value. Painting uses a separate frame snapshot: `BeginFrameLayout` refreshes menu enablement and the selected Font radio, clamps tab scrolling, and retains one `ApplicationLayout`; both permanent-chrome and overlay painters read it until `EndFrameLayout`. Hit testing and painting therefore never run separate component layout calculations within the same event or frame.

## Find bar

The find bar is a third opaque top-chrome band below the tab strip. `FindBar` is one concrete model/layout/input/painter component; `ApplicationUi` owns visibility, the process-lifetime query, incremental origin, and translation of typed requests into searches and close. There is no control base class, focus manager, or registration system.

`Ctrl+F` and Edit > Find share `OpenFindBar`: they show the bar when hidden, focus the field, select the retained query, and capture an incremental origin in the active document. When the query is empty and the editor has a non-empty, single-line selection of valid UTF-8, that selection seeds the field. Closing (Escape or the Close control) cancels preedit, restores the base inset, and leaves the last document match selected.

Search is case-insensitive plain text. Committed query changes run an incremental forward search from the captured origin so extending the query does not skip past the match for the shorter query. Enter and Next search forward from the end of the current editor selection; Shift+Enter and Previous search backward from its start. Both directions wrap once. Empty queries do not search. Status is clear after a first-range match, `Wrapped` after a wrap match, and `No matches` after a failed search. Search changes only the editor selection and scroll position, not document bytes, undo, or save state. The origin is document-qualified and is reset on focus gain and active-document change.

While the find field is focused, direct keyboard text, text-input batches (commit, preedit, delete-surrounding), and clipboard cut/copy/paste operate on the query. `ApplicationUi` assigns shell-facing clipboard request IDs, maps editor-local IDs for document paste, and ignores a late find paste after focus loss or close so it cannot mutate the document or a superseded query. When the bar is visible but unfocused, text-input and editor clipboard paths still target the editor. Opening, closing, focus transfer, and query edits dirty the text-input client state so the compositor receives updated surrounding text and caret geometry (including the dynamic top-chrome inset).

## Editor font menu

The permanent menu bar headings are File, Edit, Font, and Recent in that order. Font opens with Alt+T so it does not conflict with Alt+F for File. Left and Right cycle all four headings.

Font offers exactly four process-lifetime choices. The menu stores and displays a typed generic selection, never a concrete installed family or file path:

| Menu label | Canonical family value |
| --- | --- |
| Monospace | `monospace` |
| Serif | `serif` |
| Sans | `sans-serif` |
| System | `system-ui` |

Startup default is System, matching `Platform::DefaultFont()`. `ApplicationEditor` owns the choice as view state: changing it updates `STYLE_DEFAULT`, copies it through every style, applies the fixed Markdown palette, and restores the monospace line-number gutter. It does not alter document bytes, save-point state, undo history, selection, or per-document switching state. Styles belong to the one editor view, so the face applies across every retained document for the process.

Markdown token presentation is view state, not document state. Ordinary text stays on the selected body face. Strong text and headings are bold; emphasis is italic. Code uses a monospace face and a restrained background. Links, quotes, list markers, rules, and strikeout tokens have distinct colours. Point size is unchanged. The same path runs at startup and after every Font menu choice so the palette cannot be erased.

Display size is adjusted with view zoom, not a separate font-size setting. When the editor owns the keyboard event, `Ctrl+=` increases zoom by one point, `Ctrl+-` decreases it by one point, and `Ctrl+0` resets it to zero. The same commands also bind to the keypad keys `Ctrl+Add`, `Ctrl+Subtract`, and `Ctrl+Divide`. Zoom is view state: it applies across every retained document for the process, does not change document bytes, undo history, save state, selection, or the chosen generic font family, and is not persisted across process restarts. Shortcut zoom is clamped to about -10 through +60 points.

Fontconfig resolves the canonical family through the host configuration when the renderer loads a face. Preference persistence, arbitrary family entry, installed-font discovery, and per-document fonts are out of scope.

`ApplicationUi` binds the permanent-chrome and overlay painter callbacks and unbinds callbacks that capture it when it is destroyed. Permanent chrome can use bounded damage. A transparent menu or modal overlay expands damage to the full frame and selects a full swap so old blended pixels cannot remain.

## Document open and save

`DocumentWorkspace` opens and saves document files as raw bytes. A readable regular file succeeds even when its contents are not valid UTF-8: it becomes a normal clean document, is recorded as a successful recent path, and does not enqueue a file error or warning. Open and save do not validate, replace, transcode, or normalize invalid sequences. Valid UTF-8 and invalid bytes may coexist in one document. Directories, devices, fifos, and other non-regular paths fail as ordinary open errors; they must not throw or leave the workspace half-updated.

Whole-file document loads are hard-capped at 256 MiB (`DocumentFileHardLimitBytes`). The bound is enforced from the size reported by `fstat` and again while reading, so a file that grows past the allowance during the read is still refused. A rejected oversized open does not create a tab, change the active document, or add the path to Recent. Interactive hard-limit failures present as `File is too large` rather than the generic open error.

Interactive Open and Recent first try the 64 MiB warning threshold (`DocumentFileWarningThresholdBytes`). Files at or below that size open normally. Files above it queue a two-choice large-file confirmation card (title `File larger than 64 MiB`, path, Open / Cancel) instead of creating a tab. Open retries under the 256 MiB hard limit; Cancel drops the path. Multi-path Open keeps remaining paths and resumes them in order after each choice. The large-file decision does not overlap a dirty-close prompt: either modal owns tab, dialog, save, and close actions until it finishes. Escape cancels; Enter or Space activates the focused choice; Tab and arrow keys move focus between Open and Cancel.

A successful open or save records a file stamp (device, inode, byte size, and nanosecond modification time) for the followed regular file that supplied or received the bytes. A later Save to that tab's currently bound path compares the destination against the remembered stamp inside the atomic writer. When the destination is missing, replaced, resized, retimed, or uninspectable, the write does not run and the workspace queues an external-change card (title `File changed on disk`, path, Overwrite / Reload / Save As / Cancel). Overwrite writes without an expected stamp. Reload rereads under the 256 MiB hard limit into the same tab through the clean buffer-load path. Save As queues a dialog for the captured tab. Cancel leaves the dirty buffer and baseline stamp unchanged. Detection runs only at the save boundary; the editor does not watch files, poll on focus, or check on each keystroke. Save As to a different path remains an explicit unconditional replacement and then binds the returned stamp. The final destination stat and rename are not a perfect cross-process compare-and-swap on ordinary POSIX files; another writer can still race the last check.

When a dirty-close Save finds a conflict, the external-change card keeps the initiating close prompt generation. Successful Overwrite or Reload completes or advances that close; successful Save As does the same through the captured generation; Cancel or a failed resolution leaves the close prompt active. A failed overwrite or reload queues a file-error card above the still-pending conflict so dismissing the error returns to the decision. Escape cancels; Enter or Space activates the focused choice; Tab and arrow keys cycle the four buttons.

Process startup may load an ordered path list through `LoadStartupFiles`. Distinct paths become tabs in argument order; the first reuses the pristine initial document, later paths create sibling documents, and the tab named by the last supplied path becomes active. Startup paths are bound for save and are not recorded as recent files, so temporary editor paths such as Git's `COMMIT_EDITMSG` stay out of the Recent menu. Every distinct path is read before any tab mutation; an empty, unreadable, or oversized path leaves the initial workspace coherent and fails the process startup. Oversized startup paths print a distinct `file too large` diagnostic on stderr. Command-line paths are treated as explicit intent for the hard limit and do not use an interactive size warning. Interactive Open and Recent continue to record successful paths.

The recent-files state file is loaded with a 1 MiB bound enforced during the read, so a corrupt or hostile state file cannot allocate more than that maximum.

Lexically normalized paths identify open tabs. Open selects an existing binding instead of creating a duplicate. Save As refuses a destination already bound to another tab so two buffers cannot claim one path and overwrite each other on save.

LF is the editor's native line ending: Enter inserts LF, including after a document has been converted to CRLF. Open and Save neither detect nor change line endings, so untouched bytes and any mixed endings remain as they are. The Edit menu provides `Convert Line Endings to LF` and `Convert Line Endings to CRLF` as explicit whole-document edits; either conversion is one undo action and marks the document modified when bytes change.

Saving an unchanged document writes its content bytes exactly. After an edit, bytes outside the edited range remain exact; normal text input continues to insert UTF-8. How invalid bytes behave under caret movement, deletion, and painting is the editor core rule in [scintilla-core.md](scintilla-core.md) (Text and layout contract); the workspace does not reimplement that logic.

Invalid UTF-8 in file contents is separate from path encoding, I/O failures, line-ending policy, external-change conflicts, oversized-file limits, and the clipboard, primary-selection, and IME paths, which reject malformed UTF-8 at those external text-transfer boundaries.

## Workspace work and host effects

`DocumentWorkspace` records product decisions without calling the platform. `ApplicationUi::TakeShellEffects` consumes them according to where the work belongs:

| Workspace work or outcome | `ApplicationUi` action |
| --- | --- |
| Prompt began | Activates modal input state for a dirty-close, large-file, or external-change card, closes the menu, cancels IME and scrollbar interaction, blurs the find field, and resets card focus and armed presses. |
| Refresh tabs | Rebuilds the tab-strip model and invalidates top chrome. |
| Recent path | Records and persists the recent-file list, then refreshes the menu model. |
| File error | Queues and activates the file-error card, outranking external-change, large-file, and unsaved cards and clearing any armed press on them. |
| Show Open or Show Save As | Returns a typed dialog effect containing a stable application dialog ID and copied document path. |
| Accept close | Returns a typed close effect. |
| Context menu open / close / invalidate | Returns typed popup effects with parent-relative anchor and grab serial for show; close and invalidate drive popup lifecycle without recreating dialog state. |

Only the Wayland runner turns those typed effects into portal requests, accepted-close termination, or context-menu popup create/paint/destroy. Portal request IDs remain platform details: the runner maps each one to the application dialog ID and returns only that application ID with success, cancellation, or startup failure. `ApplicationUi` passes the result to the captured workspace intent, so a late result cannot silently target whichever tab is active at that time.

## Scope and extension

The boundary is intentionally application-specific:

- There is no widget tree, control base class, registration system, generic event dispatcher, or reusable GUI toolkit.
- `ApplicationUi` does not own Wayland objects, portal request IDs, input-protocol conversion, frame submission, or waiting.
- It does not recompute Scintilla client geometry or scrollbar metrics.
- Fixed `UiStyle` values are immutable painter-owned snapshots; runtime theme discovery is outside the current product.
- The editor context menu is an application-owned fixed list (Undo, Redo, Cut, Copy, Paste, Select All) opened by right-click in eligible text or Shift+F10 at the caret. It reuses `ApplicationAction` enablement and `DispatchApplicationAction`; it is not Scintilla's generic `Platform::Menu`. Layout and hit testing are popup-local. The shell maps typed show/close/invalidate effects to a grabbed `xdg_popup` child of the toplevel.
- Right-click inside the current selection preserves it; outside it places a single caret first. Margin right-clicks stay on the core notification path and do not open the text menu. File errors and unsaved prompts outrank and dismiss it. Focus loss, tab/document change, portal takeover, resize/scale, Escape, action activation, outside click, and `xdg_popup.popup_done` also close it. Bare F10 / Menu still open the menu bar.
- Scintilla autocomplete and call-tip popup surfaces remain explicit unsupported production paths. Production stubs keep in-memory list state and never report a created window; cancel must clear active state without a window id.
- Find is the one UI-local application action: menu activation and `Ctrl+F` share `OpenFindBar` before the existing dispatcher. Other actions still dispatch through `DocumentWorkspace` and `ApplicationEditor`.

Production composition is exercised through `applicationUiTest` with an offscreen `ApplicationEditor`, so input, layout, overlays, portal workflows, and rendering can be tested without opening a Wayland connection. Live Wayland runs check the platform integration rather than replacing those deterministic cases.
