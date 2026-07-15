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

namespace {

// Three-line nest: header at 0, child at 1, last at 2.
// Paints so later Redraw calls are not swallowed by redrawPendingText.
void InstallSimpleFold(TestEditor &editor) {
	editor.SetText("header\nchild\nlast\n");
	const int header = static_cast<int>(FoldLevel::Base) | static_cast<int>(FoldLevel::HeaderFlag);
	const int child = static_cast<int>(FoldLevel::Base) + 1;
	editor.WndProc(Message::SetFoldLevel, 0, header);
	editor.WndProc(Message::SetFoldLevel, 1, child);
	editor.WndProc(Message::SetFoldLevel, 2, child);
	editor.PaintAll();
	editor.ClearObservations();
}

}  // namespace

TEST_CASE("Fold level parent last-child and message path") {
	TestHost host;
	TestEditor editor(host);
	InstallSimpleFold(editor);

	const int header = static_cast<int>(FoldLevel::Base) | static_cast<int>(FoldLevel::HeaderFlag);
	CHECK(editor.WndProc(Message::GetFoldLevel, 0, 0) == header);
	CHECK(editor.WndProc(Message::GetLastChild, 0, -1) == 2);
	CHECK(editor.WndProc(Message::GetFoldParent, 1, 0) == 0);
	CHECK(editor.WndProc(Message::GetFoldParent, 0, 0) == -1);

	// SetFoldLevel returns the previous level; margin-only redraw still invalidates.
	editor.ClearObservations();
	const sptr_t prev = editor.WndProc(Message::SetFoldLevel, 1, static_cast<int>(FoldLevel::Base));
	CHECK(prev == static_cast<int>(FoldLevel::Base) + 1);
	const auto afterLevel = editor.Snapshot();
	const bool sawRedraw = afterLevel.invalidatedRectangles > 0 || afterLevel.invalidateAllCount > 0;
	CHECK(sawRedraw);
}

TEST_CASE("Doc and display line map after hide and show") {
	TestHost host;
	TestEditor editor(host);
	InstallSimpleFold(editor);

	CHECK(editor.WndProc(Message::GetAllLinesVisible, 0, 0) != 0);
	CHECK(editor.WndProc(Message::VisibleFromDocLine, 0, 0) == 0);
	CHECK(editor.WndProc(Message::VisibleFromDocLine, 2, 0) == 2);
	CHECK(editor.WndProc(Message::DocLineFromVisible, 2, 0) == 2);

	editor.ClearObservations();
	editor.WndProc(Message::HideLines, 1, 1);
	CHECK(editor.WndProc(Message::GetLineVisible, 1, 0) == 0);
	CHECK(editor.WndProc(Message::GetAllLinesVisible, 0, 0) == 0);
	// Hiding the middle line compresses the display map: doc line 2 moves up.
	CHECK(editor.WndProc(Message::VisibleFromDocLine, 2, 0) == 1);
	CHECK(editor.WndProc(Message::DocLineFromVisible, 1, 0) == 2);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	CHECK(editor.Snapshot().scrollbarChanges > 0);

	editor.WndProc(Message::ShowLines, 1, 1);
	CHECK(editor.WndProc(Message::GetLineVisible, 1, 0) != 0);
	CHECK(editor.WndProc(Message::GetAllLinesVisible, 0, 0) != 0);
	CHECK(editor.WndProc(Message::VisibleFromDocLine, 2, 0) == 2);
	CHECK(editor.WndProc(Message::DocLineFromVisible, 1, 0) == 1);
}

TEST_CASE("Toggle fold contracts and expands children with redraw") {
	TestHost host;
	TestEditor editor(host);
	InstallSimpleFold(editor);

	CHECK(editor.WndProc(Message::GetFoldExpanded, 0, 0) != 0);
	editor.ClearObservations();
	editor.WndProc(Message::ToggleFold, 0, 0);
	CHECK(editor.WndProc(Message::GetFoldExpanded, 0, 0) == 0);
	CHECK(editor.WndProc(Message::GetLineVisible, 1, 0) == 0);
	CHECK(editor.WndProc(Message::GetLineVisible, 2, 0) == 0);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.ClearObservations();
	editor.WndProc(Message::ToggleFold, 0, 0);
	CHECK(editor.WndProc(Message::GetFoldExpanded, 0, 0) != 0);
	CHECK(editor.WndProc(Message::GetLineVisible, 1, 0) != 0);
	CHECK(editor.WndProc(Message::GetLineVisible, 2, 0) != 0);

	// FoldLine Contract / Expand match Toggle around the same header.
	editor.WndProc(Message::FoldLine, 0, static_cast<sptr_t>(FoldAction::Contract));
	CHECK(editor.WndProc(Message::GetFoldExpanded, 0, 0) == 0);
	editor.WndProc(Message::FoldLine, 0, static_cast<sptr_t>(FoldAction::Expand));
	CHECK(editor.WndProc(Message::GetFoldExpanded, 0, 0) != 0);
}

TEST_CASE("Fold children fold all contracted-next and expand children") {
	TestHost host;
	TestEditor editor(host);
	// Nested: line 0 header base, line 1 header base+1, lines 2-3 children, line 4 sibling.
	editor.SetText("a\nb\nc\nd\ne\n");
	const int h0 = static_cast<int>(FoldLevel::Base) | static_cast<int>(FoldLevel::HeaderFlag);
	const int h1 = (static_cast<int>(FoldLevel::Base) + 1) | static_cast<int>(FoldLevel::HeaderFlag);
	const int c2 = static_cast<int>(FoldLevel::Base) + 2;
	editor.WndProc(Message::SetFoldLevel, 0, h0);
	editor.WndProc(Message::SetFoldLevel, 1, h1);
	editor.WndProc(Message::SetFoldLevel, 2, c2);
	editor.WndProc(Message::SetFoldLevel, 3, c2);
	editor.WndProc(Message::SetFoldLevel, 4, static_cast<int>(FoldLevel::Base) + 1);

	editor.WndProc(Message::FoldChildren, 1, static_cast<sptr_t>(FoldAction::Contract));
	CHECK(editor.WndProc(Message::GetFoldExpanded, 1, 0) == 0);
	CHECK(editor.WndProc(Message::GetLineVisible, 2, 0) == 0);
	CHECK(editor.WndProc(Message::GetLineVisible, 3, 0) == 0);
	CHECK(editor.WndProc(Message::GetLineVisible, 4, 0) != 0);

	CHECK(editor.WndProc(Message::ContractedFoldNext, 0, 0) == 1);

	editor.WndProc(Message::ExpandChildren, 1, h1);
	CHECK(editor.WndProc(Message::GetFoldExpanded, 1, 0) != 0);
	CHECK(editor.WndProc(Message::GetLineVisible, 2, 0) != 0);

	editor.WndProc(Message::FoldAll, static_cast<uptr_t>(FoldAction::Contract), 0);
	CHECK(editor.WndProc(Message::GetFoldExpanded, 0, 0) == 0);
	CHECK(editor.WndProc(Message::GetLineVisible, 1, 0) == 0);

	editor.WndProc(Message::FoldAll, static_cast<uptr_t>(FoldAction::Expand), 0);
	CHECK(editor.WndProc(Message::GetFoldExpanded, 0, 0) != 0);
	CHECK(editor.WndProc(Message::GetAllLinesVisible, 0, 0) != 0);
}

TEST_CASE("EnsureVisible expands parents; automatic and display options round-trip") {
	TestHost host;
	TestEditor editor(host);
	InstallSimpleFold(editor);

	editor.WndProc(Message::ToggleFold, 0, 0);
	CHECK(editor.WndProc(Message::GetLineVisible, 2, 0) == 0);

	editor.ClearObservations();
	editor.WndProc(Message::EnsureVisible, 2, 0);
	CHECK(editor.WndProc(Message::GetLineVisible, 2, 0) != 0);
	CHECK(editor.WndProc(Message::GetFoldExpanded, 0, 0) != 0);

	const int autoFlags = static_cast<int>(AutomaticFold::Show) | static_cast<int>(AutomaticFold::Click);
	editor.WndProc(Message::SetAutomaticFold, static_cast<uptr_t>(autoFlags), 0);
	CHECK(editor.WndProc(Message::GetAutomaticFold, 0, 0) == autoFlags);

	// Clear pending redraw so the next Redraw is observed.
	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetFoldFlags, static_cast<uptr_t>(FoldFlag::LineAfterContracted), 0);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.WndProc(Message::FoldDisplayTextSetStyle, static_cast<uptr_t>(FoldDisplayTextStyle::Boxed), 0);
	CHECK(static_cast<FoldDisplayTextStyle>(
		editor.WndProc(Message::FoldDisplayTextGetStyle, 0, 0)) == FoldDisplayTextStyle::Boxed);

	const char *tag = "...";
	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetDefaultFoldDisplayText, 0, reinterpret_cast<sptr_t>(tag));
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	char buf[8] = {};
	const sptr_t n = editor.WndProc(Message::GetDefaultFoldDisplayText, 0, reinterpret_cast<sptr_t>(buf));
	CHECK(n == 3);
	CHECK(std::string(buf, static_cast<size_t>(n)) == tag);

	// ToggleFoldShowText contracts and attaches per-line display text.
	editor.WndProc(Message::FoldAll, static_cast<uptr_t>(FoldAction::Expand), 0);
	const char *hidden = "hidden";
	editor.WndProc(Message::ToggleFoldShowText, 0, reinterpret_cast<sptr_t>(hidden));
	CHECK(editor.WndProc(Message::GetFoldExpanded, 0, 0) == 0);
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
	editor.WndProc(Message::EnsureVisibleEnforcePolicy, 30, 0);
	// With a short client height the policy should scroll toward line 30.
	CHECK(editor.GetFirstVisibleLine() > 0);
	CHECK(editor.WndProc(Message::VisibleFromDocLine, 30, 0) >= 0);
}
