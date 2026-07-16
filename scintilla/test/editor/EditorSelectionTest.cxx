// scalpel-editor test code
/** @file EditorSelectionTest.cxx
 ** Focused behavior tests for selection ranges and navigation.
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

TEST_CASE("SetSel and getters track stream selection") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "abcdef");

	editor.SetSel(1, 4);
	CHECK(editor.GetSelectionStart() == 1);
	CHECK(editor.GetSelectionEnd() == 4);
	CHECK(editor.GetCurrentPos() == 4);
	CHECK(editor.GetAnchor() == 1);
	CHECK_FALSE(editor.GetSelectionEmpty());
	CHECK(editor.GetSelText() == "bcd");

	editor.SetSel(2, 5);
	CHECK(editor.GetSelText() == "cde");
	CHECK_FALSE(editor.GetSelectionEmpty());
}

TEST_CASE("SetSelectionSerialized round-trips and ignores a null argument") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "abcdef");

	editor.SetSel(1, 4);

	const std::string serialized = editor.GetSelectionSerialized();
	REQUIRE_FALSE(serialized.empty());

	// Clear the selection, then restore it from the serialized form.
	editor.SetSel(0, 0);
	editor.SetSelectionSerialized(serialized);
	CHECK(editor.GetSelectionStart() == 1);
	CHECK(editor.GetSelectionEnd() == 4);
}

TEST_CASE("Empty selection and position setters") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "hello");

	editor.SetCurrentPos(3);
	editor.SetAnchor(3);
	CHECK(editor.GetSelectionEmpty());
	CHECK(editor.GetCurrentPos() == 3);

	editor.SetSelectionStart(1);
	editor.SetSelectionEnd(4);
	CHECK(editor.GetSelectionStart() == 1);
	CHECK(editor.GetSelectionEnd() == 4);

	editor.SetEmptySelection(2);
	CHECK(editor.GetSelectionEmpty());
	CHECK(editor.GetCurrentPos() == 2);
	CHECK(editor.GetAnchor() == 2);
}

TEST_CASE("GotoLine and GotoPos clamp and move caret") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "ab\ncd\nef\n");

	editor.GotoLine(1);
	CHECK(editor.GetCurrentPos() == 3);
	CHECK(editor.GetSelectionEmpty());

	editor.GotoPos(5);
	CHECK(editor.GetCurrentPos() == 5);

	editor.GotoLine(100);
	// Clamped to last line start of empty trailing line.
	CHECK(editor.GetCurrentPos() == editor.GetTextLength());
}

TEST_CASE("SelectAll command selects the whole document text") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "xyz");
	editor.RunCommand(EditorCommand::SelectAll);
	CHECK(editor.GetSelText() == "xyz");
	CHECK(editor.GetSelectionStart() == 0);
	CHECK(editor.GetSelectionEnd() == 3);
}

TEST_CASE("Multiple selection options and counts round-trip") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "one two one");

	CHECK_FALSE(editor.GetMultipleSelection());
	editor.SetMultipleSelection(true);
	CHECK(editor.GetMultipleSelection());
	CHECK(editor.GetSelections() == 1);

	editor.TargetWholeDocument();
	editor.SetSel(0, 3);
	editor.RunCommand(EditorCommand::MultipleSelectAddNext);
	CHECK(editor.GetSelections() >= 2);
}

TEST_CASE("SetSelectionN endpoints update one range by index") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "abcdefgh");
	editor.SetMultipleSelection(true);
	editor.SetSelection(2, 0);
	editor.AddSelection(6, 4);

	editor.SetSelectionNCaret(1, 7);
	CHECK(editor.GetSelectionNCaret(1) == 7);
	editor.SetSelectionNAnchor(1, 3);
	CHECK(editor.GetSelectionNAnchor(1) == 3);
	editor.SetSelectionNStart(0, 1);
	editor.SetSelectionNEnd(0, 3);
	CHECK(editor.GetSelectionNStart(0) == 1);
	CHECK(editor.GetSelectionNEnd(0) == 3);

	editor.SetSelectionNCaret(1, 5);
	CHECK(editor.GetSelectionNCaret(1) == 5);
}

TEST_CASE("RectangularSelectionModifier defaults to Alt and round-trips") {
	TestHost host;
	TestEditor editor(host);
	CHECK(editor.GetRectangularSelectionModifier() == KeyMod::Alt);
	editor.SetRectangularSelectionModifier(KeyMod::Ctrl);
	CHECK(editor.GetRectangularSelectionModifier() == KeyMod::Ctrl);
	editor.SetRectangularSelectionModifier(KeyMod::Alt);
	CHECK(editor.GetRectangularSelectionModifier() == KeyMod::Alt);
}

TEST_CASE("HideSelection toggles visibility flag") {
	TestHost host;
	TestEditor editor(host);
	CHECK_FALSE(editor.GetSelectionHidden());
	editor.HideSelection(true);
	CHECK(editor.GetSelectionHidden());
	editor.HideSelection(false);
	CHECK_FALSE(editor.GetSelectionHidden());
}

TEST_CASE("TargetFromSelection copies main selection into target") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "abcdef");
	editor.SetSel(2, 5);
	editor.TargetFromSelection();
	CHECK(editor.GetTargetStart() == 2);
	CHECK(editor.GetTargetEnd() == 5);
}

TEST_CASE("SwapMainAnchorCaret and RotateSelection commands run") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "abcdef");
	editor.SetSel(1, 4);
	const Sci::Position beforeCaret = editor.GetCurrentPos();
	const Sci::Position beforeAnchor = editor.GetAnchor();
	editor.RunCommand(EditorCommand::SwapMainAnchorCaret);
	CHECK(editor.GetCurrentPos() == beforeAnchor);
	CHECK(editor.GetAnchor() == beforeCaret);
}
