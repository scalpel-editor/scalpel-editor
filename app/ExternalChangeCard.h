// Fixed layout, hit-testing, and painting for the external-change save card.

#ifndef EXTERNALCHANGECARD_H
#define EXTERNALCHANGECARD_H

#include <memory>
#include <string_view>

#include "Geometry.h"
#include "Platform.h"
#include "UiStyle.h"

namespace Scalpel {

struct ExternalChangeCardLayout {
	Scintilla::Internal::PRectangle scrim;
	Scintilla::Internal::PRectangle card;
	Scintilla::Internal::PRectangle title;
	Scintilla::Internal::PRectangle path;
	Scintilla::Internal::PRectangle overwriteButton;
	Scintilla::Internal::PRectangle reloadButton;
	Scintilla::Internal::PRectangle saveAsButton;
	Scintilla::Internal::PRectangle cancelButton;
};

enum class ExternalChangeCardHit {
	None,
	Overwrite,
	Reload,
	SaveAs,
	Cancel,
};

/** Layout in logical client pixels for a client of the given size. */
[[nodiscard]] ExternalChangeCardLayout LayoutExternalChangeCard(
	int width, int height) noexcept;

[[nodiscard]] ExternalChangeCardHit HitTestExternalChangeCard(
	const ExternalChangeCardLayout &layout,
	Scintilla::Internal::Point point) noexcept;

/** Focus index: 0 Overwrite, 1 Reload, 2 Save As, 3 Cancel. */
[[nodiscard]] int CycleExternalChangeCardFocus(int focusedIndex,
	int delta) noexcept;

/**
 * Owns chrome fonts for the card. Construct once beside the UI and reuse
 * across frames.
 */
class ExternalChangeCardPainter final {
public:
	explicit ExternalChangeCardPainter(const UiStyle &style = DefaultUiStyle());
	~ExternalChangeCardPainter() = default;

	ExternalChangeCardPainter(const ExternalChangeCardPainter &) = delete;
	ExternalChangeCardPainter &operator=(
		const ExternalChangeCardPainter &) = delete;

	void Paint(Scintilla::Internal::Surface &surface,
		const ExternalChangeCardLayout &layout,
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
