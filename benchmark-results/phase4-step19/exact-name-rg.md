# Exact-name rg (phase 4 step 19)

- **SetWrapMode**: expected `scintilla/src/EditorWrapping.cxx`: PASS
  - `scintilla/src/EditorWrapping.cxx:87:void Editor::SetWrapMode(Wrap wrapMode) {`
- **WrapCount**: expected `scintilla/src/EditorWrapping.cxx`: PASS
  - `scintilla/src/EditorWrapping.cxx:446:Sci::Line Editor::WrapCount(Sci::Line line) {`
- **SetScrollWidth**: expected `scintilla/src/EditorScrolling.cxx`: PASS
  - `scintilla/src/EditorScrolling.cxx:146:void Editor::SetScrollWidth(int width) {`
- **SetReadOnly**: expected `scintilla/src/EditorDocument.cxx`: PASS
  - `scintilla/src/EditorDocument.cxx:108:void Editor::SetReadOnly(bool readOnly) {`
- **SetModEventMask**: expected `scintilla/src/EditorHost.cxx`: PASS
  - `scintilla/src/EditorHost.cxx:82:void Editor::SetModEventMask(ModificationFlags eventMask) noexcept {`
- **SearchInTarget**: expected `scintilla/src/EditorSearch.cxx`: PASS
  - `scintilla/src/EditorSearch.cxx:171:Sci::Position Editor::SearchInTarget(std::string_view text) {`
  - `scintilla/src/EditorSearch.cxx:225:Sci::Position Editor::SearchInTarget(const char *text, Sci::Position length) {`
- **Undo**: expected `scintilla/src/EditorCommands.h`: FAIL
  - `scintilla/src/KeyMap.cxx:146:    {Keys::Back, 		SCI_ALT,	EditorCommand::Undo},`
  - `scintilla/src/KeyMap.cxx:148:    {Key('Z'), 			SCI_CTRL,	EditorCommand::Undo},`
  - `scintilla/src/EditorCommands.cxx:182:	case Message::Undo: return EditorCommand::Undo;`
  - `scintilla/src/EditorCommands.cxx:481:	case EditorCommand::Undo:`
  - `scintilla/src/ScintillaBase.cxx:112:		ExecuteCommand(EditorCommand::Undo);`
- **LineDown**: expected `scintilla/src/EditorCommands.h`: FAIL
  - `scintilla/src/KeyMap.cxx:91:    {Keys::Down,		SCI_NORM,	EditorCommand::LineDown},`
  - `scintilla/src/KeyMap.cxx:92:    {Keys::Down,		SCI_SHIFT,	EditorCommand::LineDownExtend},`
  - `scintilla/src/KeyMap.cxx:94:    {Keys::Down,		SCI_ASHIFT,	EditorCommand::LineDownRectExtend},`
  - `scintilla/src/ScintillaBase.cxx:149:		case EditorCommand::LineDown:`
  - `scintilla/src/EditorCommands.cxx:81:	case Message::LineDown: return EditorCommand::LineDown;`
- **AutoCShow**: expected `scintilla/src/EditorAutocomplete.cxx`: PASS
  - `scintilla/src/EditorAutocomplete.cxx:71:void ScintillaBase::AutoCShow(Sci::Position lengthEntered, const char *itemList) {`
- **CallTipShow**: expected `scintilla/src/EditorCallTips.cxx`: PASS
  - `scintilla/src/EditorCallTips.cxx:72:void ScintillaBase::CallTipShow(Sci::Position pos, const char *definition) {`
  - `scintilla/src/EditorCallTips.cxx:80:void ScintillaBase::CallTipShow(Point pt, const char *defn) {`
- **FormatRange**: expected `scintilla/src/EditorPrinting.cxx`: PASS
  - `scintilla/src/EditorPrinting.cxx:117:Sci::Position Editor::FormatRange(bool draw, const RangeToFormatFull &fr) {`

## SetTechnology (deleted feature)
- Live `Editor::SetTechnology` definition: NONE (PASS)

## Undo/LineDown command bodies
scintilla/src/EditorCommands.cxx:481:	case EditorCommand::Undo:

scintilla/src/ScintillaBase.cxx:149:		case EditorCommand::LineDown:
scintilla/src/EditorCommands.cxx:212:	case EditorCommand::LineDown:
scintilla/src/EditorRecording.h:85:	case EditorCommand::LineDown:

