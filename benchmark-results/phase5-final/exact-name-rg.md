# Phase 5 final exact-name checks

Recorded against revision `03d6703`. `rg` is the authority for exact definitions, declarations, and focused evidence; grepai exact-name ranks remain in `search-results.jsonl`.

| Feature | Authoritative definition | Focused evidence | Result |
| --- | --- | --- | --- |
| `SetWrapMode` / `WrapCount` | `EditorWrapping.cxx:89`, `EditorWrapping.cxx:448` | `EditorWrappingTest.cxx` | PASS |
| `SetScrollWidth` | `EditorScrolling.cxx:148` | `EditorScrollingTest.cxx` | PASS |
| `SetReadOnly` | `EditorDocument.cxx:110` | `EditorDocumentTest.cxx` | PASS |
| `SetModEventMask` | `EditorHost.cxx:84` | `EditorHostTest.cxx` | PASS |
| `SearchInTarget` | public string-view operation at `EditorSearch.cxx:178`; private counted helper at `EditorSearch.cxx:225` | `EditorSearchTest.cxx` | PASS |
| `Undo` | `EditorHistory.cxx:80`; command dispatch at `EditorCommands.cxx:357` | `EditorHistoryTest.cxx`, `EditorCommandTest.cxx` | PASS |
| `LineDown` | command dispatch at `EditorCommands.cxx:88`; shared movement helper at `Editor.cxx:2610` | `EditorCommandTest.cxx` | PASS |
| `AutoCShow` | `EditorAutocomplete.cxx:73` | `EditorAutocompleteTest.cxx` | PASS |
| `CallTipShow` | public position operation at `EditorCallTips.cxx:74`; private point helper at `EditorCallTips.cxx:82` | `EditorCallTipsTest.cxx` | PASS |
| `FormatRange` | `EditorPrinting.cxx:117` | `EditorPrintingTest.cxx` | PASS |
| `SetTechnology` | no definition under `scintilla/src`, `scintilla/test`, or `scintilla/include`; deletion decision at `MESSAGE_REMOVAL.md:308` | `tools/check-no-message-layer.sh` | PASS |

Declarations remain in `Editor.h` or `ScintillaBase.h` for every retained operation. Repository searches find no `WndProc`, `DefWndProc`, generated `Message` command case, or deleted `SetTechnology` implementation in live core or test code.
