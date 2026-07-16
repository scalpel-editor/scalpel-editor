// Scintilla source code edit control
/** @file ScintillaTypes.h
 ** Temporary umbrella for project-owned editor types (phase 5 step 9).
 **
 ** Retained enums and constants live in Editor*Types.h headers under src/.
 ** This file re-includes them so existing includes keep working until every
 ** consumer points at the concern headers and this file is deleted.
 **
 ** Accessibility and ScaleTechnique were client/message-only and are not
 ** re-homed. uptr_t / sptr_t remain here only until test consumers drop them.
 **/
// Copyright 1998-2019 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef SCINTILLATYPES_H
#define SCINTILLATYPES_H

#include <cstdint>

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorStyleTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"

namespace Scintilla {

// Temporary message-shaped pointer aliases; not a long-term project API.
// Removed when the last test consumer is converted (step 9).
using uptr_t = uintptr_t;
using sptr_t = intptr_t;

}

#endif
