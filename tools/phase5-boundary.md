# Phase 5 removal boundary (frozen 2026-07-16)

Recorded while `Scintilla.iface`, the temporary `WndProc` shells, and the phase 4 verifiers still exist. Later steps must not reconstruct this classification by re-reading a deleted interface file. The phase 4 callable inventory in [MESSAGE_REMOVAL.md](../MESSAGE_REMOVAL.md) remains historical evidence for every `fun` / `get` / `set` / `evt` entry; this document freezes **generated type and client-surface** material that phase 5 deletes or re-owns.

Run `tools/check-no-message-layer.sh` as the repeatable completion check. Until phase 5 finishes it is expected to fail and print residual counts.

## Generated headers and disposition

| Header / file | Role today | Disposition |
| --- | --- | --- |
| `scintilla/include/ScintillaMessages.h` | `enum class Message` for temporary shells and tests | **Delete** with the shells (roadmap step 6). No retained project type keeps a message number. |
| `scintilla/include/ScintillaTypes.h` | Enums, flag operators, `Position`/`Line`/`Colour` aliases, `uptr_t`/`sptr_t`, marker/indicator masks | **Split and re-own** retained definitions in concern-sized headers (step 9). Delete client/message-only enums and the file when empty of consumers. |
| `scintilla/include/ScintillaStructures.h` | Ranges, find, print, `NotifyHeader`, `NotificationData` | **Replace** with project-owned types (steps 7–8). Delete the file when last include is gone. |
| `scintilla/include/ScintillaCall.h` | Generated C++ client wrapper over message numbers | **Delete** (step 10). No production consumer. |
| `scintilla/include/Scintilla.h` | C client constants (`SCI_*` / `SCN_*`) and parallel structs | **Delete** after any last retained constant moves (step 10). No production consumer. |
| `scintilla/include/Scintilla.iface` | Generator input and phase 4 inventory source | **Delete** (step 10). Phase 4 inventory text is the historical record. |
| `scintilla/include/ScintillaWidget.h` | GTK widget embedding | **Delete** (step 10). No consumer in this tree. |
| `scintilla/include/Sci_Position.h` | `Sci_Position`, `Sci_PositionU`, `SCI_METHOD` | **Retain** as Lexilla / external position contract. |
| `scintilla/include/ILexer.h` | Lexilla lexer and document attachment | **Retain**. Uses `SCI_METHOD` and `Sci_Position` only; does not include `ScintillaTypes.h`. |
| `scintilla/include/ILoader.h` | Incremental load interface | **Retain** for Lexilla-facing loader shape; multi-document *editor* API already deleted from dispatch. |
| `scintilla/ScintillaDoc.html` | Residual pointers and deletion notes | **Delete** when every retained feature has comments beside implementation (step 10). |

## `ScintillaTypes.h` — retained vs client/message-only

Values that retained behavior, masks, identifier ranges, or Lexilla still need keep their numeric meaning when moved.

### Retain (move into project-owned headers in step 9 batches)

**Shared position / colour / pointer aliases (batch: shared):** `Position`, `Line`, `Colour`, `ColourAlpha`, `InvalidPosition`, `CpUtf8` (UTF-8-only document reports this to Lexilla). `uptr_t` / `sptr_t` stay only until the last shell and packing helper die; they are not a long-term project API.

**Document / history / search:** `WhiteSpace`, `TabDrawMode`, `EndOfLine`, `FindOption` (+ operators), `ChangeHistoryOption`, `UndoSelectionHistoryOption`, `UndoFlags`, `LineEndType` (+ operators), `LineCharacterIndexType` (+ operators), `Status` (includes regex error), `DocumentOption` (internal `Document` construction still uses `DocumentOption::Default`; multi-document *messages* already deleted), `IdleStyling`, `TypeProperty` (lexer property typing).

**Styling / markers / decorations:** `StylesCommon`, `CharacterSet`, `CaseVisible`, `FontWeight`, `FontStretch`, `FontQuality` (+ operators), `Element`, `Layer`, `IndicatorStyle`, `IndicatorNumbers`, `IndicValue`, `IndicFlag`, `Alpha`, `MarkerSymbol`, `MarkerOutline`, `MarginType`, `MarginOption`, `AnnotationVisible`, `EOLAnnotationVisible`, `EdgeVisualStyle`, `PhasesDraw`, `LineCache`, `RepresentationAppearance` (+ operators), `IndentView`, `PrintOption`, `Supports`, `MarkerMax`, `MaskHistory`, `MaskFolders`, `MaxMargin`, `FontSizeMultiplier`, `TimeForever`, `KeywordsetMax`, `IndicatorMax`.

**Input / selection / commands:** `CursorShape`, `IMEInteraction`, `CaretPolicy` (+ operators), `VisiblePolicy`, `SelectionMode`, `CaretSticky`, `CaretStyle` (+ operators), `VirtualSpace`, `MultiPaste`, `Keys`, `KeyMod` (+ `ModifierFlags`), `CharacterSource`, `CompletionMethods`, `PopUp`, `FocusChange`, `ModificationFlags` (+ operators), `Update` (+ operators).

**Wrapping / folding / lexing:** `Wrap`, `WrapVisualFlag`, `WrapVisualLocation`, `WrapIndentMode`, `FoldLevel` (+ helpers), `FoldDisplayTextStyle`, `FoldAction`, `AutomaticFold`, `FoldFlag` (+ operators), `AutoCompleteOption`, `CaseInsensitiveBehaviour`, `MultiAutoComplete`, `Ordering`.

**Still used internally though application messages were removed:** `Technology` (surface / font realise still carry a default technology until phase 6 collapses the platform), `Bidirectional` (state and named set/get remain; full mixed-direction layout is later work), `ScaleTechnique` only if a retained type still references it after renderer collapse — treat as **review at step 9**; prefer delete if no production field remains.

### Client / message-only (delete with the layer; no project re-home)

- `Accessibility` — dispatch deleted; always-disabled bridge never implemented.
- Notification kind `MacroRecord` and any constant that exists only for `SCN_MACRORECORD` — production recording is `RecordedAction` only.
- Notification kinds already deleted as callables: `Key`, `URIDropped` (and any other deleted `evt` rows in MESSAGE_REMOVAL).
- Generated C names in `Scintilla.h` (`SCI_*`, `SCN_*`) that duplicate the enums above.
- Entire `Message` enumeration in `ScintillaMessages.h`.

When an enum value was never referenced outside generated headers and deleted messages, drop it in the batch that owns its former neighbors rather than carrying empty stubs.

## `ScintillaStructures.h` — field-level freeze

| Type | Disposition |
| --- | --- |
| `PositionCR`, `CharacterRange` | **Delete** — 32-bit client range; named code uses `Sci::Position`. |
| `CharacterRangeFull` | **Retain shape** next to printing / format-range (`Position` pair). |
| `TextRange`, `TextRangeFull` | **Delete client packing** — named `GetTextRange` / `GetStyledText` already take buffer + positions. |
| `TextToFind`, `TextToFindFull` | **Delete** — structure-unpacking find messages already deleted; use target search. |
| `SurfaceID` | **Retain** as platform surface handle alias until phase 6 renames with the renderer. |
| `Rectangle` (in this header) | Prefer project `PRectangle` / geometry types; delete the duplicate client `Rectangle` when print path no longer needs it. |
| `RangeToFormat` | **Delete** — narrow client form; WndProc only widens it today. |
| `RangeToFormatFull` | **Retain shape** with `Sci::Position` ranges beside printing. |
| `NotifyHeader` | **Delete** Windows-shaped header (`hwndFrom`, `idFrom`). |
| `NotificationData` | **Replace** (step 7) with project-owned notification kind + fields the core actually fills. |

### Notification kinds emitted by production core (retain)

`StyleNeeded`, `CharAdded`, `SavePointReached`, `SavePointLeft`, `ModifyAttemptRO`, `Modified`, `UpdateUI`, `Painted`, `FocusIn`, `FocusOut`, `Zoom`, `DoubleClick`, `HotSpotClick`, `HotSpotDoubleClick`, `HotSpotReleaseClick`, `IndicatorClick`, `IndicatorRelease`, `MarginClick`, `MarginRightClick`, `NeedShown`, `DwellStart`, `DwellEnd`, `CallTipClick`, `AutoCSelection`, `AutoCCancelled`, `AutoCCharDeleted`, `AutoCCompleted`, `AutoCSelectionChange`, `UserListSelection`.

### Notification fields assigned in production (retain meaning; re-type)

`position`, `ch`, `characterSource`, `modifiers`, `modificationType`, `text`, `length`, `linesAdded`, `line`, `foldLevelNow`, `foldLevelPrev`, `margin`, `listType`, `x`, `y`, `token`, `annotationLinesAdded`, `updated`, `listCompletionMethod`.

### Notification fields that must leave with the message layer

- `nmhdr` / `NotifyHeader` shape.
- `message`, `wParam`, `lParam` — leftover macro-record and autocomplete packing through the Windows-shaped struct. Autocomplete currently stashes list type and positions in those fields; step 7 must give them typed members (`listType`, `position`, …) instead of reusing macro-record slots.
- `Notification::MacroRecord` — never emitted; typed recording only.

## Temporary shells and helpers (delete in steps 5–6)

| Item | Owner | Notes |
| --- | --- | --- |
| `ScintillaBase::WndProc` | `ScintillaBase.cxx` | Autocomplete, call tip, lexer forwarders then `Editor::WndProc`. |
| `Editor::WndProc` | `Editor.cxx` | ~700 thin cases; only unpack + call named code. |
| `Editor::DefWndProc` | pure virtual; `TestEditor` implements | Test fall-through for deleted messages. |
| `StringResult` / `BytesResult` | `Editor` | Message string return packing. |
| `PtrFromSPtr`, `ConstCharPtrFromSPtr`, `ViewFromParams`, `PositionFromUPtr`, … | `Editor.h` | Shell coercion only after named APIs are typed (step 2). |
| `CommandFromMessage` | `EditorCommands` | Shell + `AssignCmdKey` message path only; named binding already takes `EditorCommand`. |
| `KeysFromWParam` / `KeyModFromWParam` | `Editor.cxx` | Packed key+modifier in one `uptr_t` for message `AssignCmdKey` / `ClearCmdKey`. |

## Phase 4 tools after the shells die

| Tool | After step 6 |
| --- | --- |
| `tools/check-message-inventory.sh` | **Historical / retire** — requires `Scintilla.iface` and inventory sync; do not keep as a green gate once `.iface` is deleted. |
| `tools/check-retained-entrypoints.py` | **Historical / retire** — requires thin `Message::` cases. Callable→named mapping lives in MESSAGE_REMOVAL inventory text. |
| `tools/discoverability/*` | **Update** query expectations that mention `ScintillaMessages.h` or message paths. |
| `tools/check-no-message-layer.sh` | **Live gate** for phase 5 completion (step 11). |

## External same-spelling terms (allowed after completion)

| Term | Owner | Reason |
| --- | --- | --- |
| `SCI_METHOD` | `Sci_Position.h` / `ILexer.h` / `ILoader.h` | Lexilla calling convention macro. |
| `Sci_Position`, `Sci_PositionU`, `Sci_PositionCR` | `Sci_Position.h` | Lexilla position typedefs. |
| `IDocument::CodePage`, `IsDBCSLeadByte` | `ILexer.h` + document implementation | Lexilla compatibility; always UTF-8 / false. |
| `lvRelease4`, `lvRelease5`, `dvRelease4` | `ILexer.h` | Lexilla interface version constants. |
| Autogenerated section markers | `CharacterCategoryMap.cxx` / `CaseConvert.cxx` | Retained Unicode category and case-conversion tables, unrelated to the message layer. |

Historical mentions in `MESSAGE_REMOVAL.md`, `ROADMAP.md`, `tools/phase5-boundary.md`, `benchmark-results/`, and commit messages are documentation of the old path, not live API.

## Named APIs that were message-shaped at freeze time (step 2)

Step 2 (2026-07-16) replaced packed parameters on these production entry points. Temporary shells still convert at the boundary until steps 5–6 delete the shells.

| API | Former shape | Typed shape (done) |
| --- | --- | --- |
| `SetXCaretPolicy` / `SetYCaretPolicy` | `uptr_t policy, sptr_t slop` | `CaretPolicy policy, int slop` |
| `SetVisiblePolicy` | `uptr_t policy, sptr_t slop` | `VisiblePolicy policy, int slop` |
| `SetSelectionMode` | `uptr_t mode, bool setMoveExtends` | `SelectionMode mode, bool setMoveExtends` |
| `TextWidth` | `uptr_t style, const char *text` | `int style, std::string_view text` |
| `SearchText` | `EditorCommand, uptr_t flags, sptr_t text` | `EditorCommand, FindOption flags, std::string_view text` |
| `ValidMargin` | `uptr_t margin` | `size_t margin` |
| `OptionalColour` | `uptr_t useFlag, sptr_t rgb` | `bool useSetting, int rgb` |
| `ViewStyle::SetElementColourOptional` | `uptr_t useFlag, sptr_t rgb` | `bool useSetting, int rgb` |
| `CaretPolicySlop` / `VisiblePolicySlop` | `uintptr_t` constructors | Typed enum constructors only |

`CommandFromMessage` has no non-shell production callers beyond the WndProc path (including message-shaped `AssignCmdKey`); it stays until step 6.

## Completion check

`tools/check-no-message-layer.sh` greps the repository (excluding `seed/`, `benchmark-results/`, and this freeze’s historical docs where noted) for forbidden live forms:

- Generated message-layer files that must be absent after steps 6–10
- `WndProc` / `DefWndProc`
- `ScintillaMessages.h` / `enum class Message` / `Message::`
- `SCI_` / `SCN_` message constants outside the Lexilla allowlist
- Message-layer generated-section markers outside the retained Unicode-table allowlist
- Documentation that still tells readers to drive the editor through a number-to-switch path

Step 11 requires a clean run plus the three-tree `./check.sh` gate.

### Baseline residual counts (freeze day, before step 2)

Approximate live hits from `tools/check-no-message-layer.sh` after excluding historical docs and allowlisted Lexilla/client header files themselves:

| Category | Approx. hits |
| --- | --- |
| Generated message-layer files | 8 |
| `WndProc` / `DefWndProc` | ~760 |
| `ScintillaMessages.h` includes/paths | ~60 |
| `Message::` | ~1700 |
| `enum class Message` (outside generated header) | 1 (`ScintillaStructures.h` forward declare) |
| `SCI_*` / `SCN_*` outside Lexilla/client headers | ~220 |
| Message-layer autogenerated section markers | ~10 |
| WndProc coercion helpers | ~260 |
| Live docs directing through messages | 0 |

These counts fall as steps 2–10 land; step 11 requires all categories clean.
