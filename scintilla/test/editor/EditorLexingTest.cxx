// scalpel-editor test code
/** @file EditorLexingTest.cxx
 ** Focused tests for lexer attachment, properties, keywords, colourise,
 ** line state, and related Lexilla-facing surface.
 **/

#include <array>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

namespace {

// Minimal ILexer5: paints every byte with style 1, stores one property and one keyword list.
class TestLexer final : public ILexer5 {
public:
	std::map<std::string, std::string> props;
	std::string keywords;
	int lexCalls = 0;
	bool released = false;

	int SCI_METHOD Version() const override { return lvRelease5; }
	void SCI_METHOD Release() override { released = true; delete this; }
	const char *SCI_METHOD PropertyNames() override { return "test.prop\n"; }
	int SCI_METHOD PropertyType(const char *) override { return static_cast<int>(TypeProperty::String); }
	const char *SCI_METHOD DescribeProperty(const char *) override { return "test property"; }
	Sci_Position SCI_METHOD PropertySet(const char *key, const char *val) override {
		props[key ? key : ""] = val ? val : "";
		return 0;
	}
	const char *SCI_METHOD DescribeWordListSets() override { return "keywords"; }
	Sci_Position SCI_METHOD WordListSet(int n, const char *wl) override {
		if (n == 0)
			keywords = wl ? wl : "";
		return 0;
	}
	void SCI_METHOD Lex(Sci_PositionU startPos, Sci_Position lengthDoc, int, IDocument *pAccess) override {
		++lexCalls;
		if (!pAccess || lengthDoc <= 0)
			return;
		pAccess->StartStyling(static_cast<Sci_Position>(startPos));
		for (Sci_Position i = 0; i < lengthDoc; ++i)
			pAccess->SetStyleFor(1, 1);
	}
	void SCI_METHOD Fold(Sci_PositionU, Sci_Position, int, IDocument *) override {}
	void *SCI_METHOD PrivateCall(int, void *) override { return nullptr; }
	int SCI_METHOD LineEndTypesSupported() override { return static_cast<int>(LineEndType::Default); }
	int SCI_METHOD AllocateSubStyles(int, int) override { return -1; }
	int SCI_METHOD SubStylesStart(int) override { return -1; }
	int SCI_METHOD SubStylesLength(int) override { return 0; }
	int SCI_METHOD StyleFromSubStyle(int sub) override { return sub; }
	int SCI_METHOD PrimaryStyleFromStyle(int style) override { return style; }
	void SCI_METHOD FreeSubStyles() override {}
	void SCI_METHOD SetIdentifiers(int, const char *) override {}
	int SCI_METHOD DistanceToSecondaryStyles() override { return 0; }
	const char *SCI_METHOD GetSubStyleBases() override { return ""; }
	int SCI_METHOD NamedStyles() override { return 0; }
	const char *SCI_METHOD NameOfStyle(int) override { return nullptr; }
	const char *SCI_METHOD TagsOfStyle(int) override { return nullptr; }
	const char *SCI_METHOD DescriptionOfStyle(int) override { return nullptr; }
	const char *SCI_METHOD GetName() override { return "testlexer"; }
	int SCI_METHOD GetIdentifier() override { return 42; }
	const char *SCI_METHOD PropertyGet(const char *key) override {
		const auto it = props.find(key ? key : "");
		return it == props.end() ? "" : it->second.c_str();
	}
};

}

TEST_CASE("SetILexer attaches and Colourise styles the document") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abcd");
	auto *lexer = new TestLexer;
	editor.SetILexer(lexer);
	CHECK(editor.GetLexer() == 42);
	CHECK(std::string(editor.GetLexerLanguage()) == "testlexer");

	editor.Colourise(0, -1);
	CHECK(lexer->lexCalls >= 1);
	// Style byte at position 0 should be 1 after colourise.
	CHECK(editor.GetStyleAt(0) == 1);
}

TEST_CASE("Lexer property and keyword setters round-trip") {
	TestHost host;
	TestEditor editor(host);
	auto *lexer = new TestLexer;
	editor.SetILexer(lexer);
	editor.SetProperty("test.prop", "value");
	CHECK(std::string(editor.GetProperty("test.prop")) == "value");
	CHECK(editor.GetPropertyInt("missing", 7) == 7);
	editor.SetProperty("num", "9");
	CHECK(editor.GetPropertyInt("num", 0) == 9);
	editor.SetKeyWords(0, "if else while");
	CHECK(lexer->keywords == "if else while");
}

TEST_CASE("Line state set get and max") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("a\nb\nc");
	CHECK(editor.GetLineState(0) == 0);
	editor.SetLineState(1, 17);
	CHECK(editor.GetLineState(1) == 17);
	// GetMaxLineState is the number of lines that have state storage, not the max state value.
	CHECK(editor.GetMaxLineState() >= 2);
}

TEST_CASE("GetLineEndTypesSupported without lexer is default") {
	TestHost host;
	TestEditor editor(host);
	// No ILexer attached: LexState returns Default.
	CHECK(editor.GetLineEndTypesSupported() == LineEndType::Default);
}
