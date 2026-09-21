#include "SpellcheckDialog.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace Scalpel {
namespace {

using Scintilla::Internal::ColourRGBA;
using Scintilla::Internal::Fill;
using Scintilla::Internal::FillStroke;
using Scintilla::Internal::Font;
using Scintilla::Internal::FontParameters;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;
using Scintilla::Internal::Surface;

constexpr int kPreferredWidth = 680;
constexpr int kPreferredHeight = 312;
constexpr int kMinimumWidth = 260;
constexpr int kRowHeight = 28;
constexpr int kGap = 8;
constexpr int kButtonGap = 6;

bool Contains(const PRectangle &rectangle, Point point) noexcept {
	return !rectangle.Empty() && rectangle.Contains(point);
}

void DrawButton(Surface &surface, const PRectangle &rectangle,
	const Font *font, std::string_view label, bool focused,
	const UiStyle &style) {
	if (rectangle.Empty()) {
		return;
	}
	surface.FillRectangle(rectangle,
		Fill(focused ? style.focusFill : style.cardButtonFill));
	DrawCenteredLabel(surface, rectangle, font, label, style.text);
	DrawInsideFrame(surface, rectangle,
		focused ? style.focusBorder : style.cardButtonBorder,
		focused ? 2.0 : 1.0);
}

std::string PhaseTitle(const SpellcheckDialogModel &model) {
	switch (model.phase) {
	case SpellcheckDialogPhase::Loading:
		return "Checking spelling\u2026";
	case SpellcheckDialogPhase::Decision:
		return "Check Spelling";
	case SpellcheckDialogPhase::Complete:
		return "Spelling check complete";
	case SpellcheckDialogPhase::Failed:
		return "Spelling check stopped";
	}
	return "Check Spelling";
}

std::string DictionaryLabel(const SpellcheckDialogModel &model) {
	if (model.dictionaries.empty() || model.dictionary >= model.dictionaries.size()) {
		return "Dictionary: none";
	}
	return "Dictionary: " + model.dictionaries[model.dictionary].locale +
		"  (Left/Right to change)";
}

std::string SuggestionLabel(const SpellcheckDialogModel &model) {
	if (model.suggestions.empty() || model.suggestion >= model.suggestions.size()) {
		return "Suggestion: none";
	}
	return "Suggestion: " + model.suggestions[model.suggestion] +
		"  (Up/Down to choose)";
}

}

SpellcheckDialogLayout LayoutSpellcheckDialog(int width, int height) noexcept {
	SpellcheckDialogLayout layout;
	if (width <= 0 || height <= 0) {
		return layout;
	}
	const UiStyle &style = DefaultUiStyle();
	layout.scrim = PRectangle::FromInts(0, 0, width, height);
	const int maxWidth = std::max(0, width - 2 * style.cardMargin);
	int cardWidth = std::min(kPreferredWidth, maxWidth);
	if (width >= kMinimumWidth) {
		cardWidth = std::max(kMinimumWidth, cardWidth);
	}
	cardWidth = std::min(width, cardWidth);
	const int cardHeight = std::min(height, kPreferredHeight);
	const int left = std::max(0, (width - cardWidth) / 2);
	const int top = std::max(0, (height - cardHeight) / 2);
	layout.card = PRectangle::FromInts(left, top, left + cardWidth,
		top + cardHeight);

	const int pad = std::min(style.cardPad, cardWidth / 2);
	const int innerLeft = left + pad;
	const int innerRight = std::max(innerLeft, left + cardWidth - pad);
	const int bottom = top + cardHeight;
	int y = top + std::min(style.cardPad, cardHeight);
	auto row = [&](int rowHeight = kRowHeight) {
		const int rowBottom = std::min(bottom, y + rowHeight);
		PRectangle rectangle = PRectangle::FromInts(
			innerLeft, y, innerRight, rowBottom);
		y = std::min(bottom, rowBottom + kGap);
		return rectangle;
	};
	layout.title = row(style.cardTitleHeight);
	layout.dictionary = row();
	layout.context = row();
	layout.word = row();
	layout.suggestion = row();
	layout.replacement = row();
	if (!layout.replacement.Empty()) {
		layout.replacementText = PRectangle(
			layout.replacement.left + style.findFieldPadX,
			layout.replacement.top,
			std::max(layout.replacement.left + style.findFieldPadX,
				layout.replacement.right - style.findFieldPadX),
			layout.replacement.bottom);
	}

	const int buttonTop = std::min(bottom,
		std::max(y, bottom - style.cardPad - style.cardButtonHeight));
	const int buttonHeight = std::max(0,
		std::min(style.cardButtonHeight, bottom - buttonTop));
	const int innerWidth = std::max(0, innerRight - innerLeft);
	const int available = std::max(0, innerWidth - 5 * kButtonGap);
	const int buttonWidth = available / 6;
	const int remainder = available - buttonWidth * 6;
	std::array<PRectangle *, 6> buttons = {
		&layout.replaceButton, &layout.replaceAllButton, &layout.ignoreButton,
		&layout.ignoreAllButton, &layout.addButton, &layout.closeButton};
	int x = innerLeft;
	for (std::size_t index = 0; index < buttons.size(); ++index) {
		const int extra = index + 1 == buttons.size() ? remainder : 0;
		*buttons[index] = PRectangle::FromInts(
			x, buttonTop, x + buttonWidth + extra, buttonTop + buttonHeight);
		x += buttonWidth + extra + kButtonGap;
	}
	return layout;
}

SpellcheckDialogHit HitTestSpellcheckDialog(
	const SpellcheckDialogLayout &layout, Point point) noexcept {
	const std::array<std::pair<PRectangle, SpellcheckDialogHit>, 9> targets = {{
		{layout.dictionary, SpellcheckDialogHit::Dictionary},
		{layout.suggestion, SpellcheckDialogHit::Suggestion},
		{layout.replacement, SpellcheckDialogHit::Replacement},
		{layout.replaceButton, SpellcheckDialogHit::Replace},
		{layout.replaceAllButton, SpellcheckDialogHit::ReplaceAll},
		{layout.ignoreButton, SpellcheckDialogHit::Ignore},
		{layout.ignoreAllButton, SpellcheckDialogHit::IgnoreAll},
		{layout.addButton, SpellcheckDialogHit::Add},
		{layout.closeButton, SpellcheckDialogHit::Close},
	}};
	for (const auto &[rectangle, hit] : targets) {
		if (Contains(rectangle, point)) {
			return hit;
		}
	}
	return SpellcheckDialogHit::None;
}

SpellcheckDialogControl CycleSpellcheckDialogFocus(
	SpellcheckDialogControl focus, int delta) noexcept {
	constexpr int count = static_cast<int>(SpellcheckDialogControl::Close) + 1;
	int index = static_cast<int>(focus);
	index = (index + delta) % count;
	if (index < 0) {
		index += count;
	}
	return static_cast<SpellcheckDialogControl>(index);
}

SpellcheckDialogPainter::SpellcheckDialogPainter(const UiStyle &styleIn) :
	style(styleIn),
	titleFont(Font::Allocate(FontParameters{
		style.fontName, UiPixelSizeFromPoints(style.cardTitlePoints)})),
	bodyFont(Font::Allocate(FontParameters{
		style.fontName, UiPixelSizeFromPoints(style.cardBodyPoints)})) {
}

void SpellcheckDialogPainter::Paint(Surface &surface,
	const SpellcheckDialogLayout &layout,
	const SpellcheckDialogModel &model) const {
	surface.AlphaRectangle(layout.scrim, 0.0,
		FillStroke(style.cardScrim, ColourRGBA(0, 0, 0, 0), 0.0));
	if (!layout.card.Empty()) {
		surface.FillRectangle(layout.card, Fill(style.cardFill));
		DrawInsideFrame(surface, layout.card, style.cardBorder, 1.0);
	}
	DrawCenteredLabel(surface, layout.title, titleFont.get(), PhaseTitle(model),
		style.text);
	DrawLeftAlignedLabel(surface, layout.dictionary, bodyFont.get(),
		DictionaryLabel(model), style.mutedText);
	const std::string context = model.message.empty() ? model.context : model.message;
	DrawLeftAlignedLabel(surface, layout.context, bodyFont.get(), context,
		model.message.empty() ? style.mutedText : style.text);
	DrawLeftAlignedLabel(surface, layout.word, bodyFont.get(),
		model.word.empty() ? std::string_view{} :
			std::string_view(model.word), style.text);
	DrawLeftAlignedLabel(surface, layout.suggestion, bodyFont.get(),
		SuggestionLabel(model), style.text);

	FindBarLayout fieldLayout;
	fieldLayout.field = layout.replacement;
	fieldLayout.fieldText = layout.replacementText;
	PaintFindBarField(surface, fieldLayout, model.replacement, bodyFont.get(),
		style);
	const bool decision = model.phase == SpellcheckDialogPhase::Decision;
	DrawButton(surface, layout.replaceButton, bodyFont.get(), "Replace",
		decision && model.focus == SpellcheckDialogControl::Replace, style);
	DrawButton(surface, layout.replaceAllButton, bodyFont.get(), "Replace all",
		decision && model.focus == SpellcheckDialogControl::ReplaceAll, style);
	DrawButton(surface, layout.ignoreButton, bodyFont.get(), "Ignore",
		decision && model.focus == SpellcheckDialogControl::Ignore, style);
	DrawButton(surface, layout.ignoreAllButton, bodyFont.get(), "Ignore all",
		decision && model.focus == SpellcheckDialogControl::IgnoreAll, style);
	DrawButton(surface, layout.addButton, bodyFont.get(), "Add",
		decision && model.focus == SpellcheckDialogControl::Add, style);
	DrawButton(surface, layout.closeButton, bodyFont.get(), "Close",
		model.focus == SpellcheckDialogControl::Close, style);
}

}
