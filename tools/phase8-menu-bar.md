# Phase 8 menu bar plan

## Goal

Add a compact in-window File / Edit menu bar above the tab strip. It exposes the application and editing actions that already exist, gives them one tested command path shared with keyboard shortcuts, and remains part of the bespoke Phase 8 chrome rather than introducing a general UI toolkit.

The first menu bar deliberately contains only actions the editor can perform now. Find belongs with the later find-bar work; View, Help, recent files, configurable menus, context menus, and plugin-provided actions are out of scope.

## Current boundary and deficiencies

`ApplicationEditor` already reserves and paints one permanent top-chrome band, while `main` paints `TabStrip` at y=0 and intercepts its pointer input before Scintilla. `DocumentWorkspace` owns new, open, save, save-as, tab close, and window-close behavior. Scintilla already owns undo, redo, selection, cut, copy, paste, and select-all behavior, but `ScintillaBase::ExecuteCommand` is protected and the application host exposes only copy and paste requests directly.

Application shortcuts are currently a chain of predicates and direct workspace calls in `main`; editing shortcuts continue through Scintilla's key map. Adding menu callbacks beside those paths would let menu and keyboard behavior drift. A small compile-time application action list and one dispatcher should replace that duplication without becoming a general command framework.

The chrome boundary also assumes one band: `LayoutTabStrip` begins at the frame origin, `SetTopChromeInset` reserves only the tab height, and `InvalidateTopChrome` treats the whole inset as one rectangle. The menu bar needs the tab strip moved down while both remain one damage-aware permanent-chrome region.

The keyboard input type has `Keys::Menu`, but Wayland key translation does not currently map the Menu key or F10 to it. Bare modifier identity is not retained, so a reliable bare-Alt toggle cannot be added without widening the input contract. The initial keyboard contract will use F10/Menu and Alt+F / Alt+E, all of which can be represented exactly.

## Product behavior

- The permanent bar shows File and Edit above the tab strip. File contains New Tab, Open…, Save, Save As…, Close Tab, and Quit. Edit contains Undo, Redo, Cut, Copy, Paste, and Select All. Separators group related actions, and the existing shortcuts are shown beside their items.
- Ctrl+Q requests the same dirty-aware window close as the compositor close button. The existing Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+W, Ctrl+Z, Ctrl+Y, Ctrl+X, Ctrl+C, Ctrl+V, and Ctrl+A behavior remains unchanged and runs through the same action dispatcher used by menu selection.
- Save and Save As remain available for the active tab. Undo and Redo reflect the active document's history; Cut and Copy require a selection; Paste reflects the current clipboard offer and editor write state; Select All is available when the document is non-empty. Disabled items paint distinctly and cannot run.
- A left press on File or Edit opens that menu; pressing the open heading again closes it. Moving across headings while a menu is open switches menus. An item runs only after a matching left press and release. A press outside the bar and dropdown closes the menu and is consumed so it cannot also move the caret or activate a tab.
- F10 or the Menu key opens File and focuses its first actionable item. Alt+F and Alt+E open the named menu directly. Left and Right switch menus, Up and Down move across actionable items with wrapping, Enter activates, and Escape closes. Key releases and unrelated text input are consumed while a menu owns input.
- Opening a menu cancels tentative IME input, and IME batches are ignored until it closes. Editor pointer capture from an in-progress selection keeps motion and release until capture ends; the menu must not steal that release.
- Menu headings and dropdowns use arrow cursor feedback. Focus loss closes the menu. Resize and live scale changes recompute layout without losing the selected menu when it still fits.
- A menu action closes the dropdown before it changes application state. Open and Save As may then launch a portal; Close Tab or Quit may show the unsaved-changes card. The card has higher input and paint priority than the menu.
- Dropdowns are drawn inside the existing Wayland toplevel above tabs and editor content. This work does not add `xdg_popup` windows and does not change the later popup-window plan for autocomplete, call tips, or the context menu.

## Ownership and rendering

Add a focused `ApplicationAction` enum, static menu metadata, shortcut matching, current enabled-state calculation, and a dispatcher that calls `DocumentWorkspace` or narrow `ApplicationEditor` methods. The list is compile-time-known and application-specific. It does not support registration, callbacks owned by arbitrary controls, or runtime menu construction.

`ApplicationEditor` gains only the named edit operations and state queries the application menu needs. Their implementations call the already-retained Scintilla commands and queries; the application action layer does not duplicate editing behavior.

`MenuBar` owns Wayland-free layout, hit testing, open-menu navigation, and painting. Its transient model contains the open menu, focused and hovered item, and press origin. `main` owns one instance of that model, converts input events into model transitions, runs returned actions through the dispatcher, and performs the workspace's existing shell requests.

The permanent chrome painter draws the menu bar followed by the tab strip. `ApplicationEditor::SetTopChromeInset` reserves their combined fixed height, and `TabStrip` accepts its actual top position instead of assuming y=0. Full-frame pointer coordinates continue unchanged.

The open dropdown uses the existing post-paint overlay slot. Menus are brief and may use a translucent shadow, so the overlay path's full-frame repaint and full swap are acceptable while one is open. `main` binds either the menu dropdown or the unsaved card, never both; the unsaved card wins. Opening and closing invalidates the full frame so preserved buffer contents cannot leave a stale dropdown.

## Commit sequence

1. **Create the shared application action path.** Add the fixed File / Edit action enum and metadata, narrow editor edit operations and availability queries, shortcut matching, and one headless dispatcher over `DocumentWorkspace` and `ApplicationEditor`. Route all listed shortcuts through it and add dirty-aware Ctrl+Q without changing their outcomes. Add `applicationActionTest`; build it and run `"application actions*"`.
2. **Lay out, hit-test, and paint the menu bar.** Add fixed logical dimensions, heading and dropdown rectangles, separator and disabled rows, label and shortcut columns, narrow-window clamping, hover/focus visuals, and deterministic offscreen paint checks. Keep this code independent of Wayland and shell callbacks. Add `menuBarTest`; build it and run `"menu bar*"`.
3. **Stack the permanent chrome.** Reserve `MenuBarHeight() + TabStripHeight()`, let tab-strip layout start below the menu bar, paint both through the existing permanent-chrome callback, and keep editor coordinates in full-frame space. Cover editor client geometry, bar-only damage, strip-only damage, overlay order, narrow resize, and framebuffer scaling; build `applicationHostTest`, `tabStripTest`, and `menuBarTest`, then run `"production editor top chrome*"`, `"tab strip offset*"`, and `"menu bar editor integration*"`.
4. **Add pointer-driven menu state.** Implement heading open/toggle/switch, item hover, matching press/release activation, disabled-item rejection, outside-click dismissal without click-through, leave handling, arrow cursor state, and the existing editor-capture exception. Bind the dropdown through the overlay painter only while open. Build `menuBarTest` and run `"menu bar pointer*"`.
5. **Add keyboard menu navigation.** Map XKB Menu and F10 to `Keys::Menu` after reading the local xkbcommon headers/source contract already used by `WaylandInput`; implement F10/Menu, Alt+F / Alt+E, arrows, wrapping, Enter, and Escape; suppress unrelated input while open. Extend `waylandKeyboardTest` and `menuBarTest`; run `"Wayland keyboard menu keys*"` and `"menu bar keyboard*"`.
6. **Keep action state current.** Calculate enabled items from the active document, selection, and clipboard offer whenever a menu opens or repaints; verify inactive-tab history cannot leak into the active tab's menu and delayed clipboard availability changes are reflected. Exercise undo, redo, cut, copy, paste, select all, portal request creation, tab close, and window close through menu actions in `applicationActionTest`; run `"application actions state*"` and `"application actions dispatch*"`.
7. **Integrate menus with modal and shell lifecycles.** Establish input priority as unsaved card, open menu, tab strip, then editor; cancel IME on open; close menus before portals, prompts, tab activation, force close, or focus loss; keep layout valid across resize and scale changes; and clear overlay state without stale pixels. Build `scalpel-editor` and run `"menu bar shell integration*"`, `"production editor menu overlay*"`, and the relevant `"document workspace*"` prompt cases.
8. **Complete the menu-bar workflow checks.** ✅ Combined `menu bar workflow*` covers keyboard open, pointer Open selection, edit-state refresh, portal Open, dirty Quit cancellation, and return to editor input. `ROADMAP.md` and application ownership comments describe the landed menu-bar boundary. Changed production headers compiled alone; `cmake --workflow --preset dev` passed all 31 tests. A live Wayland launch against the current session stayed up for several seconds with the menu bar and tab strip visible; interactive File/Edit open, keyboard navigation, command execution, scaling, and dirty Quit were not automated on this runner (those paths are covered by the headless menu-bar and application-action cases). The full sanitizer matrix waits for the Phase 8 gate.

## Completion checks

The work is complete when every visible item reaches the same tested action as its shortcut; enabled state follows the active document and clipboard; pointer and keyboard menus cannot leak input into tabs or editor content; opening a portal or dirty prompt closes the menu cleanly; the menu bar and moved tab strip remain correct under partial damage, resize, and scale changes; no Wayland popup or generic UI framework has been introduced; changed production headers compile alone; and the normal development workflow passes. The menu bar meets these checks as of commit 8; remaining Phase 8 chrome and file-lifecycle policy is later work.
