// Scintilla source code edit control
/** @file EditorLexing.cxx
 ** Lexer attachment (ILexer5), colourise, properties, keywords, substyles,
 ** named styles, line state, and the LexState adapter over Document.
 **
 ** SetILexer takes ownership of an ILexer5 and calls ILexer5::Release when the
 ** lexer is replaced or when the document drops the interface.
 ** Colourise runs the lexer or, for container lexing, asks the host for styles.
 ** Line state and line-end types supported are exchanged with the lexer and
 ** document for multi-line state machines. This surface is the future Lexilla
 ** attachment point.
 **
 ** String pointers returned by a lexer remain owned by that lexer and may all
 ** share reusable storage. Copy a returned string before making another lexer
 ** call.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cmath>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <algorithm>
#include <memory>

#include "ScintillaTypes.h"
#include "ScintillaStructures.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "CharacterCategoryMap.h"

#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "CallTip.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "EditorCommands.h"
#include "AutoComplete.h"
#include "ScintillaBase.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace Scintilla::Internal {

class LexState : public LexInterface {
public:
	explicit LexState(Document *pdoc_) noexcept;

	// LexInterface deleted the standard operators and defined the virtual destructor so don't need to here.

	const char *DescribeWordListSets();
	void SetWordList(int n, const char *wl);
	[[nodiscard]] int GetIdentifier() const;
	[[nodiscard]] const char *GetName() const;
	void *PrivateCall(int operation, void *pointer);
	const char *PropertyNames();
	TypeProperty PropertyType(const char *name);
	const char *DescribeProperty(const char *name);
	void PropSet(const char *key, const char *val);
	const char *PropGet(const char *key) const;
	int PropGetInt(const char *key, int defaultValue=0) const;

	LineEndType LineEndTypesSupported() override;
	int AllocateSubStyles(int styleBase, int numberStyles);
	int SubStylesStart(int styleBase);
	int SubStylesLength(int styleBase);
	int StyleFromSubStyle(int subStyle);
	int PrimaryStyleFromStyle(int style);
	void FreeSubStyles();
	void SetIdentifiers(int style, const char *identifiers);
	int DistanceToSecondaryStyles();
	const char *GetSubStyleBases();
	int NamedStyles();
	const char *NameOfStyle(int style);
	const char *TagsOfStyle(int style);
	const char *DescriptionOfStyle(int style);
};

}

LexState::LexState(Document *pdoc_) noexcept : LexInterface(pdoc_) {
}

LexState *ScintillaBase::DocumentLexState() {
	if (!pdoc->GetLexInterface()) {
		pdoc->SetLexInterface(std::make_unique<LexState>(pdoc));
	}
	return dynamic_cast<LexState *>(pdoc->GetLexInterface());
}

const char *LexState::DescribeWordListSets() {
	if (instance) {
		return instance->DescribeWordListSets();
	}
	return nullptr;
}

void LexState::SetWordList(int n, const char *wl) {
	if (instance) {
		const Sci_Position firstModification = instance->WordListSet(n, wl);
		if (firstModification >= 0) {
			pdoc->ModifiedAt(firstModification);
		}
	}
}

int LexState::GetIdentifier() const {
	if (instance) {
		return instance->GetIdentifier();
	}
	return 0;
}

const char *LexState::GetName() const {
	if (instance) {
		return instance->GetName();
	}
	return "";
}

void *LexState::PrivateCall(int operation, void *pointer) {
	if (instance) {
		return instance->PrivateCall(operation, pointer);
	}
	return nullptr;
}

const char *LexState::PropertyNames() {
	if (instance) {
		return instance->PropertyNames();
	}
	return nullptr;
}

TypeProperty LexState::PropertyType(const char *name) {
	if (instance) {
		return static_cast<TypeProperty>(instance->PropertyType(name));
	}
	return TypeProperty::Boolean;
}

const char *LexState::DescribeProperty(const char *name) {
	if (instance) {
		return instance->DescribeProperty(name);
	}
	return nullptr;
}

void LexState::PropSet(const char *key, const char *val) {
	if (instance) {
		const Sci_Position firstModification = instance->PropertySet(key, val);
		if (firstModification >= 0) {
			pdoc->ModifiedAt(firstModification);
		}
	}
}

const char *LexState::PropGet(const char *key) const {
	if (instance) {
		return instance->PropertyGet(key);
	}
	return nullptr;
}

int LexState::PropGetInt(const char *key, int defaultValue) const {
	if (instance) {
		const char *value = instance->PropertyGet(key);
		if (value && *value) {
			return atoi(value);
		}
	}
	return defaultValue;
}

LineEndType LexState::LineEndTypesSupported() {
	if (instance) {
		return static_cast<LineEndType>(instance->LineEndTypesSupported());
	}
	return LineEndType::Default;
}

int LexState::AllocateSubStyles(int styleBase, int numberStyles) {
	if (instance) {
		return instance->AllocateSubStyles(styleBase, numberStyles);
	}
	return -1;
}

int LexState::SubStylesStart(int styleBase) {
	if (instance) {
		return instance->SubStylesStart(styleBase);
	}
	return -1;
}

int LexState::SubStylesLength(int styleBase) {
	if (instance) {
		return instance->SubStylesLength(styleBase);
	}
	return 0;
}

int LexState::StyleFromSubStyle(int subStyle) {
	if (instance) {
		return instance->StyleFromSubStyle(subStyle);
	}
	return 0;
}

int LexState::PrimaryStyleFromStyle(int style) {
	if (instance) {
		return instance->PrimaryStyleFromStyle(style);
	}
	return 0;
}

void LexState::FreeSubStyles() {
	if (instance) {
		instance->FreeSubStyles();
	}
}

void LexState::SetIdentifiers(int style, const char *identifiers) {
	if (instance) {
		instance->SetIdentifiers(style, identifiers);
		pdoc->ModifiedAt(0);
	}
}

int LexState::DistanceToSecondaryStyles() {
	if (instance) {
		return instance->DistanceToSecondaryStyles();
	}
	return 0;
}

const char *LexState::GetSubStyleBases() {
	if (instance) {
		return instance->GetSubStyleBases();
	}
	return "";
}

int LexState::NamedStyles() {
	if (instance) {
		return instance->NamedStyles();
	}
	return -1;
}

const char *LexState::NameOfStyle(int style) {
	if (instance) {
		return instance->NameOfStyle(style);
	}
	return nullptr;
}

const char *LexState::TagsOfStyle(int style) {
	if (instance) {
		return instance->TagsOfStyle(style);
	}
	return nullptr;
}

const char *LexState::DescriptionOfStyle(int style) {
	if (instance) {
		return instance->DescriptionOfStyle(style);
	}
	return nullptr;
}

void ScintillaBase::NotifyStyleToNeeded(Sci::Position endStyleNeeded) {
	if (!DocumentLexState()->UseContainerLexing()) {
		const Sci::Position startStyling = pdoc->LineStartPosition(pdoc->GetEndStyled());
		DocumentLexState()->Colourise(startStyling, endStyleNeeded);
		return;
	}
	Editor::NotifyStyleToNeeded(endStyleNeeded);
}

// Take ownership of lexer. Its Release method is called when it is replaced or
// when the document drops the lexer interface. Pass null for container lexing.
void ScintillaBase::SetILexer(ILexer5 *lexer) {
	DocumentLexState()->SetInstance(lexer);
}

// Run the lexer over [start, end), including folding requested by the lexer, or
// ask the container to style that range. An end of -1 means the document end.
// Redraw after the request.
void ScintillaBase::Colourise(Sci::Position start, Sci::Position end) {
	if (DocumentLexState()->UseContainerLexing()) {
		pdoc->ModifiedAt(start);
		NotifyStyleToNeeded((end == -1) ? pdoc->Length() : end);
	} else {
		DocumentLexState()->Colourise(start, end);
	}
	Redraw();
}

// Pass a case-sensitive key and value to the lexer. When the lexer reports a
// changed position, mark the document to be styled again from that position.
void ScintillaBase::SetProperty(const char *key, const char *val) {
	DocumentLexState()->PropSet(key, val);
}

// Current property value as stored by the lexer, or null without a lexer.
const char *ScintillaBase::GetProperty(const char *key) {
	return DocumentLexState()->PropGet(key);
}

// Current property parsed as an integer. Return defaultValue when the property
// is missing or empty, and 0 when it is present but does not start with a number.
int ScintillaBase::GetPropertyInt(const char *key, int defaultValue) {
	return DocumentLexState()->PropGetInt(key, defaultValue);
}

// Pass a lexer-defined, zero-based keyword list. Lexers commonly split words on
// spaces, tabs, CR, and LF. When the lexer reports a changed position, mark the
// document to be styled again from that position.
void ScintillaBase::SetKeyWords(int wordList, const char *keywords) {
	DocumentLexState()->SetWordList(wordList, keywords);
}

// Lexer numeric identifier, or 0 when no lexer is attached. Some lexers have a
// name but no numeric identifier and also return 0.
int ScintillaBase::GetLexer() {
	return DocumentLexState()->GetIdentifier();
}

// Lexer language name, or an empty string when no lexer is attached.
const char *ScintillaBase::GetLexerLanguage() {
	return DocumentLexState()->GetName();
}

// Call a lexer-specific operation that the editor does not interpret.
void *ScintillaBase::PrivateLexerCall(int operation, void *pointer) {
	return DocumentLexState()->PrivateCall(operation, pointer);
}

// Newline-separated property names advertised by the lexer.
const char *ScintillaBase::PropertyNames() {
	return DocumentLexState()->PropertyNames();
}

// Whether a lexer property is boolean, integer, or string.
TypeProperty ScintillaBase::PropertyType(const char *name) {
	return DocumentLexState()->PropertyType(name);
}

// English description of a lexer property.
const char *ScintillaBase::DescribeProperty(const char *name) {
	return DocumentLexState()->DescribeProperty(name);
}

// Newline-separated descriptions of the lexer's keyword lists.
const char *ScintillaBase::DescribeKeyWordSets() {
	return DocumentLexState()->DescribeWordListSets();
}

// Bit set of line-end types understood by the lexer. Default means that only
// CR, LF, and CRLF are supported; Unicode adds the Unicode line separators.
LineEndType ScintillaBase::GetLineEndTypesSupported() {
	return DocumentLexState()->LineEndTypesSupported();
}

// Allocate contiguous substyles for styleBase and return the first one, or a
// negative value when the lexer cannot satisfy the request.
int ScintillaBase::AllocateSubStyles(int styleBase, int numberStyles) {
	return DocumentLexState()->AllocateSubStyles(styleBase, numberStyles);
}

// First allocated substyle for styleBase, or a negative value when absent.
int ScintillaBase::GetSubStylesStart(int styleBase) {
	return DocumentLexState()->SubStylesStart(styleBase);
}

// Number of allocated substyles for styleBase.
int ScintillaBase::GetSubStylesLength(int styleBase) {
	return DocumentLexState()->SubStylesLength(styleBase);
}

// Base style for a substyle; lexers return the argument when it is not a
// substyle.
int ScintillaBase::GetStyleFromSubStyle(int subStyle) {
	return DocumentLexState()->StyleFromSubStyle(subStyle);
}

// Primary style for a secondary style; lexers return the argument when it is
// already primary.
int ScintillaBase::GetPrimaryStyleFromStyle(int style) {
	return DocumentLexState()->PrimaryStyleFromStyle(style);
}

// Free every substyle allocated by the lexer.
void ScintillaBase::FreeSubStyles() {
	DocumentLexState()->FreeSubStyles();
}

// Set the identifiers recognized by a substyle and restyle the document. Unlike
// keyword lists, identifier lists do not support prefix matching.
void ScintillaBase::SetIdentifiers(int style, const char *identifiers) {
	DocumentLexState()->SetIdentifiers(style, identifiers);
}

// Distance from a primary style number to its corresponding secondary style.
int ScintillaBase::DistanceToSecondaryStyles() {
	return DocumentLexState()->DistanceToSecondaryStyles();
}

// Byte string containing each style that the lexer can split into substyles.
const char *ScintillaBase::GetSubStyleBases() {
	return DocumentLexState()->GetSubStyleBases();
}

// Number of styles for which the lexer supplies names and descriptions.
int ScintillaBase::GetNamedStyles() {
	return DocumentLexState()->NamedStyles();
}

// C or C++ identifier for a lexer style, such as SCE_C_COMMENTDOC.
const char *ScintillaBase::NameOfStyle(int style) {
	return DocumentLexState()->NameOfStyle(style);
}

// Space-separated descriptive tags for a lexer style.
const char *ScintillaBase::TagsOfStyle(int style) {
	return DocumentLexState()->TagsOfStyle(style);
}

// English description of a lexer style, suitable for display in a user
// interface.
const char *ScintillaBase::DescriptionOfStyle(int style) {
	return DocumentLexState()->DescriptionOfStyle(style);
}

// Set per-line lexer state and notify listeners when it changes. Return the
// previous state.
int Editor::SetLineState(Sci::Line line, int state) {
	return pdoc->SetLineState(line, state);
}

// Per-line lexer state, or 0 for a line without stored state.
int Editor::GetLineState(Sci::Line line) const {
	return pdoc->GetLineState(line);
}

// Number of entries allocated in the line-state store, not the greatest state
// value.
int Editor::GetMaxLineState() const noexcept {
	return static_cast<int>(pdoc->GetMaxLineState());
}

// Notify listeners that lexer-dependent state in [start, end) changed.
void Editor::ChangeLexerState(Sci::Position start, Sci::Position end) {
	pdoc->ChangeLexerState(start, end);
}
