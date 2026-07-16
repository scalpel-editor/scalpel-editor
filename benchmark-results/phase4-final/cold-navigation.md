# Phase 4 final cold navigation

Each check started with only the natural-language prompt. Opening a returned feature file and using `rg` for a visible operation name is the exact second step described in `DISCOVERABILITY.md`.

| Concern | First search | Navigation result | Searches |
| --- | --- | --- | --- |
| Wrapping | `turn wrapping on for long lines` | `EditorWrapping.cxx` ranked first; the public operations and focused test are found directly | 1 |
| Undo | `undo the most recent document change` | `EditorHistory.cxx` ranked first; `Editor::Undo`, callers, and history tests are visible | 1 |
| Target search | `search within the current target range`, then `target range search and replace editor` | The first query reached shared target work; the refined query ranked `EditorSearch.cxx` first | 2 |
| Line down | `move the caret down one visual line` | The intentionally shared movement helper in `Editor.cxx` ranked first; `rg LineDown` reaches `EditorCommands.cxx` and `EditorCommandTest.cxx` | 1 |
| Modification filtering | `choose which document changes send notifications`, then `modification event mask notifications` | The first query placed `EditorHost.cxx` third; the refined query ranked it first | 2 |
| Call tips | `show a call tip near the caret` | `EditorCallTips.cxx` ranked first with the public and point-based operations in one chunk | 1 |

All sampled concerns reach the authoritative implementation within the two-search limit. Wrong first results were shared implementation code rather than obsolete HTML or generated benchmark output.
