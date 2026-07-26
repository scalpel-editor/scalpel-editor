# Phase 8 recent files and file errors

## Goal

Add a compact top-level Recent menu to the in-window menu bar, persist a bounded most-recently-used path list, and give failed document reads and writes visible in-window feedback.

## Product behavior

- Recent contains at most ten paths, newest first. A successful Open, selection of an already-open path, or successful Save As promotes that normalized path. Ordinary Save does not change the order.
- Entries are compared after lexical normalization, matching `DocumentWorkspace`; paths are not resolved through symlinks and are not probed at startup, so temporarily unavailable storage remains represented.
- Each row shows the file name before its parent directory. The final separated row clears the list without closing documents. An empty list shows one disabled placeholder.
- Alt+R opens Recent. Pointer press/release, Up/Down/Enter, Escape, and Left/Right menu cycling use the same `MenuBarItemId` path as fixed menu actions.
- State lives at `$XDG_STATE_HOME/scalpel-editor/recent-files`, falling back to `$HOME/.local/state/scalpel-editor/recent-files`. The file is versioned and NUL-delimited so newline-containing Unix paths round-trip. Writes reuse the document layer's atomic replacement and private new-file mode.
- A failed Open or Save queues a `DocumentFileError`. The shell presents errors in order with a dismissible card; Return, Space, Escape, or the OK button dismisses one error. File errors own input and the overlay above an unsaved-changes prompt, which remains active underneath and returns after dismissal.

## Ownership

`RecentFiles` owns MRU ordering, the entry limit, serialization, XDG state-path selection, and persistence. It does not open documents or draw menus.

`DocumentWorkspace` continues to own document paths and file operations. It publishes successful paths and failed file operations as drainable outcomes, but does not persist recent state or draw errors.

`MenuBar` owns dynamic Recent row identity, labels, layout, hit testing, painting, and pointer and keyboard transitions. Fixed `ApplicationAction` values remain the shared path for File and Edit shortcuts and menu activation; recent paths are not forced into that fixed dispatcher.

`FileErrorCard` owns Wayland-free card layout, hit testing, and paint. `main` owns the pending error queue, recent-file state instance, outcome collection, dynamic activation, and overlay priority.

## Commit sequence

1. Persist a bounded recent-file list.
2. Report successful document paths and failed file operations.
3. Add dynamic Recent menu rows.
4. Add a file-operation error card.
5. Wire recent files and file errors into the shell.
6. Complete documentation and verification.

## Completion checks

The focused recent-file, workspace-outcome, menu-bar, and error-card tests cover ordering, normalization, state corruption, unusual paths, successful and failed document operations, dynamic layout, pointer and keyboard activation, stale entries, error paint, promotion, persistence, and clearing. Changed production headers compiled alone, and `cmake --workflow --preset dev` passed all 33 tests. A live Wayland process stayed up for the five-second smoke window; interactive Recent selection and error-card dismissal were not automated against the live compositor and are covered by the headless workflow and card tests. The sanitizer matrix remains reserved for the Phase 8 gate.
