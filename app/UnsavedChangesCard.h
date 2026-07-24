// Fixed layout and hit-testing for the centered unsaved-changes card.

#ifndef UNSAVEDCHANGESCARD_H
#define UNSAVEDCHANGESCARD_H

#include "Geometry.h"

namespace Scalpel {

struct UnsavedChangesCardLayout {
	Scintilla::Internal::PRectangle scrim;
	Scintilla::Internal::PRectangle card;
	Scintilla::Internal::PRectangle title;
	Scintilla::Internal::PRectangle subtitle;
	Scintilla::Internal::PRectangle saveButton;
	Scintilla::Internal::PRectangle discardButton;
	Scintilla::Internal::PRectangle cancelButton;
};

enum class UnsavedCardHit {
	None,
	Save,
	Discard,
	Cancel,
};

/** Layout in logical client pixels for a client of the given size. */
[[nodiscard]] UnsavedChangesCardLayout LayoutUnsavedChangesCard(
	int width, int height) noexcept;

[[nodiscard]] UnsavedCardHit HitTestUnsavedChangesCard(
	const UnsavedChangesCardLayout &layout,
	Scintilla::Internal::Point point) noexcept;

/** Focus index: 0 Save, 1 Discard, 2 Cancel. */
[[nodiscard]] int CycleUnsavedCardFocus(int focusedIndex, int delta) noexcept;

}

#endif
