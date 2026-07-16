## wrap
Query: `turn wrapping on for long lines`
Step 1 (grepai vector):
  1. scintilla/src/EditorWrapping.cxx
  2. scintilla/src/Editor.h
  3. scintilla/src/EditView.cxx
  4. scintilla/src/EditorFolding.cxx
  5. scintilla/src/ContractionState.h
Step 2: open scintilla/src/EditorWrapping.cxx and exact-search public operations
  77:bool Editor::Wrapping() const noexcept {
  87:void Editor::SetWrapMode(Wrap wrapMode) {
  105:void Editor::SetWrapVisualFlags(WrapVisualFlag wrapVisualFlags) {
  119:void Editor::SetWrapVisualFlagsLocation(WrapVisualLocation wrapVisualFlagsLocation) {
  133:void Editor::SetWrapStartIndent(int wrapStartIndent) {
  140:int Editor::GetWrapStartIndent() const noexcept {
  148:void Editor::SetWrapIndentMode(WrapIndentMode wrapIndentMode) {
  164:void Editor::NeedWrapping(Sci::Line docLineStart, Sci::Line docLineEnd) {

## undo
Query: `undo the most recent document change`
Step 1 (grepai vector):
  1. scintilla/src/EditorHistory.cxx
  2. scintilla/src/EditorDocument.cxx
  3. scintilla/src/UndoHistory.cxx
  4. scintilla/src/Document.h
  5. scintilla/src/UndoHistory.h
Step 2: open scintilla/src/EditorHistory.cxx and exact-search public operations
  78:void Editor::Undo() {
  86:void Editor::Redo() {
  93:bool Editor::CanUndo() const noexcept {
  98:bool Editor::CanRedo() const noexcept {
  104:void Editor::SetSavePoint() {
  111:void Editor::BeginUndoAction() {
  116:void Editor::EndUndoAction() {
  122:void Editor::EmptyUndoBuffer() {

## search
Query: `search within the current target range`
Step 1 (grepai vector):
  1. scintilla/src/Editor.cxx
  2. scintilla/src/Document.cxx
  3. scintilla/include/Scintilla.iface
  4. scintilla/src/EditorSelection.cxx
  5. scintilla/src/Selection.cxx
Step 2: open scintilla/src/Editor.cxx and exact-search public operations
  221:void Editor::Finalise() {
  226:void Editor::DropGraphics() noexcept {
  231:void Editor::InvalidateStyleData() noexcept {
  239:void Editor::InvalidateStyleRedraw() {
  245:void Editor::RefreshStyleData() {
  257:bool Editor::HasMarginWindow() const noexcept {
  278:Sci::Line Editor::TopLineOfMain() const noexcept {
  304:Sci::Line Editor::LinesOnScreen() const {

## autocomplete
Query: `show an autocomplete list at the caret`
Step 1 (grepai vector):
  1. scintilla/src/EditorAutocomplete.cxx
  2. scintilla/src/AutoComplete.h
  3. scintilla/src/AutoComplete.cxx
  4. scintilla/src/ScintillaBase.h
  5. scintilla/include/Scintilla.iface
Step 2: open scintilla/src/EditorAutocomplete.cxx and exact-search public operations
  71:void ScintillaBase::AutoCShow(Sci::Position lengthEntered, const char *itemList) {
  78:void ScintillaBase::AutoCCancel() {
  82:bool ScintillaBase::AutoCActive() const noexcept {
  87:Sci::Position ScintillaBase::AutoCPosStart() const noexcept {
  92:void ScintillaBase::AutoCComplete() {
  97:void ScintillaBase::AutoCStops(const char *characterSet) {
  101:void ScintillaBase::AutoCSetSeparator(char separatorCharacter) {
  111:void ScintillaBase::AutoCSelect(const char *select) {

