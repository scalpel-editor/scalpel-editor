// Scintilla source code edit control
/** @file EditorHost.cxx
 ** Fixed-host notification policy, status reporting, and feature queries.
 **
 ** The modification-event mask controls which document changes reach the
 ** typed parent notification. Command events independently control the
 ** coarse change callback for text edits. Status stores the latest reported
 ** error or warning; setting Status::Ok clears it. Feature queries report the
 ** capabilities of the one active drawing surface.
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

void Editor::SetModEventMask(ModificationFlags eventMask) noexcept {
	modEventMask = eventMask;
}

ModificationFlags Editor::GetModEventMask() const noexcept {
	return modEventMask;
}

void Editor::SetCommandEvents(bool enabled) noexcept {
	commandEvents = enabled;
}

bool Editor::GetCommandEvents() const noexcept {
	return commandEvents;
}

void Editor::SetStatus(Status status) noexcept {
	errorStatus = status;
}

Status Editor::GetStatus() const noexcept {
	return errorStatus;
}

void Editor::NotifyErrorOccurred(Document *, void *, Status status) {
	SetStatus(status);
}

// Query the active surface rather than predicting renderer capabilities in
// the editor. The Phase 6 renderer supplies the fixed answers for Wayland.
int Editor::SupportsFeature(Supports feature) {
	AutoSurface surface(this);
	return surface->SupportsFeature(feature);
}
