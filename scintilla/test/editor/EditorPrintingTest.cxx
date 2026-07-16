// scalpel-editor test code
/** @file EditorPrintingTest.cxx
 ** Focused tests for print settings and FormatRange measurement/draw.
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

	editor.SetPrintMagnification(3);
	CHECK(editor.GetPrintMagnification() == 3);
	editor.SetPrintColourMode(PrintOption::BlackOnWhite);
	CHECK(editor.GetPrintColourMode() == PrintOption::BlackOnWhite);
}

TEST_CASE("FormatRange measures without drawing and returns a position") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("hello\nworld\n");

	// Stand-in SurfaceID from the test main window for AutoSurface.
	const SurfaceID sid = static_cast<SurfaceID>(&host.mainWindow);
	const PRectangle rc = PRectangle::FromInts(0, 0, 400, 200);

	const Sci::Position next = editor.FormatRange(false, 0, editor.GetTextLength(), rc, sid, sid);
	// Measurement should advance past some or all of the document.
	CHECK(next > 0);
	CHECK(next <= editor.GetTextLength());
}
