// Fixed layout, hit-testing, and painting for the centered unsaved-changes card.

#ifndef UNSAVEDCHANGESCARD_H
#define UNSAVEDCHANGESCARD_H

#include <memory>
#include <string_view>

#include "Geometry.h"
#include "Platform.h"
#include "UiStyle.h"

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

/**
 * Owns chrome fonts for the card. Construct once beside the prompt and reuse
 * across frames.
 */
class UnsavedChangesCardPainter final {
public:
	explicit UnsavedChangesCardPainter(const UiStyle &style = DefaultUiStyle());
	~UnsavedChangesCardPainter() = default;

	UnsavedChangesCardPainter(const UnsavedChangesCardPainter &) = delete;
	UnsavedChangesCardPainter &operator=(const UnsavedChangesCardPainter &) = delete;

	void Paint(Scintilla::Internal::Surface &surface,
		const UnsavedChangesCardLayout &layout,
		std::string_view title,
		std::string_view subtitle,
		int focusedButtonIndex) const;

	[[nodiscard]] const UiStyle &Style() const noexcept { return style; }

private:
	UiStyle style;
	std::shared_ptr<Scintilla::Internal::Font> titleFont;
	std::shared_ptr<Scintilla::Internal::Font> bodyFont;
};

}

#endif
