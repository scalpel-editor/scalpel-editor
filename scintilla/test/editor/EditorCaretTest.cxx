// scalpel-editor test code
/** @file EditorCaretTest.cxx
 ** Focused behavior tests for caret appearance, sticky state, and period.
 **/

#include <array>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <forward_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ScintillaTypes.h"
#include "ScintillaMessages.h"
#include "ScintillaStructures.h"
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
#include "Document.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "AutoComplete.h"
#include "CallTip.h"
#include "ScintillaBase.h"

#include "TestPlatform.h"
#include "TestEditor.h"

#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

TEST_CASE("Caret period sticky style and width round-trip") {
	TestHost host;
	TestEditor editor(host);

	editor.WndProc(Message::SetCaretPeriod, 250, 0);
	CHECK(editor.WndProc(Message::GetCaretPeriod, 0, 0) == 250);

	editor.WndProc(Message::SetCaretSticky, static_cast<uptr_t>(CaretSticky::On), 0);
	CHECK(static_cast<CaretSticky>(editor.WndProc(Message::GetCaretSticky, 0, 0)) == CaretSticky::On);
	editor.WndProc(Message::ToggleCaretSticky, 0, 0);
	CHECK(static_cast<CaretSticky>(editor.WndProc(Message::GetCaretSticky, 0, 0)) == CaretSticky::Off);

	editor.WndProc(Message::SetCaretStyle, static_cast<uptr_t>(CaretStyle::Block), 0);
	CHECK(static_cast<CaretStyle>(editor.WndProc(Message::GetCaretStyle, 0, 0)) == CaretStyle::Block);

	editor.WndProc(Message::SetCaretWidth, 3, 0);
	CHECK(editor.WndProc(Message::GetCaretWidth, 0, 0) == 3);
}

TEST_CASE("Caret line visibility and frame options round-trip") {
	TestHost host;
	TestEditor editor(host);

	CHECK(editor.WndProc(Message::GetCaretLineVisible, 0, 0) == 0);
	editor.WndProc(Message::SetCaretLineVisible, 1, 0);
	CHECK(editor.WndProc(Message::GetCaretLineVisible, 0, 0) != 0);

	editor.WndProc(Message::SetCaretLineVisibleAlways, 1, 0);
	CHECK(editor.WndProc(Message::GetCaretLineVisibleAlways, 0, 0) != 0);

	editor.WndProc(Message::SetCaretLineFrame, 2, 0);
	CHECK(editor.WndProc(Message::GetCaretLineFrame, 0, 0) == 2);

	editor.WndProc(Message::SetCaretLineHighlightSubLine, 1, 0);
	CHECK(editor.WndProc(Message::GetCaretLineHighlightSubLine, 0, 0) != 0);
}

TEST_CASE("VerticalCentreCaret command runs without error") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("line1\nline2\nline3\nline4\nline5\n");
	editor.GotoLine(3);
	editor.RunCommand(EditorCommand::VerticalCentreCaret);
	// Host may or may not scroll in the fixed metric surface; command must be safe.
	CHECK(editor.GetCurrentPos() >= 0);
}

TEST_CASE("Additional carets blink and visibility options") {
	TestHost host;
	TestEditor editor(host);
	editor.WndProc(Message::SetAdditionalCaretsBlink, 0, 0);
	CHECK(editor.WndProc(Message::GetAdditionalCaretsBlink, 0, 0) == 0);
	editor.WndProc(Message::SetAdditionalCaretsVisible, 0, 0);
	CHECK(editor.WndProc(Message::GetAdditionalCaretsVisible, 0, 0) == 0);
}
