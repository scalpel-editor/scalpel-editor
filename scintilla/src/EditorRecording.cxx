// Scintilla source code edit control
/** @file EditorRecording.cxx
 ** Macro recording lifecycle, typed command capture, host callback, and replay.
 **
 ** StartRecording / StopRecording turn capture on and off. Nested start while
 ** already recording stays recording. SetRecordingCallback installs the host
 ** sink that receives owned RecordedAction values.
 **
 ** Zero-argument bindable commands that IsRecordableCommand accepts are
 ** captured at ExecuteCommand as RecordedCommand. Parameterized operations
 ** (text insert/replace, goto, search, selection mode, character insert) emit
 ** typed RecordedAction values at their named entry points.
 **
 ** Replay applies the same named editor operations that produced the actions.
 ** An internal replaying flag suppresses capture so playback does not record
 ** itself even if recording remains on.
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
#include <variant>

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorStyleTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"
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

// Turn capture on. Nested start while already recording is a no-op.
void Editor::StartRecording() noexcept {
	recording = true;
}

// Turn capture off.
void Editor::StopRecording() noexcept {
	recording = false;
}

// True while StartRecording has been called and StopRecording has not.
bool Editor::IsRecording() const noexcept {
	return recording;
}

// Install the host sink for typed recorded actions. Empty callback discards them.
void Editor::SetRecordingCallback(RecordingCallback callback) {
	recordingCallback = std::move(callback);
}

// Deliver one action when recording and not replaying. No-op if the host
// installed no callback.
void Editor::EmitRecordedAction(const RecordedAction &action) {
	if (!recording || replaying) {
		return;
	}
	if (recordingCallback) {
		recordingCallback(action);
	}
}

namespace {

// RAII guard so nested replay and exceptions leave replaying clear.
class ReplayingGuard {
public:
	explicit ReplayingGuard(bool &flag) noexcept : flag(flag), previous(flag) {
		flag = true;
	}
	~ReplayingGuard() {
		flag = previous;
	}
	ReplayingGuard(const ReplayingGuard &) = delete;
	ReplayingGuard &operator=(const ReplayingGuard &) = delete;

private:
	bool &flag;
	const bool previous;
};

}

// Apply one owned action through the named editor path that produced it.
// Member function so protected document and selection operations are reachable.
void Editor::ApplyRecordedAction(const RecordedAction &action) {
	if (const RecordedCommand *cmd = std::get_if<RecordedCommand>(&action)) {
		ExecuteCommand(cmd->Command());
		return;
	}
	if (const RecordedReplaceSelection *replace = std::get_if<RecordedReplaceSelection>(&action)) {
		ReplaceSel(replace->text);
		return;
	}
	if (const RecordedAddText *add = std::get_if<RecordedAddText>(&action)) {
		AddText(add->text);
		return;
	}
	if (const RecordedInsertText *insert = std::get_if<RecordedInsertText>(&action)) {
		InsertText(insert->position, insert->text);
		return;
	}
	if (const RecordedAppendText *append = std::get_if<RecordedAppendText>(&action)) {
		AppendText(append->text);
		return;
	}
	if (std::holds_alternative<RecordedClearAll>(action)) {
		ClearAll();
		return;
	}
	if (const RecordedGotoLine *gotoLine = std::get_if<RecordedGotoLine>(&action)) {
		GotoLine(gotoLine->line);
		return;
	}
	if (const RecordedGotoPos *gotoPos = std::get_if<RecordedGotoPos>(&action)) {
		GotoPos(gotoPos->position);
		return;
	}
	if (std::holds_alternative<RecordedSearchAnchor>(action)) {
		SearchAnchor();
		return;
	}
	if (const RecordedSearch *search = std::get_if<RecordedSearch>(&action)) {
		const EditorCommand command = search->direction == SearchDirection::Next
			? EditorCommand::SearchNext
			: EditorCommand::SearchPrev;
		SearchText(command, search->flags, search->text);
		return;
	}
	if (const RecordedSetSelectionMode *mode = std::get_if<RecordedSetSelectionMode>(&action)) {
		// Match the path that enables move-extends when mode changes.
		SetSelectionMode(mode->mode, true);
		return;
	}
}

// Apply one recorded action without emitting new records.
void Editor::ReplayRecordedAction(const RecordedAction &action) {
	const ReplayingGuard guard(replaying);
	ApplyRecordedAction(action);
}

// Apply a sequence in order. Capture stays suppressed for the whole sequence.
void Editor::ReplayRecordedActions(const std::vector<RecordedAction> &actions) {
	const ReplayingGuard guard(replaying);
	for (const RecordedAction &action : actions) {
		ApplyRecordedAction(action);
	}
}
