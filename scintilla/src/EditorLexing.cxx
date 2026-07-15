// Scintilla source code edit control
/** @file EditorLexing.cxx
 ** Lexer attachment (ILexer5), colourise, properties, keywords, substyles,
 ** named styles, line state, and the LexState adapter over Document.
 **
 ** SetILexer attaches a borrowed ILexer5; the lexer releases itself through
 ** ILexer5::Release when replaced or when the document drops the interface.
 ** Colourise runs the lexer or, for container lexing, asks the host for styles.
 ** Line state and line-end types supported are exchanged with the lexer and
 ** document for multi-line state machines. This surface is the future Lexilla
 ** attachment point.
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
#include "ScintillaMessages.h"
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

// Attach a borrowed ILexer5. Ownership and Release follow the existing LexInterface rules.
void ScintillaBase::SetILexer(ILexer5 *lexer) {
	DocumentLexState()->SetInstance(lexer);
}

// Run the lexer (or request container styles) over [start, end). end of -1 means the document end.
void ScintillaBase::Colourise(Sci::Position start, Sci::Position end) {
	if (DocumentLexState()->UseContainerLexing()) {
		pdoc->ModifiedAt(start);
		NotifyStyleToNeeded((end == -1) ? pdoc->Length() : end);
	} else {
		DocumentLexState()->Colourise(start, end);
	}
	Redraw();
}

// Set a lexer property key/value; may restyle from the first modified position.
void ScintillaBase::SetProperty(const char *key, const char *val) {
	DocumentLexState()->PropSet(key, val);
}

// Current property value, or empty/null when unset.
const char *ScintillaBase::GetProperty(const char *key) {
	return DocumentLexState()->PropGet(key);
}

// Integer property with default when missing or empty.
int ScintillaBase::GetPropertyInt(const char *key, int defaultValue) {
	return DocumentLexState()->PropGetInt(key, defaultValue);
}

// Keyword list n (0-based) as a space-separated string.
void ScintillaBase::SetKeyWords(int wordList, const char *keywords) {
	DocumentLexState()->SetWordList(wordList, keywords);
}

// Lexer numeric identifier from ILexer5::GetIdentifier.
int ScintillaBase::GetLexer() {
	return DocumentLexState()->GetIdentifier();
}

// Lexer language name from ILexer5::GetName.
const char *ScintillaBase::GetLexerLanguage() {
	return DocumentLexState()->GetName();
}

void *ScintillaBase::PrivateLexerCall(int operation, void *pointer) {
	return DocumentLexState()->PrivateCall(operation, pointer);
}

const char *ScintillaBase::PropertyNames() {
	return DocumentLexState()->PropertyNames();
}

TypeProperty ScintillaBase::PropertyType(const char *name) {
	return DocumentLexState()->PropertyType(name);
}

const char *ScintillaBase::DescribeProperty(const char *name) {
	return DocumentLexState()->DescribeProperty(name);
}

const char *ScintillaBase::DescribeKeyWordSets() {
	return DocumentLexState()->DescribeWordListSets();
}

LineEndType ScintillaBase::GetLineEndTypesSupported() {
	return DocumentLexState()->LineEndTypesSupported();
}

int ScintillaBase::AllocateSubStyles(int styleBase, int numberStyles) {
	return DocumentLexState()->AllocateSubStyles(styleBase, numberStyles);
}

int ScintillaBase::GetSubStylesStart(int styleBase) {
	return DocumentLexState()->SubStylesStart(styleBase);
}

int ScintillaBase::GetSubStylesLength(int styleBase) {
	return DocumentLexState()->SubStylesLength(styleBase);
}

int ScintillaBase::GetStyleFromSubStyle(int subStyle) {
	return DocumentLexState()->StyleFromSubStyle(subStyle);
}

int ScintillaBase::GetPrimaryStyleFromStyle(int style) {
	return DocumentLexState()->PrimaryStyleFromStyle(style);
}

void ScintillaBase::FreeSubStyles() {
	DocumentLexState()->FreeSubStyles();
}

void ScintillaBase::SetIdentifiers(int style, const char *identifiers) {
	DocumentLexState()->SetIdentifiers(style, identifiers);
}

int ScintillaBase::DistanceToSecondaryStyles() {
	return DocumentLexState()->DistanceToSecondaryStyles();
}

const char *ScintillaBase::GetSubStyleBases() {
	return DocumentLexState()->GetSubStyleBases();
}

int ScintillaBase::GetNamedStyles() {
	return DocumentLexState()->NamedStyles();
}

const char *ScintillaBase::NameOfStyle(int style) {
	return DocumentLexState()->NameOfStyle(style);
}

const char *ScintillaBase::TagsOfStyle(int style) {
	return DocumentLexState()->TagsOfStyle(style);
}

const char *ScintillaBase::DescriptionOfStyle(int style) {
	return DocumentLexState()->DescriptionOfStyle(style);
}

// Per-line integer state for multi-line lexer machines.
int Editor::SetLineState(Sci::Line line, int state) {
	return pdoc->SetLineState(line, state);
}

int Editor::GetLineState(Sci::Line line) const {
	return pdoc->GetLineState(line);
}

int Editor::GetMaxLineState() const noexcept {
	return static_cast<int>(pdoc->GetMaxLineState());
}

// Notify that lexer-dependent state in [start, end) changed and needs restyle.
void Editor::ChangeLexerState(Sci::Position start, Sci::Position end) {
	pdoc->ChangeLexerState(start, end);
}
