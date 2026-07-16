// scalpel-editor test code
/** @file EditorClipboardTest.cxx
 ** Focused behavior tests for cut, copy, paste options, and CanPaste.
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

TEST_CASE("CanPaste is false when read-only") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "text");
	CHECK(editor.CanPaste());
	editor.SetReadOnly(true);
	CHECK_FALSE(editor.CanPaste());
}

TEST_CASE("Cut Copy and Paste commands use the host clipboard") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "hello");
	editor.SelectAll();

	editor.RunCommand(EditorCommand::Copy);
	CHECK(editor.observations.clipboard == "hello");
	CHECK(editor.GetText() == "hello");

	editor.RunCommand(EditorCommand::Cut);
	CHECK(editor.GetText().empty());
	CHECK(editor.observations.clipboard == "hello");

	editor.RunCommand(EditorCommand::Paste);
	CHECK(editor.GetText() == "hello");
}

TEST_CASE("CopyRange places a document range on the clipboard") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "abcdef");
	editor.CopyRangeToClipboard(1, 4);
	CHECK(editor.observations.clipboard == "bcd");
}

TEST_CASE("CopyText places supplied text on the clipboard") {
	TestHost host;
	TestEditor editor(host);
	editor.CopyText("xyz");
	CHECK(editor.observations.clipboard == "xyz");
}

TEST_CASE("CopyAllowLine copies the current line when empty") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "line one\nline two\n");
	editor.GotoPos(0);
	editor.RunCommand(EditorCommand::CopyAllowLine);
	// Line copy includes the line end.
	CHECK(editor.observations.clipboard.find("line one") == 0);
}

TEST_CASE("Paste convert endings and multi-paste options round trip") {
	TestHost host;
	TestEditor editor(host);
	CHECK(editor.GetPasteConvertEndings());
	editor.SetPasteConvertEndings(false);
	CHECK_FALSE(editor.GetPasteConvertEndings());

	CHECK(editor.GetMultiPaste() == MultiPaste::Once);
	editor.SetMultiPaste(MultiPaste::Each);
	CHECK(editor.GetMultiPaste() == MultiPaste::Each);
}

TEST_CASE("Copy separator option round trips") {
	TestHost host;
	TestEditor editor(host);
	editor.SetCopySeparator("|");
	CHECK(editor.GetCopySeparator() == "|");
}
