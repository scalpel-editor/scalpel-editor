// Create the in-tree Lexilla Markdown lexer. Other names return null.

#include <cstring>

#include "ILexer.h"
#include "LexerModule.h"
#include "CreateLexer.h"

extern const Lexilla::LexerModule lmMarkdown;

extern "C" Scintilla::ILexer5 *CreateLexer(const char *name) {
	if (name && std::strcmp(name, "markdown") == 0) {
		return lmMarkdown.Create();
	}
	return nullptr;
}
