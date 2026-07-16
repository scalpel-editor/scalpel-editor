// scalpel-editor test code
/** @file EditorCaretTest.cxx
 ** Focused behavior tests for caret appearance, sticky state, and period.
 **/

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

	editor.SetCaretPeriod(250);
	CHECK(editor.GetCaretPeriod() == 250);

	editor.SetCaretSticky(CaretSticky::On);
	CHECK(editor.GetCaretSticky() == CaretSticky::On);
	editor.ToggleCaretSticky();
	CHECK(editor.GetCaretSticky() == CaretSticky::Off);

	editor.SetCaretStyle(CaretStyle::Block);
	CHECK(editor.GetCaretStyle() == CaretStyle::Block);

	editor.SetCaretWidth(3);
	CHECK(editor.GetCaretWidth() == 3);
}

TEST_CASE("Caret line visibility and frame options round-trip") {
	TestHost host;
	TestEditor editor(host);

	CHECK_FALSE(editor.GetCaretLineVisible());
	editor.SetCaretLineVisible(true);
	CHECK(editor.GetCaretLineVisible());

	editor.SetCaretLineVisibleAlways(true);
	CHECK(editor.GetCaretLineVisibleAlways());

	editor.SetCaretLineFrame(2);
	CHECK(editor.GetCaretLineFrame() == 2);

	editor.SetCaretLineHighlightSubLine(true);
	CHECK(editor.GetCaretLineHighlightSubLine());
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
	editor.SetAdditionalCaretsBlink(false);
	CHECK_FALSE(editor.GetAdditionalCaretsBlink());
	editor.SetAdditionalCaretsVisible(false);
	CHECK_FALSE(editor.GetAdditionalCaretsVisible());
}
