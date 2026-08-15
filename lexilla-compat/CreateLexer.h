// Factory for the in-tree Lexilla Markdown lexer.

#ifndef CREATELEXER_H
#define CREATELEXER_H

#include "ILexer.h"

extern "C" Scintilla::ILexer5 *CreateLexer(const char *name);

#endif
