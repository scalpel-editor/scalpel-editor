// scalpel-editor test code
/** @file EditorPrintingTest.cxx
 ** Focused tests for print settings and FormatRange measurement/draw.
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

TEST_CASE("Print magnification colour mode and wrap round-trip") {
	TestHost host;
	TestEditor editor(host);

	editor.SetPrintMagnification(2);
	CHECK(editor.GetPrintMagnification() == 2);
	editor.SetPrintColourMode(PrintOption::InvertLight);
	CHECK(editor.GetPrintColourMode() == PrintOption::InvertLight);
	editor.SetPrintWrapMode(Wrap::Word);
	CHECK(editor.GetPrintWrapMode() == Wrap::Word);
	editor.SetPrintWrapMode(Wrap::Char);  // collapses to None
	CHECK(editor.GetPrintWrapMode() == Wrap::None);
}

TEST_CASE("FormatRange measures without drawing and returns a position") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("hello\nworld\n");

	// Use the test main window address as a stand-in SurfaceID for AutoSurface.
	RangeToFormatFull fr{};
	fr.hdc = static_cast<SurfaceID>(&host.mainWindow);
	fr.hdcTarget = fr.hdc;
	fr.rc = {0, 0, 400, 200};
	fr.rcPage = fr.rc;
	fr.chrg.cpMin = 0;
	fr.chrg.cpMax = editor.GetTextLength();

	const Sci::Position next = editor.FormatRange(false, fr);
	// Measurement should advance past some or all of the document.
	CHECK(next >= 0);
	CHECK(next <= editor.GetTextLength());
}

TEST_CASE("Print settings named path matches message path") {
	TestHost host;
	TestEditor editor(host);
	editor.SetPrintMagnification(3);
	CHECK(editor.WndProc(Message::GetPrintMagnification, 0, 0) == 3);
	editor.WndProc(Message::SetPrintColourMode, static_cast<uptr_t>(PrintOption::BlackOnWhite), 0);
	CHECK(editor.GetPrintColourMode() == PrintOption::BlackOnWhite);
	editor.WndProc(Message::SetPrintWrapMode, static_cast<uptr_t>(Wrap::Word), 0);
	CHECK(editor.GetPrintWrapMode() == Wrap::Word);
}

TEST_CASE("FormatRangeFull message is deleted from dispatch") {
	TestHost host;
	TestEditor editor(host);
	editor.ClearObservations();
	editor.WndProc(Message::FormatRangeFull, 0, 0);
	CHECK(std::find(editor.observations.defaultWindowCalls.begin(),
		editor.observations.defaultWindowCalls.end(),
		Message::FormatRangeFull) != editor.observations.defaultWindowCalls.end());
}
