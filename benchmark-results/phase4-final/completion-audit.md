# Phase 4 final completion audit

## Inventory

`tools/discoverability/check-corpus.sh` reports 60 rows with valid final targets, definition markers, and evidence files. `tools/check-message-inventory.sh` reports 853 entries matching `Scintilla.iface`. `tools/check-retained-entrypoints.sh` reports 782 retained callables, 41 deleted callables, 32 notifications, 782 message cases, 115 `EditorCommand` members, and 1,146 named-method index keys; every retained callable has a thin case and typed destination, and deleted callables have no dispatch case.

## Dispatch completion

A repository search for `.WndProc(`, `->WndProc(`, and `WndProc(` under `scintilla/src` and `scintilla/include`, excluding the two dispatch implementations and their declarations, returns no production call. The remaining definitions are `ScintillaBase::WndProc`, `Editor::WndProc`, `Editor::DefWndProc`, and their declarations. Their 782 cases only unpack parameters and forward to named methods or convert command messages to `EditorCommand`; the retained-entry audit parses and checks every case body.

## Named-path tests

The completed application-facing inventory in `MESSAGE_REMOVAL.md` names focused direct-path tests for wrapping, autocomplete and user lists, call tips, document text, line endings, selection, history, clipboard availability, target search, focus, scrolling, lexer attachment, recording, navigation, and zoom. Completion searches confirm those test files and their direct operations exist. Representative direct primary cases include `SetText replaces content and reports dirty state`, `Target range and SearchInTarget find text`, `CanUndo and CanRedo follow edits and history`, `Undo and Redo commands match named methods`, `GotoLine and GotoPos clamp and move caret`, `Scroll width and end-at-last-line options`, and `Host notification policy defaults and round-trips`. Message calls remain only as compatibility parity checks or coverage of private operations until Phase 5 removes the shell.
