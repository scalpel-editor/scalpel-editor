// Fixed layout, hit-testing, and painting for the large-file open confirmation.

#ifndef LARGEFILECARD_H
#define LARGEFILECARD_H

#include <memory>
#include <string_view>

#include "Geometry.h"
#include "Platform.h"
#include "UiStyle.h"

namespace Scalpel {

struct LargeFileCardLayout {
	Scintilla::Internal::PRectangle scrim;
	Scintilla::Internal::PRectangle card;
	Scintilla::Internal::PRectangle title;
	Scintilla::Internal::PRectangle path;
	Scintilla::Internal::PRectangle openButton;
	Scintilla::Internal::PRectangle cancelButton;
};

enum class LargeFileCardHit {
	None,
	Open,
	Cancel,
};

/** Layout in logical client pixels for a client of the given size. */
[[nodiscard]] LargeFileCardLayout LayoutLargeFileCard(
	int width, int height) noexcept;

[[nodiscard]] LargeFileCardHit HitTestLargeFileCard(
	const LargeFileCardLayout &layout,
	Scintilla::Internal::Point point) noexcept;

/** Focus index: 0 Open, 1 Cancel. */
[[nodiscard]] int CycleLargeFileCardFocus(int focusedIndex, int delta) noexcept;

/**
 * Owns chrome fonts for the card. Construct once beside the UI and reuse
 * across frames.
 */
class LargeFileCardPainter final {
public:
	explicit LargeFileCardPainter(const UiStyle &style = DefaultUiStyle());
	~LargeFileCardPainter() = default;

	LargeFileCardPainter(const LargeFileCardPainter &) = delete;
	LargeFileCardPainter &operator=(const LargeFileCardPainter &) = delete;

	void Paint(Scintilla::Internal::Surface &surface,
		const LargeFileCardLayout &layout,
		std::string_view title,
		std::string_view path,
		int focusedButtonIndex) const;

	[[nodiscard]] const UiStyle &Style() const noexcept { return style; }

private:
	UiStyle style;
	std::shared_ptr<Scintilla::Internal::Font> titleFont;
	std::shared_ptr<Scintilla::Internal::Font> bodyFont;
};

}

#endif
