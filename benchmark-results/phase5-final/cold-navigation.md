# Phase 5 final cold navigation

Each check started with only the natural-language prompt. Opening a returned concern and using `rg` for a visible operation name is the exact follow-up described in `DISCOVERABILITY.md`.

| Concern | First search | Navigation result | Searches |
| --- | --- | --- | ---: |
| Wrapping | `turn wrapping on for long lines` | `EditorWrapping.cxx` ranked first; public operations and focused tests are reached directly | 1 |
| Undo | `undo the most recent document change` | `EditorHistory.cxx` ranked first; `Editor::Undo`, callers, and history tests are visible | 1 |
| Target search | `search within the current target range`, then `target range search and replace editor` | The first query reached document target work; the refined query ranked `EditorSearch.cxx` first | 2 |
| Line down | `move the caret down one visual line` | The intentionally shared movement helper in `Editor.cxx` ranked first; `rg LineDown` reaches `EditorCommands.cxx` and `EditorCommandTest.cxx` | 1 |
| Modification filtering | `choose which document changes send notifications` | `EditorHost.cxx` ranked third with its fixed-host policy and named operation | 1 |
| Call tips | `show a call tip near the caret` | `EditorCallTips.cxx` ranked second with the public and point-based operations in one chunk | 1 |

All sampled concerns reach the authoritative implementation within the two-search limit. The only refinement started in lower-level document target work; no deleted generated interface or obsolete documentation was opened.
