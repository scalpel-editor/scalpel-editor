#include "ApplicationTest.h"

#include "MarkdownStyles.h"

#include <cstddef>
#include <string>

using Scalpel::ApplicationEditor;
using Scalpel::EditorFont;
using Scalpel::MarkdownStyleBlockQuote;
using Scalpel::MarkdownStyleCode;
using Scalpel::MarkdownStyleCode2;
using Scalpel::MarkdownStyleCodeBlock;
using Scalpel::MarkdownStyleDefault;
using Scalpel::MarkdownStyleEm1;
using Scalpel::MarkdownStyleEm2;
using Scalpel::MarkdownStyleHRule;
using Scalpel::MarkdownStyleHeader1;
using Scalpel::MarkdownStyleHeader2;
using Scalpel::MarkdownStyleHeader3;
using Scalpel::MarkdownStyleHeader4;
using Scalpel::MarkdownStyleHeader5;
using Scalpel::MarkdownStyleHeader6;
using Scalpel::MarkdownStyleLineBegin;
using Scalpel::MarkdownStyleLink;
using Scalpel::MarkdownStyleOListItem;
using Scalpel::MarkdownStylePrechar;
using Scalpel::MarkdownStyleStrikeout;
using Scalpel::MarkdownStyleStrong1;
using Scalpel::MarkdownStyleStrong2;
using Scalpel::MarkdownStyleUListItem;

namespace {

constexpr EditorFont kFonts[] = {
	EditorFont::System,
	EditorFont::Monospace,
	EditorFont::Serif,
	EditorFont::Sans,
};

constexpr const char *kFamilies[] = {
	"system-ui",
	"monospace",
	"serif",
	"sans-serif",
};

constexpr int kMarkdownStyles[] = {
	MarkdownStyleDefault,
	MarkdownStyleLineBegin,
	MarkdownStyleStrong1,
	MarkdownStyleStrong2,
	MarkdownStyleEm1,
	MarkdownStyleEm2,
	MarkdownStyleHeader1,
	MarkdownStyleHeader2,
	MarkdownStyleHeader3,
	MarkdownStyleHeader4,
	MarkdownStyleHeader5,
	MarkdownStyleHeader6,
	MarkdownStylePrechar,
	MarkdownStyleUListItem,
	MarkdownStyleOListItem,
	MarkdownStyleBlockQuote,
	MarkdownStyleStrikeout,
	MarkdownStyleHRule,
	MarkdownStyleLink,
	MarkdownStyleCode,
	MarkdownStyleCode2,
	MarkdownStyleCodeBlock,
};

void CheckMarkdownPalette(ApplicationEditor &editor, const char *bodyFamily) {
	const int defaultStyle = static_cast<int>(Scintilla::StylesCommon::Default);
	const int lineNumber = static_cast<int>(Scintilla::StylesCommon::LineNumber);
	const int bodySize = editor.StyleSize(MarkdownStyleDefault);

	CHECK(editor.StyleFontName(defaultStyle) == bodyFamily);
	CHECK(editor.StyleFontName(MarkdownStyleDefault) == bodyFamily);
	CHECK(editor.StyleFontName(MarkdownStyleLineBegin) == bodyFamily);
	CHECK(editor.StyleFontName(lineNumber) == "monospace");

	CHECK(editor.StyleBold(MarkdownStyleStrong1));
	CHECK(editor.StyleBold(MarkdownStyleStrong2));
	CHECK(editor.StyleItalic(MarkdownStyleEm1));
	CHECK(editor.StyleItalic(MarkdownStyleEm2));
	CHECK(editor.StyleBold(MarkdownStyleHeader1));
	CHECK(editor.StyleBold(MarkdownStyleHeader2));
	CHECK(editor.StyleBold(MarkdownStyleHeader3));
	CHECK(editor.StyleBold(MarkdownStyleHeader4));
	CHECK(editor.StyleBold(MarkdownStyleHeader5));
	CHECK(editor.StyleBold(MarkdownStyleHeader6));
	CHECK_FALSE(editor.StyleBold(MarkdownStyleDefault));
	CHECK_FALSE(editor.StyleItalic(MarkdownStyleDefault));

	CHECK(editor.StyleFontName(MarkdownStyleCode) == "monospace");
	CHECK(editor.StyleFontName(MarkdownStyleCode2) == "monospace");
	CHECK(editor.StyleFontName(MarkdownStyleCodeBlock) == "monospace");
	CHECK(editor.StyleBack(MarkdownStyleCode) != editor.StyleBack(MarkdownStyleDefault));
	CHECK(editor.StyleBack(MarkdownStyleCode2) == editor.StyleBack(MarkdownStyleCode));
	CHECK(editor.StyleBack(MarkdownStyleCodeBlock) == editor.StyleBack(MarkdownStyleCode));

	CHECK(editor.StyleFore(MarkdownStyleLink) != editor.StyleFore(MarkdownStyleDefault));
	CHECK(editor.StyleFore(MarkdownStyleBlockQuote) != editor.StyleFore(MarkdownStyleDefault));
	CHECK(editor.StyleFore(MarkdownStyleUListItem) != editor.StyleFore(MarkdownStyleDefault));
	CHECK(editor.StyleFore(MarkdownStyleOListItem) != editor.StyleFore(MarkdownStyleDefault));
	CHECK(editor.StyleFore(MarkdownStyleHRule) != editor.StyleFore(MarkdownStyleDefault));
	CHECK(editor.StyleFore(MarkdownStyleStrikeout) != editor.StyleFore(MarkdownStyleDefault));
	CHECK(editor.StyleFore(MarkdownStylePrechar) != editor.StyleFore(MarkdownStyleDefault));

	for (int style : kMarkdownStyles) {
		CHECK(editor.StyleSize(style) == bodySize);
	}
}

}

TEST_CASE("production editor applies Markdown styles at startup") {
	ApplicationEditor editor(320, 180);
	CheckMarkdownPalette(editor, "system-ui");
}

TEST_CASE("production editor Markdown styles survive every font choice") {
	ApplicationEditor editor(320, 180);
	for (std::size_t i = 0; i < std::size(kFonts); ++i) {
		INFO("font=" << kFamilies[i]);
		editor.SetEditorFont(kFonts[i]);
		CHECK(editor.CurrentEditorFont() == kFonts[i]);
		CheckMarkdownPalette(editor, kFamilies[i]);
	}
}
