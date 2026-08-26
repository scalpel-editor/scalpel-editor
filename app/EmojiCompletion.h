// Caret-anchored in-window emoji completion list. ApplicationUi owns the
// model, refreshes it from a live `:query` token, and paints it with
// permanent chrome rather than BoundOverlay so typing stays on the document.

#ifndef EMOJICOMPLETION_H
#define EMOJICOMPLETION_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ApplicationInput.h"
#include "EmojiCatalog.h"
#include "Geometry.h"
#include "Platform.h"
#include "Position.h"
#include "UiStyle.h"

namespace Scalpel {

/** Visible rows shown for one completion list. Extra matches are dropped. */
[[nodiscard]] int EmojiCompletionMaxRows() noexcept;

struct EmojiCompletionModel {
	bool open = false;
	std::vector<EmojiMatch> matches;
	std::size_t selected = 0;
	Scintilla::Position colonPos = 0;
	std::optional<std::size_t> hovered;
	std::optional<std::size_t> pressIndex;
};

struct EmojiCompletionItemLayout {
	std::size_t index = 0;
	Scintilla::Internal::PRectangle row;
	std::string label;
};

struct EmojiCompletionLayout {
	Scintilla::Internal::PRectangle panel;
	std::vector<EmojiCompletionItemLayout> items;
	bool aboveCaret = false;
};

enum class EmojiCompletionHit {
	None,
	Item,
	Panel,
};

struct EmojiCompletionHitResult {
	EmojiCompletionHit kind = EmojiCompletionHit::None;
	std::size_t index = 0;
};

struct EmojiCompletionKeyboardResult {
	bool consumed = false;
	bool dirty = false;
	bool dismissed = false;
	std::optional<EmojiMatch> completed;
};

struct EmojiCompletionPointerResult {
	bool consumed = false;
	bool dirty = false;
	bool dismissed = false;
	std::optional<EmojiMatch> completed;
};

void CloseEmojiCompletion(EmojiCompletionModel &model) noexcept;

/**
 * Lay out the open list in frame coordinates, clipped to client, below the
 * caret line or flipped above when it would not fit.
 */
[[nodiscard]] EmojiCompletionLayout LayoutEmojiCompletion(
	const EmojiCompletionModel &model, double caretX, double caretY,
	int lineHeight, Scintilla::Internal::PRectangle client) noexcept;

[[nodiscard]] EmojiCompletionHitResult HitTestEmojiCompletion(
	const EmojiCompletionLayout &layout,
	Scintilla::Internal::Point point) noexcept;

[[nodiscard]] EmojiCompletionKeyboardResult HandleEmojiCompletionKeyboard(
	EmojiCompletionModel &model, const KeyboardInput &input) noexcept;

/**
 * Pointer over the list highlights and can complete a row. A press outside
 * dismisses without consuming so the click still reaches the editor.
 */
[[nodiscard]] EmojiCompletionPointerResult HandleEmojiCompletionPointer(
	EmojiCompletionModel &model, const EmojiCompletionLayout &layout,
	const PointerInput &input) noexcept;

class EmojiCompletionPainter final {
public:
	explicit EmojiCompletionPainter(const UiStyle &style = DefaultUiStyle());
	~EmojiCompletionPainter() = default;

	EmojiCompletionPainter(const EmojiCompletionPainter &) = delete;
	EmojiCompletionPainter &operator=(const EmojiCompletionPainter &) = delete;

	void Paint(Scintilla::Internal::Surface &surface,
		const EmojiCompletionLayout &layout,
		const EmojiCompletionModel &model) const;

private:
	UiStyle style;
	std::shared_ptr<Scintilla::Internal::Font> labelFont;
};

}

#endif
