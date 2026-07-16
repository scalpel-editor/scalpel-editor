// scalpel-editor test code
/** @file EditorFoldingTest.cxx
 ** Focused behavior tests for fold levels, expand/collapse, and line visibility.
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

namespace {

// Three-line nest: header at 0, child at 1, last at 2.
// Paints so later Redraw calls are not swallowed by redrawPendingText.
void InstallSimpleFold(TestEditor &editor) {
	editor.SetText("header\nchild\nlast\n");
	const int header = static_cast<int>(FoldLevel::Base) | static_cast<int>(FoldLevel::HeaderFlag);
	const int child = static_cast<int>(FoldLevel::Base) + 1;
	editor.SetFoldLevel(0, static_cast<FoldLevel>(header));
	editor.SetFoldLevel(1, static_cast<FoldLevel>(child));
	editor.SetFoldLevel(2, static_cast<FoldLevel>(child));
	editor.PaintAll();
	editor.ClearObservations();
}

}  // namespace

TEST_CASE("Fold level parent and last-child queries") {
	TestHost host;
	TestEditor editor(host);
	InstallSimpleFold(editor);

	const int header = static_cast<int>(FoldLevel::Base) | static_cast<int>(FoldLevel::HeaderFlag);
	CHECK(static_cast<int>(editor.GetFoldLevel(0)) == header);
	CHECK(editor.GetLastChild(0) == 2);
	CHECK(editor.GetFoldParent(1) == 0);
	CHECK(editor.GetFoldParent(0) == -1);

	// SetFoldLevel returns the previous level; margin-only redraw still invalidates.
	editor.ClearObservations();
	const int prev = editor.SetFoldLevel(1, static_cast<FoldLevel>(static_cast<int>(FoldLevel::Base)));
	CHECK(prev == static_cast<int>(FoldLevel::Base) + 1);
	const auto afterLevel = editor.Snapshot();
	const bool sawRedraw = afterLevel.invalidatedRectangles > 0 || afterLevel.invalidateAllCount > 0;
	CHECK(sawRedraw);
}

TEST_CASE("Doc and display line map after hide and show") {
	TestHost host;
	TestEditor editor(host);
	InstallSimpleFold(editor);

	CHECK(editor.GetAllLinesVisible() != 0);
	CHECK(editor.VisibleFromDocLine(0) == 0);
	CHECK(editor.VisibleFromDocLine(2) == 2);
	CHECK(editor.DocLineFromVisible(2) == 2);

	editor.ClearObservations();
	editor.HideLines(1, 1);
	CHECK(editor.GetLineVisible(1) == 0);
	CHECK(editor.GetAllLinesVisible() == 0);
	// Hiding the middle line compresses the display map: doc line 2 moves up.
	CHECK(editor.VisibleFromDocLine(2) == 1);
	CHECK(editor.DocLineFromVisible(1) == 2);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	CHECK(editor.Snapshot().scrollbarChanges > 0);

	editor.ShowLines(1, 1);
	CHECK(editor.GetLineVisible(1) != 0);
	CHECK(editor.GetAllLinesVisible() != 0);
	CHECK(editor.VisibleFromDocLine(2) == 2);
	CHECK(editor.DocLineFromVisible(1) == 1);
}

TEST_CASE("Toggle fold contracts and expands children with redraw") {
	TestHost host;
	TestEditor editor(host);
	InstallSimpleFold(editor);

	CHECK(editor.GetFoldExpanded(0) != 0);
	editor.ClearObservations();
	editor.ToggleFold(0);
	CHECK(editor.GetFoldExpanded(0) == 0);
	CHECK(editor.GetLineVisible(1) == 0);
	CHECK(editor.GetLineVisible(2) == 0);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.ClearObservations();
	editor.ToggleFold(0);
	CHECK(editor.GetFoldExpanded(0) != 0);
	CHECK(editor.GetLineVisible(1) != 0);
	CHECK(editor.GetLineVisible(2) != 0);

	// FoldLine Contract / Expand match Toggle around the same header.
	editor.FoldLine(0, FoldAction::Contract);
	CHECK(editor.GetFoldExpanded(0) == 0);
	editor.FoldLine(0, FoldAction::Expand);
	CHECK(editor.GetFoldExpanded(0) != 0);
}

TEST_CASE("Fold children fold all contracted-next and expand children") {
	TestHost host;
	TestEditor editor(host);
	// Nested: line 0 header base, line 1 header base+1, lines 2-3 children, line 4 sibling.
	editor.SetText("a\nb\nc\nd\ne\n");
	const int h0 = static_cast<int>(FoldLevel::Base) | static_cast<int>(FoldLevel::HeaderFlag);
	const int h1 = (static_cast<int>(FoldLevel::Base) + 1) | static_cast<int>(FoldLevel::HeaderFlag);
	const int c2 = static_cast<int>(FoldLevel::Base) + 2;
	editor.SetFoldLevel(0, static_cast<FoldLevel>(h0));
	editor.SetFoldLevel(1, static_cast<FoldLevel>(h1));
	editor.SetFoldLevel(2, static_cast<FoldLevel>(c2));
	editor.SetFoldLevel(3, static_cast<FoldLevel>(c2));
	editor.SetFoldLevel(4, static_cast<FoldLevel>(static_cast<int>(FoldLevel::Base) + 1));

	editor.FoldChildren(1, FoldAction::Contract);
	CHECK(editor.GetFoldExpanded(1) == 0);
	CHECK(editor.GetLineVisible(2) == 0);
	CHECK(editor.GetLineVisible(3) == 0);
	CHECK(editor.GetLineVisible(4) != 0);

	CHECK(editor.ContractedFoldNext(0) == 1);

	editor.ExpandChildren(1, static_cast<FoldLevel>(h1));
	CHECK(editor.GetFoldExpanded(1) != 0);
	CHECK(editor.GetLineVisible(2) != 0);

	editor.FoldAll(FoldAction::Contract);
	CHECK(editor.GetFoldExpanded(0) == 0);
	CHECK(editor.GetLineVisible(1) == 0);

	editor.FoldAll(FoldAction::Expand);
	CHECK(editor.GetFoldExpanded(0) != 0);
	CHECK(editor.GetAllLinesVisible() != 0);
}

TEST_CASE("EnsureVisible expands parents; automatic and display options round-trip") {
	TestHost host;
	TestEditor editor(host);
	InstallSimpleFold(editor);

	editor.ToggleFold(0);
	CHECK(editor.GetLineVisible(2) == 0);

	editor.ClearObservations();
	editor.EnsureVisible(2);
	CHECK(editor.GetLineVisible(2) != 0);
	CHECK(editor.GetFoldExpanded(0) != 0);

	const int autoFlags = static_cast<int>(AutomaticFold::Show) | static_cast<int>(AutomaticFold::Click);
	editor.SetAutomaticFold(static_cast<AutomaticFold>(autoFlags));
	CHECK(static_cast<int>(editor.GetAutomaticFold()) == autoFlags);

	// Clear pending redraw so the next Redraw is observed.
	editor.PaintAll();
	editor.ClearObservations();
	editor.SetFoldFlags(FoldFlag::LineAfterContracted);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.FoldDisplayTextSetStyle(FoldDisplayTextStyle::Boxed);
	CHECK(static_cast<FoldDisplayTextStyle>(editor.FoldDisplayTextGetStyle()) == FoldDisplayTextStyle::Boxed);

	const char *tag = "...";
	editor.PaintAll();
	editor.ClearObservations();
	editor.SetDefaultFoldDisplayText(tag);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	const char *got = editor.GetDefaultFoldDisplayText();
	REQUIRE(got != nullptr);
	CHECK(std::string(got) == tag);

	// ToggleFoldShowText contracts and attaches per-line display text.
	editor.FoldAll(FoldAction::Expand);
	const char *hidden = "hidden";
	editor.ToggleFoldShowText(0, hidden);
	CHECK(editor.GetFoldExpanded(0) == 0);
}

TEST_CASE("EnsureVisibleEnforcePolicy scrolls toward the target line") {
	TestHost host;
	TestEditor editor(host);
	// Enough lines that the target can sit below the first screen.
	std::string text;
	for (int i = 0; i < 40; ++i) {
		text += "line\n";
	}
	editor.SetText(text);
	editor.SetClientRectangle(PRectangle(0, 0, 200, 40));
	editor.PaintAll();

	CHECK(editor.GetFirstVisibleLine() == 0);
	editor.EnsureVisibleEnforcePolicy(30);
	// With a short client height the policy should scroll toward line 30.
	CHECK(editor.GetFirstVisibleLine() > 0);
	CHECK(editor.VisibleFromDocLine(30) >= 0);
}
