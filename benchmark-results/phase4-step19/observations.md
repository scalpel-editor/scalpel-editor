# Phase 4 step 19 discoverability observations

Recorded 2026-07-16 after residual `ScintillaDoc.html` cleanup and inventory wording updates. Corpus TSVs were not changed. Full matrix: 240 searches in `search-results.jsonl` (vector and hybrid × repository and `scintilla/src` × normal and held-out). Use `--all-target` expectations (final concern files).

## Index

See `metadata.json`, `grepai-status.txt`, and `grepai-config.yaml`. grepai version 47bba43; durable index waited for changed HTML, guides, and concern sources before the run.

## Exact-name `rg`

See `exact-name-rg.md`. Authoritative definitions:

| Feature | Definition location | `rg` |
| --- | --- | --- |
| SetWrapMode / WrapCount | `EditorWrapping.cxx` | PASS |
| SetScrollWidth | `EditorScrolling.cxx` | PASS |
| SetReadOnly | `EditorDocument.cxx` | PASS |
| SetModEventMask | `EditorHost.cxx` | PASS (note: fixed corpus still targets `EditorNotifications.cxx`, which does not exist) |
| SearchInTarget | `EditorSearch.cxx` | PASS |
| Undo | `EditorHistory.cxx` (`Editor::Undo`) and `EditorCommands.cxx` (`EditorCommand::Undo` dispatch) | PASS for both |
| LineDown | `EditorCommands.cxx` / movement helpers | PASS |
| AutoCShow | `EditorAutocomplete.cxx` | PASS |
| CallTipShow | `EditorCallTips.cxx` | PASS |
| FormatRange | `EditorPrinting.cxx` | PASS |
| SetTechnology | no live `Editor::SetTechnology` | PASS (deleted) |

## Default cell: vector / whole repository

| Slice | Concern top-3 | Definition top-3 |
| --- | --- | --- |
| Exact (retained, n=11) | 6/11 (55%) | 4/11 (36%) |
| Natural spaced/intent/effect (n=33) | 21/33 (64%) | 11/33 (33%) |
| Held-out retained (n=11) | 6/11 (55%) | 2/11 (18%) |

Pilot / hybrid / path-limited cells remain diagnostic; full roll-ups are in `summary.json`.

### Exact ranks (vector / repository)

| id | kind | concern_rank | definition_rank | top path | expected_file |
| --- | --- | --- | --- | --- | --- |
| autocomplete-show-exact | exact | 1 | 1 | scintilla/src/EditorAutocomplete.cxx | scintilla/src/EditorAutocomplete.cxx |
| call-tip-show-exact | exact | 3 | 3 | scintilla/src/CallTip.h | scintilla/src/EditorCallTips.cxx |
| format-range-exact | exact | 2 | None | scintilla/src/EditView.h | scintilla/src/EditorPrinting.cxx |
| line-down-exact | exact | None | None | scintilla/src/EditorWrapping.cxx | scintilla/src/EditorCommands.cxx |
| mod-event-mask-exact | exact | None | None | scintilla/src/EditorCommands.cxx | scintilla/src/EditorNotifications.cxx |
| read-only-exact | exact | 9 | None | scintilla/src/EditorCallTips.cxx | scintilla/src/EditorDocument.cxx |
| scroll-width-exact | exact | 1 | 1 | scintilla/src/EditorScrolling.cxx | scintilla/src/EditorScrolling.cxx |
| search-target-exact | exact | None | None | scintilla/src/CharacterCategoryMap.h | scintilla/src/EditorSearch.cxx |
| technology-exact | exact | None | None | scintilla/src/EditorPrinting.cxx | MESSAGE_REMOVAL.md |
| undo-exact | exact | None | None | scintilla/src/UndoHistory.cxx | scintilla/src/EditorCommands.cxx |
| wrap-count-exact | exact | 1 | None | scintilla/src/EditorWrapping.cxx | scintilla/src/EditorWrapping.cxx |
| wrap-set-mode-exact | exact | 1 | 1 | scintilla/src/EditorWrapping.cxx | scintilla/src/EditorWrapping.cxx |

### Natural-language ranks (vector / repository, retained)

| id | kind | concern_rank | definition_rank | top path | expected_file |
| --- | --- | --- | --- | --- | --- |
| autocomplete-show-effect | effect | 1 | None | scintilla/src/EditorAutocomplete.cxx | scintilla/src/EditorAutocomplete.cxx |
| autocomplete-show-intent | intent | 1 | None | scintilla/src/EditorAutocomplete.cxx | scintilla/src/EditorAutocomplete.cxx |
| autocomplete-show-spaced | spaced | 1 | None | scintilla/src/EditorAutocomplete.cxx | scintilla/src/EditorAutocomplete.cxx |
| call-tip-show-effect | effect | 1 | 1 | scintilla/src/EditorCallTips.cxx | scintilla/src/EditorCallTips.cxx |
| call-tip-show-intent | intent | 3 | 3 | scintilla/src/CallTip.h | scintilla/src/EditorCallTips.cxx |
| call-tip-show-spaced | spaced | 3 | 3 | scintilla/src/CallTip.h | scintilla/src/EditorCallTips.cxx |
| format-range-effect | effect | 3 | None | scintilla/src/LineMarker.cxx | scintilla/src/EditorPrinting.cxx |
| format-range-intent | intent | 1 | None | scintilla/src/EditorPrinting.cxx | scintilla/src/EditorPrinting.cxx |
| format-range-spaced | spaced | 4 | None | scintilla/src/Selection.h | scintilla/src/EditorPrinting.cxx |
| line-down-effect | effect | None | None | scintilla/src/EditorCaret.cxx | scintilla/src/EditorCommands.cxx |
| line-down-intent | intent | None | None | scintilla/src/Editor.cxx | scintilla/src/EditorCommands.cxx |
| line-down-spaced | spaced | None | None | scintilla/src/EditorWrapping.cxx | scintilla/src/EditorCommands.cxx |
| mod-event-mask-effect | effect | None | None | scintilla/src/EditorHost.cxx | scintilla/src/EditorNotifications.cxx |
| mod-event-mask-intent | intent | None | None | scintilla/src/Editor.cxx | scintilla/src/EditorNotifications.cxx |
| mod-event-mask-spaced | spaced | None | None | scintilla/src/EditorHost.cxx | scintilla/src/EditorNotifications.cxx |
| read-only-effect | effect | 1 | 1 | scintilla/src/EditorDocument.cxx | scintilla/src/EditorDocument.cxx |
| read-only-intent | intent | 1 | 1 | scintilla/src/EditorDocument.cxx | scintilla/src/EditorDocument.cxx |
| read-only-spaced | spaced | 5 | None | scintilla/src/EditorInput.cxx | scintilla/src/EditorDocument.cxx |
| scroll-width-effect | effect | 1 | 1 | scintilla/src/EditorScrolling.cxx | scintilla/src/EditorScrolling.cxx |
| scroll-width-intent | intent | 1 | 1 | scintilla/src/EditorScrolling.cxx | scintilla/src/EditorScrolling.cxx |
| scroll-width-spaced | spaced | 1 | 1 | scintilla/src/EditorScrolling.cxx | scintilla/src/EditorScrolling.cxx |
| search-target-effect | effect | 1 | None | scintilla/src/EditorSearch.cxx | scintilla/src/EditorSearch.cxx |
| search-target-intent | intent | 6 | None | scintilla/src/Editor.cxx | scintilla/src/EditorSearch.cxx |
| search-target-spaced | spaced | 2 | None | seed/sample/CMakeLists.txt | scintilla/src/EditorSearch.cxx |
| undo-effect | effect | None | None | scintilla/src/UndoHistory.cxx | scintilla/src/EditorCommands.cxx |
| undo-intent | intent | None | None | scintilla/src/EditorHistory.cxx | scintilla/src/EditorCommands.cxx |
| undo-spaced | spaced | None | None | scintilla/src/UndoHistory.h | scintilla/src/EditorCommands.cxx |
| wrap-count-effect | effect | 1 | None | scintilla/src/EditorWrapping.cxx | scintilla/src/EditorWrapping.cxx |
| wrap-count-intent | intent | 2 | 2 | scintilla/src/EditorFolding.cxx | scintilla/src/EditorWrapping.cxx |
| wrap-count-spaced | spaced | 1 | None | scintilla/src/EditorWrapping.cxx | scintilla/src/EditorWrapping.cxx |
| wrap-set-mode-effect | effect | 2 | 2 | scintilla/src/EditorScrolling.cxx | scintilla/src/EditorWrapping.cxx |
| wrap-set-mode-intent | intent | 1 | None | scintilla/src/EditorWrapping.cxx | scintilla/src/EditorWrapping.cxx |
| wrap-set-mode-spaced | spaced | 1 | 1 | scintilla/src/EditorWrapping.cxx | scintilla/src/EditorWrapping.cxx |

### Held-out ranks (vector / repository, retained)

| id | kind | concern_rank | definition_rank | top path | expected_file |
| --- | --- | --- | --- | --- | --- |
| autocomplete-show-held-out | held-out | 1 | None | scintilla/src/EditorAutocomplete.cxx | scintilla/src/EditorAutocomplete.cxx |
| call-tip-show-held-out | held-out | 7 | None | scintilla/src/CellBuffer.cxx | scintilla/src/EditorCallTips.cxx |
| format-range-held-out | held-out | 1 | None | scintilla/src/EditorPrinting.cxx | scintilla/src/EditorPrinting.cxx |
| line-down-held-out | held-out | None | None | scintilla/src/Position.h | scintilla/src/EditorCommands.cxx |
| mod-event-mask-held-out | held-out | None | None | scintilla/src/EditorHistory.cxx | scintilla/src/EditorNotifications.cxx |
| read-only-held-out | held-out | 1 | 1 | scintilla/src/EditorDocument.cxx | scintilla/src/EditorDocument.cxx |
| scroll-width-held-out | held-out | 1 | 1 | scintilla/src/EditorScrolling.cxx | scintilla/src/EditorScrolling.cxx |
| search-target-held-out | held-out | 9 | None | scintilla/src/RunStyles.h | scintilla/src/EditorSearch.cxx |
| undo-held-out | held-out | None | None | scintilla/src/EditorHistory.cxx | scintilla/src/EditorCommands.cxx |
| wrap-count-held-out | held-out | 1 | None | scintilla/src/EditorWrapping.cxx | scintilla/src/EditorWrapping.cxx |
| wrap-set-mode-held-out | held-out | 3 | None | scintilla/src/Editor.cxx | scintilla/src/EditorWrapping.cxx |

### Deleted feature (SetTechnology)

| id | concern_rank | top path |
| --- | --- | --- |
| technology-effect | effect | None | None | scintilla/src/Style.cxx | MESSAGE_REMOVAL.md |
| technology-exact | exact | None | None | scintilla/src/EditorPrinting.cxx | MESSAGE_REMOVAL.md |
| technology-held-out | held-out | None | None | seed/backends/OnlyWayUi_Renderer_GL3.cpp | MESSAGE_REMOVAL.md |
| technology-intent | intent | None | None | scintilla/src/EditView.cxx | MESSAGE_REMOVAL.md |
| technology-spaced | spaced | None | None | scintilla/src/XPM.h | MESSAGE_REMOVAL.md |

Technology queries should not surface a live implementation. Decision record remains in `MESSAGE_REMOVAL.md`.

## Boundary stability

About 200 characters of padding were inserted immediately before `Editor::SetWrapMode` and `Editor::SearchInTarget`, the index was waited on, and the normal queries for SetWrapMode, WrapCount, and SearchInTarget were re-run under all modes and scopes (`boundary/`). Files were restored afterward.

Under vector / repository, wrap queries kept the same concern ranks (all top-3). SearchInTarget exact remained absent from the top ten (same as base); search spaced/effect stayed at ranks 2 and 1. No wrap regression from the pad.

## Cold navigation

Natural-language first search, then exact read of the top concern file (no symbol name given up front). Full notes in `cold-navigation.md`.

| Concern | First search file | Within two steps? | Notes |
| --- | --- | --- | --- |
| Wrap | `EditorWrapping.cxx` | Yes (one step) | `SetWrapMode` visible in file |
| Undo | `EditorHistory.cxx` | Yes | `Editor::Undo` lives there; corpus expects `EditorCommands.cxx` for the command type |
| Search | `Editor.cxx` | No | Target search not reached without a second, better query or exact name |
| Autocomplete | `EditorAutocomplete.cxx` | Yes (one step) | `AutoCShow` visible in file |

## Acceptance against DISCOVERABILITY.md

Pilot-era bars (exact concern top-3 for every exact name; ≥80% natural-language concern top-3) are **not** met for the full multi-feature corpus under vector / whole repository after all concern moves. Strong features: wrapping, scrolling width, autocomplete, call tips, format range. Weak features under this cell: mod-event mask (corpus targets a non-existent `EditorNotifications.cxx`; implementation is `EditorHost.cxx`), SearchInTarget exact/intent, Undo/LineDown command queries (lower-level `UndoHistory` / caret / wrap chunks often outrank `EditorCommands.cxx`), read-only exact.

Obsolete HTML catalogs no longer dominate top results for moved concerns (post step-19 HTML reduction). Seed and generated iface still appear occasionally for search paraphrases.

## Follow-ups for step 20 (not done here)

1. Record a corpus target correction for SetModEventMask → `EditorHost.cxx` in `DISCOVERABILITY.md` with old/new evidence, then re-score.
2. Decide whether Undo/LineDown expectations should score `EditorHistory.cxx` / command bodies rather than only `EditorCommands.cxx`.
3. Full phase gate: inventory verifiers, WndProc production-call searches, focused editor tests, `./check.sh`.

## Files in this directory

- `search-results.jsonl` — 240 ranked searches
- `summary.json` — jq roll-ups
- `metadata.json`, `grepai-config.yaml`, `grepai-status.txt`
- `boundary/` — boundary re-run
- `exact-name-rg.md`, `cold-navigation.md`, this file
