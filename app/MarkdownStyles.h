// Lexilla 5.5.3 Markdown style numbers. Application-owned so consumers do
// not include the private SciLexer.h used only while compiling Lexilla.

#ifndef MARKDOWNSTYLES_H
#define MARKDOWNSTYLES_H

namespace Scalpel {

constexpr int MarkdownStyleDefault = 0;
constexpr int MarkdownStyleLineBegin = 1;
constexpr int MarkdownStyleStrong1 = 2;
constexpr int MarkdownStyleStrong2 = 3;
constexpr int MarkdownStyleEm1 = 4;
constexpr int MarkdownStyleEm2 = 5;
constexpr int MarkdownStyleHeader1 = 6;
constexpr int MarkdownStyleHeader2 = 7;
constexpr int MarkdownStyleHeader3 = 8;
constexpr int MarkdownStyleHeader4 = 9;
constexpr int MarkdownStyleHeader5 = 10;
constexpr int MarkdownStyleHeader6 = 11;
constexpr int MarkdownStylePrechar = 12;
constexpr int MarkdownStyleUListItem = 13;
constexpr int MarkdownStyleOListItem = 14;
constexpr int MarkdownStyleBlockQuote = 15;
constexpr int MarkdownStyleStrikeout = 16;
constexpr int MarkdownStyleHRule = 17;
constexpr int MarkdownStyleLink = 18;
constexpr int MarkdownStyleCode = 19;
constexpr int MarkdownStyleCode2 = 20;
constexpr int MarkdownStyleCodeBlock = 21;

}

#endif
