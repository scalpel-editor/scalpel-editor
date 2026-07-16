# Phase 4 final exact-name checks

Recorded against revision `0eb05e6`. `rg` is the authority for exact definitions and callers; grepai exact-name ranks remain in `search-results.jsonl`.

| Feature | Authoritative definition | Focused evidence | Result |
| --- | --- | --- | --- |
| `SetWrapMode` / `WrapCount` | `EditorWrapping.cxx:87`, `EditorWrapping.cxx:446` | `EditorWrappingTest.cxx` | PASS |
| `SetScrollWidth` | `EditorScrolling.cxx:146` | `EditorScrollingTest.cxx` | PASS |
| `SetReadOnly` | `EditorDocument.cxx:108` | `EditorDocumentTest.cxx` | PASS |
| `SetModEventMask` | `EditorHost.cxx:82` | `EditorHostTest.cxx` | PASS |
| `SearchInTarget` | public string-view operation at `EditorSearch.cxx:171`; private counted helper at `EditorSearch.cxx:225` | `EditorSearchTest.cxx` | PASS |
| `Undo` | `EditorHistory.cxx:78`; command dispatch at `EditorCommands.cxx:481` | `EditorHistoryTest.cxx`, `EditorCommandTest.cxx` | PASS |
| `LineDown` | command dispatch at `EditorCommands.cxx:212`; shared movement helper at `Editor.cxx:2612` | `EditorCommandTest.cxx` | PASS |
| `AutoCShow` | `EditorAutocomplete.cxx:71` | `EditorAutocompleteTest.cxx` | PASS |
| `CallTipShow` | public position operation at `EditorCallTips.cxx:72`; private point helper at `EditorCallTips.cxx:80` | `EditorCallTipsTest.cxx` | PASS |
| `FormatRange` | `EditorPrinting.cxx:117` | `EditorPrintingTest.cxx` | PASS |
| `SetTechnology` | no `Editor` definition or `Message::SetTechnology` case; deletion decision at `MESSAGE_REMOVAL.md:302` | `tools/check-retained-entrypoints.sh` | PASS |

The retained-entry audit verifies the full inventory rather than only these benchmark samples: every retained callable has one thin temporary message case plus a named method or `EditorCommand`, and every deleted callable has no dispatch case.
