// Fixed layout, hit-testing, and painting for document file-operation errors.

#ifndef FILEERRORCARD_H
#define FILEERRORCARD_H

#include <memory>
#include <string_view>

#include "Geometry.h"
#include "Platform.h"
#include "UiStyle.h"

namespace Scalpel {

struct FileErrorCardLayout {
	Scintilla::Internal::PRectangle scrim;
	Scintilla::Internal::PRectangle card;
	Scintilla::Internal::PRectangle title;
	Scintilla::Internal::PRectangle path;
	Scintilla::Internal::PRectangle dismissButton;
};

/** Layout in logical client pixels for a client of the given size. */
[[nodiscard]] FileErrorCardLayout LayoutFileErrorCard(
	int width, int height) noexcept;

[[nodiscard]] bool HitTestFileErrorCard(const FileErrorCardLayout &layout,
	Scintilla::Internal::Point point) noexcept;

class FileErrorCardPainter final {
public:
	explicit FileErrorCardPainter(const UiStyle &style = DefaultUiStyle());
	~FileErrorCardPainter() = default;

	FileErrorCardPainter(const FileErrorCardPainter &) = delete;
	FileErrorCardPainter &operator=(const FileErrorCardPainter &) = delete;

	void Paint(Scintilla::Internal::Surface &surface,
		const FileErrorCardLayout &layout,
		std::string_view title,
		std::string_view path) const;

	[[nodiscard]] const UiStyle &Style() const noexcept { return style; }

private:
	const UiStyle &style;
	std::shared_ptr<Scintilla::Internal::Font> titleFont;
	std::shared_ptr<Scintilla::Internal::Font> bodyFont;
};

}

#endif
