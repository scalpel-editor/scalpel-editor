/** @file testDecoration.cxx
 ** Unit Tests for Scintilla internal data structures
 **/

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <algorithm>
#include <memory>

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorStyleTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"

#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"

#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "CellBuffer.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "CharacterCategoryMap.h"
#include "Document.h"
#include "UniConversion.h"

#include "catch.hpp"

constexpr int indicator=4;

using namespace Scintilla;
using namespace Scintilla::Internal;

// Test Decoration.

TEST_CASE("Decoration") {

	std::unique_ptr<IDecoration> deco = DecorationCreate(false, indicator);

	SECTION("HasCorrectIndicator") {
		REQUIRE(indicator == deco->Indicator());
	}

	SECTION("IsEmptyInitially") {
		REQUIRE(0 == deco->Length());
		REQUIRE(1 == deco->Runs());
		REQUIRE(deco->Empty());
	}

	SECTION("SimpleSpace") {
		deco->InsertSpace(0, 1);
		REQUIRE(deco->Empty());
	}

	SECTION("SimpleRun") {
		deco->InsertSpace(0, 1);
		deco->SetValueAt(0, 2);
		REQUIRE(!deco->Empty());
	}
}

// Test DecorationList.

TEST_CASE("DecorationList") {

	std::unique_ptr<IDecorationList> decol = DecorationListCreate(false);

	SECTION("HasCorrectIndicator") {
		decol->SetCurrentIndicator(indicator);
		REQUIRE(indicator == decol->GetCurrentIndicator());
	}

	SECTION("HasCorrectCurrentValue") {
		constexpr int value = 55;
		decol->SetCurrentValue(value);
		REQUIRE(value == decol->GetCurrentValue());
	}

	SECTION("ExpandSetValues") {
		decol->SetCurrentIndicator(indicator);
		decol->InsertSpace(0, 9);
		constexpr int value = 59;
		constexpr Sci::Position position = 4;
		constexpr Sci::Position fillLength = 3;
		auto fr = decol->FillRange(position, value, fillLength);
		REQUIRE(fr.changed);
		REQUIRE(fr.position == 4);
		REQUIRE(fr.fillLength == 3);
		REQUIRE(decol->ValueAt(indicator, 5) == value);
		REQUIRE(decol->AllOnFor(5) == (1 << indicator));
		REQUIRE(decol->Start(indicator, 5) == 4);
		REQUIRE(decol->End(indicator, 5) == 7);
		constexpr int indicatorB=6;
		decol->SetCurrentIndicator(indicatorB);
		fr = decol->FillRange(position, value, fillLength);
		REQUIRE(fr.changed);
		REQUIRE(decol->AllOnFor(5) == ((1 << indicator) | (1 << indicatorB)));
		decol->DeleteRange(5, 1);
		REQUIRE(decol->Start(indicatorB, 5) == 4);
		REQUIRE(decol->End(indicatorB, 5) == 6);
	}

}

// Document-owned decorations must track text inserts and deletes through
// NotifyModified so indicator ranges stay aligned with the substance.

TEST_CASE("Document decorations shift with insert and delete") {
	Document document(DocumentOption::Default);
	document.InsertString(0, "abcdef");
	document.DecorationSetCurrentIndicator(indicator);
	document.DecorationFillRange(1, 3, 3); // positions 1..3 -> "bcd"
	REQUIRE(document.decorations->ValueAt(indicator, 1) == 3);
	REQUIRE(document.decorations->ValueAt(indicator, 3) == 3);
	REQUIRE(document.decorations->ValueAt(indicator, 4) == 0);
	REQUIRE(document.decorations->Start(indicator, 2) == 1);
	REQUIRE(document.decorations->End(indicator, 2) == 4);

	// Mid-range insert extends the run (inherits indicator value at the split).
	document.InsertString(2, "X");
	REQUIRE(document.Length() == 7);
	REQUIRE(document.decorations->ValueAt(indicator, 1) == 3);
	REQUIRE(document.decorations->ValueAt(indicator, 2) == 3); // inserted
	REQUIRE(document.decorations->ValueAt(indicator, 4) == 3); // former end of run
	REQUIRE(document.decorations->ValueAt(indicator, 5) == 0);

	// Delete the inserted byte and one decorated byte.
	REQUIRE(document.DeleteChars(2, 2));
	REQUIRE(document.Length() == 5);
	REQUIRE(document.decorations->ValueAt(indicator, 1) == 3);
	REQUIRE(document.decorations->ValueAt(indicator, 2) == 3);
	REQUIRE(document.decorations->ValueAt(indicator, 3) == 0);
	REQUIRE(document.decorations->Start(indicator, 1) == 1);
	REQUIRE(document.decorations->End(indicator, 1) == 3);
}

