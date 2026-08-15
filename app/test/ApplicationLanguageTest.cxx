#include "ApplicationTest.h"

#include "MarkdownStyles.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using Scalpel::ApplicationEditor;
using Scalpel::DocumentLanguage;
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

constexpr std::string_view kLanguageSample =
	"# Heading\n"
	"plain\n"
	"**strong** and *em*\n"
	"`code` and [link](https://example.com)\n";

Scintilla::Position RequireFind(const std::string &text, std::string_view needle) {
	const auto pos = text.find(needle);
	REQUIRE(pos != std::string::npos);
	return static_cast<Scintilla::Position>(pos);
}

void CheckStyleRun(const ApplicationEditor &editor, Scintilla::Position pos,
	Scintilla::Position length, int expected) {
	for (Scintilla::Position i = 0; i < length; ++i) {
		CHECK(editor.StyleAt(pos + i) == expected);
	}
}

void CheckAllStylesZero(const ApplicationEditor &editor) {
	const Scintilla::Position length =
		static_cast<Scintilla::Position>(editor.Text().size());
	for (Scintilla::Position i = 0; i < length; ++i) {
		CHECK(editor.StyleAt(i) == 0);
	}
}

void CheckLanguageSampleStyles(const ApplicationEditor &editor) {
	const std::string text = editor.Text();
	CheckStyleRun(editor, RequireFind(text, "# Heading"), 9, MarkdownStyleHeader1);
	CheckStyleRun(editor, RequireFind(text, "plain"), 5, MarkdownStyleDefault);
	CheckStyleRun(editor, RequireFind(text, "**strong**"), 10, MarkdownStyleStrong1);
	CheckStyleRun(editor, RequireFind(text, "*em*"), 4, MarkdownStyleEm1);
	CheckStyleRun(editor, RequireFind(text, "`code`"), 6, MarkdownStyleCode);
	CheckStyleRun(editor, RequireFind(text, "[link](https://example.com)"), 27,
		MarkdownStyleLink);
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

TEST_CASE("production editor language defaults to plain text") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer(std::string(kLanguageSample));
	CHECK(editor.Language() == DocumentLanguage::PlainText);
	CHECK(editor.Language(editor.ActiveDocument()) == DocumentLanguage::PlainText);
	CHECK(editor.LexerLanguage().empty());
	CheckAllStylesZero(editor);
	CHECK_THROWS_WITH(editor.Language(0),
		"ApplicationEditor::Language requires a retained document");
	CHECK_THROWS_WITH(
		editor.SetDocumentLanguage(0, DocumentLanguage::Markdown),
		"ApplicationEditor::SetDocumentLanguage requires a retained document");
}

TEST_CASE("production editor attaches and detaches the Markdown lexer") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer(std::string(kLanguageSample));
	editor.HandleKeyboardInput(
		{Scintilla::Keys::End, Scintilla::KeyMod::Norm, {}, 1, true});
	editor.HandleKeyboardInput(
		{static_cast<Scintilla::Keys>(0), Scintilla::KeyMod::Norm, "!", 2, true});
	REQUIRE(editor.Modified());
	REQUIRE(editor.CanUndoEdit());
	const std::string textBefore = editor.Text();
	const auto selectionStart = editor.GetSelectionStart();
	const auto selectionEnd = editor.GetSelectionEnd();

	editor.SetDocumentLanguage(editor.ActiveDocument(), DocumentLanguage::Markdown);
	CHECK(editor.Language() == DocumentLanguage::Markdown);
	CHECK(editor.LexerLanguage() == "markdown");
	CHECK(editor.Text() == textBefore);
	CHECK(editor.GetSelectionStart() == selectionStart);
	CHECK(editor.GetSelectionEnd() == selectionEnd);
	CHECK(editor.Modified());
	CHECK(editor.CanUndoEdit());
	CheckLanguageSampleStyles(editor);

	editor.SetDocumentLanguage(editor.ActiveDocument(), DocumentLanguage::Markdown);
	CHECK(editor.LexerLanguage() == "markdown");
	CheckLanguageSampleStyles(editor);

	editor.SetDocumentLanguage(editor.ActiveDocument(), DocumentLanguage::PlainText);
	CHECK(editor.Language() == DocumentLanguage::PlainText);
	CHECK(editor.LexerLanguage().empty());
	CHECK(editor.Text() == textBefore);
	CHECK(editor.GetSelectionStart() == selectionStart);
	CHECK(editor.GetSelectionEnd() == selectionEnd);
	CHECK(editor.Modified());
	CheckAllStylesZero(editor);
}

TEST_CASE("production editor defers inactive language until activation") {
	ApplicationEditor editor(320, 180);
	const auto first = editor.ActiveDocument();
	editor.LoadInitialBuffer(std::string(kLanguageSample));
	const auto second = editor.CreateDocument();
	editor.ActivateDocument(second);
	editor.LoadInitialBuffer(std::string(kLanguageSample));
	editor.ActivateDocument(first);
	CHECK(editor.Language(second) == DocumentLanguage::PlainText);
	CHECK(editor.LexerLanguage().empty());
	CheckAllStylesZero(editor);

	editor.SetDocumentLanguage(second, DocumentLanguage::Markdown);
	CHECK(editor.Language(second) == DocumentLanguage::Markdown);
	CHECK(editor.Language(first) == DocumentLanguage::PlainText);
	CHECK(editor.LexerLanguage().empty());
	CheckAllStylesZero(editor);

	editor.ActivateDocument(second);
	CHECK(editor.Language() == DocumentLanguage::Markdown);
	CHECK(editor.LexerLanguage() == "markdown");
	CheckLanguageSampleStyles(editor);

	editor.SetDocumentLanguage(first, DocumentLanguage::Markdown);
	editor.SetDocumentLanguage(second, DocumentLanguage::PlainText);
	CHECK(editor.Language(first) == DocumentLanguage::Markdown);
	CHECK(editor.Language(second) == DocumentLanguage::PlainText);
	CHECK(editor.LexerLanguage().empty());
	CheckAllStylesZero(editor);

	editor.ActivateDocument(first);
	CHECK(editor.Language() == DocumentLanguage::Markdown);
	CHECK(editor.LexerLanguage() == "markdown");
	CheckLanguageSampleStyles(editor);

	editor.ActivateDocument(second);
	CHECK(editor.Language() == DocumentLanguage::PlainText);
	CHECK(editor.LexerLanguage().empty());
	CheckAllStylesZero(editor);
}

TEST_CASE("production editor recolourises Markdown after buffer load") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer(std::string(kLanguageSample));
	editor.SetDocumentLanguage(editor.ActiveDocument(), DocumentLanguage::Markdown);
	CheckLanguageSampleStyles(editor);

	editor.LoadInitialBuffer(std::string(kLanguageSample));
	CHECK(editor.Language() == DocumentLanguage::Markdown);
	CHECK(editor.LexerLanguage() == "markdown");
	CHECK_FALSE(editor.Modified());
	CheckLanguageSampleStyles(editor);
}

TEST_CASE("production editor Markdown styles survive font changes after lexing") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer(std::string(kLanguageSample));
	editor.SetDocumentLanguage(editor.ActiveDocument(), DocumentLanguage::Markdown);
	CheckLanguageSampleStyles(editor);

	for (std::size_t i = 0; i < std::size(kFonts); ++i) {
		INFO("font=" << kFamilies[i]);
		editor.SetEditorFont(kFonts[i]);
		CheckMarkdownPalette(editor, kFamilies[i]);
		CheckLanguageSampleStyles(editor);
	}
}

TEST_CASE("production editor paints Markdown style runs") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer(std::string(kLanguageSample));
	REQUIRE(editor.RenderFrame());
	const std::vector<uint8_t> plainPixels = editor.FramePixels();
	REQUIRE(plainPixels.size() == 320U * 180U * 4U);

	editor.SetDocumentLanguage(editor.ActiveDocument(), DocumentLanguage::Markdown);
	CheckLanguageSampleStyles(editor);
	REQUIRE(editor.RenderFrame());
	const std::vector<uint8_t> markdownPixels = editor.FramePixels();
	REQUIRE(markdownPixels.size() == plainPixels.size());
	CHECK(markdownPixels != plainPixels);

	bool hasNonBackgroundPixel = false;
	for (size_t offset = 4; offset < markdownPixels.size(); offset += 4) {
		if (!std::equal(markdownPixels.begin(), markdownPixels.begin() + 4,
				markdownPixels.begin() + offset)) {
			hasNonBackgroundPixel = true;
			break;
		}
	}
	CHECK(hasNonBackgroundPixel);
}
