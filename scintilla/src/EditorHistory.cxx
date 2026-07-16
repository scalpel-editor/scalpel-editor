// Scintilla source code edit control
/** @file EditorHistory.cxx
 ** Undo, redo, save point, and undo-buffer control for the editor.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>

#include <stdexcept>
#include <utility>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <forward_list>
#include <optional>
#include <algorithm>
#include <iterator>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <future>

#include "ScintillaTypes.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "CharacterType.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "PerLine.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "CaseConvert.h"
#include "Document.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "ElapsedPeriod.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

// Undoes one action, or a whole top-level group opened with BeginUndoAction.
// Does nothing when CanUndo() is false.
void Editor::Undo() {
	if (pdoc->CanUndo()) {
		InvalidateCaret();
		pdoc->Undo();
	}
}

// Reapplies the last undone action or group. Does nothing when CanRedo() is false.
void Editor::Redo() {
	if (pdoc->CanRedo()) {
		pdoc->Redo();
	}
}

// True when there is undo history and the document is not read-only.
bool Editor::CanUndo() const noexcept {
	return pdoc->CanUndo() && !pdoc->IsReadOnly();
}

// True when there is redo history and the document is not read-only.
bool Editor::CanRedo() const noexcept {
	return pdoc->CanRedo() && !pdoc->IsReadOnly();
}

// Marks the current document state as the save point (unmodified). Leaving or
// re-entering that point notifies SavePointLeft / SavePointReached.
void Editor::SetSavePoint() {
	pdoc->SetSavePoint();
}

// Start a nested undo group. Matching EndUndoAction calls close the group so
// the sequence undoes as one step. Groups may nest; only the top-level sequence
// is a single undo unit.
void Editor::BeginUndoAction() {
	pdoc->BeginUndoAction();
}

// Close one nested undo group started with BeginUndoAction.
void Editor::EndUndoAction() {
	pdoc->EndUndoAction();
}

// Drops all undo and redo history and treats the document as unmodified
// without sending SavePointReached.
void Editor::EmptyUndoBuffer() {
	pdoc->DeleteUndoHistory();
}

// When false, further edits are not recorded. Empty the buffer when turning
// collection off so history does not drift from the text.
void Editor::SetUndoCollection(bool collectUndo) {
	pdoc->SetUndoCollection(collectUndo);
}

// True when edits are being recorded on the undo stack.
bool Editor::GetUndoCollection() const noexcept {
	return pdoc->IsCollectingUndo();
}

// Nested BeginUndoAction depth: 0 means no open group.
int Editor::GetUndoSequence() const noexcept {
	return pdoc->UndoSequenceDepth();
}

// Number of actions currently stored on the undo stack.
int Editor::GetUndoActions() const noexcept {
	return pdoc->UndoActions();
}

// Set which action index is the save point (-1 if none).
void Editor::SetUndoSavePoint(int action) {
	pdoc->SetUndoSavePoint(action);
}

// Action index of the save point, or -1.
int Editor::GetUndoSavePoint() const noexcept {
	return pdoc->UndoSavePoint();
}

// Set the detach point where history branched from the saved state.
void Editor::SetUndoDetach(int action) {
	pdoc->SetUndoDetach(action);
}

// Action index of the detach point, or -1.
int Editor::GetUndoDetach() const noexcept {
	return pdoc->UndoDetach();
}

// Set the tentative (IME) action point, or -1 when none.
void Editor::SetUndoTentative(int action) {
	pdoc->SetUndoTentative(action);
}

// Action index of the tentative point, or -1.
int Editor::GetUndoTentative() const noexcept {
	return pdoc->UndoTentative();
}

// Set the current action index (boundary between undo and redo).
void Editor::SetUndoCurrent(int action) {
	pdoc->SetUndoCurrent(action);
}

// Current action index on the undo stack.
int Editor::GetUndoCurrent() const noexcept {
	return pdoc->UndoCurrent();
}

// Type flags of the action at the given index.
int Editor::GetUndoActionType(int action) const noexcept {
	return pdoc->UndoActionType(action);
}

// Document position associated with the action at the given index.
Sci::Position Editor::GetUndoActionPosition(int action) const noexcept {
	return pdoc->UndoActionPosition(action);
}

// Text bytes stored with the action at the given index.
std::string_view Editor::GetUndoActionText(int action) const noexcept {
	return pdoc->UndoActionText(action);
}

// Push a typed action without text at position onto the undo stack.
void Editor::PushUndoActionType(int type, Sci::Position position) {
	pdoc->PushUndoActionType(type, position);
}

// Replace the text of the most recent undo action.
void Editor::ChangeLastUndoActionText(std::string_view text) {
	pdoc->ChangeLastUndoActionText(text.size(), text.data());
}

// Records a container-owned action on the undo stack. token is returned in the
// Modified notification with Container when the action is undone or redone.
// mayCoalesce allows joining with adjacent typing/deletion coalescing.
void Editor::AddUndoAction(Sci::Position token, bool mayCoalesce) {
	pdoc->AddUndoAction(token, mayCoalesce);
}

// Enables or disables change-history markers on the document. The low bit of
// the option turns document change history on.
void Editor::SetChangeHistory(ChangeHistoryOption option) {
	changeHistoryOption = option;
	pdoc->ChangeHistorySet(static_cast<int>(option) & 1);
}

// Current change-history option (markers / indicators in the margin and text).
ChangeHistoryOption Editor::GetChangeHistory() const noexcept {
	return changeHistoryOption;
}
