// Scintilla source code edit control
/** @file EditorHost.cxx
 ** Fixed-host notification policy, status reporting, and feature queries.
 **
 ** The modification-event mask controls which document changes reach the
 ** typed parent notification. Command events independently control the
 ** coarse change callback for text edits. Status stores the latest reported
 ** error or warning; setting Status::Ok clears it. Feature queries report the
 ** capabilities of the one active drawing surface.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include "Editor.h"
#include "EditorDocumentTypes.h"
#include "EditorInputTypes.h"
#include "EditorStyleTypes.h"
#include "Platform.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

void Editor::SetModEventMask(ModificationFlags eventMask) noexcept {
	modEventMask = eventMask;
}

ModificationFlags Editor::GetModEventMask() const noexcept {
	return modEventMask;
}

void Editor::SetCommandEvents(bool enabled) noexcept {
	commandEvents = enabled;
}

bool Editor::GetCommandEvents() const noexcept {
	return commandEvents;
}

void Editor::SetStatus(Status status) noexcept {
	errorStatus = status;
}

Status Editor::GetStatus() const noexcept {
	return errorStatus;
}

void Editor::NotifyErrorOccurred(Document *, void *, Status status) {
	SetStatus(status);
}

/// Query the active surface rather than predicting renderer capabilities in
/// the editor. The concrete renderer supplies the answers for this host.
int Editor::SupportsFeature(Supports feature) {
	AutoSurface surface(this);
	return surface->SupportsFeature(feature);
}
