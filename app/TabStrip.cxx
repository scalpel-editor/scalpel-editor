#include "TabStrip.h"

#include <algorithm>
#include <limits>

namespace Scalpel {

namespace {

using Scintilla::Internal::ColourRGBA;
using Scintilla::Internal::Fill;
using Scintilla::Internal::Font;
using Scintilla::Internal::FontParameters;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;
using Scintilla::Internal::Surface;
using Scintilla::Internal::XYPOSITION;

constexpr int kStripHeight = 28;
constexpr int kPreferredTabWidth = 140;
constexpr int kAddButtonWidth = 28;
constexpr int kCloseSize = 14;
constexpr int kClosePadRight = 6;
constexpr int kLabelPadLeft = 10;
constexpr int kLabelPadRight = 4;
constexpr int kDefaultScrollStep = 40;

// FontParameters.size is device pixels (see FontPlatform FC_PIXEL_SIZE).
constexpr float PixelSizeFromPoints(float points) noexcept {
	return points * 96.0f / 72.0f;
}

bool NonEmpty(const PRectangle &rc) noexcept {
	return rc.right > rc.left && rc.bottom > rc.top;
}

bool NonEmptyContains(const PRectangle &rc, Point point) noexcept {
	// PRectangle documents half-open bounds, but Contains currently includes
	// right and bottom. Keep adjacent tab hit regions unambiguous here.
	return NonEmpty(rc) &&
		point.x >= rc.left && point.x < rc.right &&
		point.y >= rc.top && point.y < rc.bottom;
}

int SaturatingAdd(int value, int delta) noexcept {
	const int64_t result = static_cast<int64_t>(value) + delta;
	return static_cast<int>(std::clamp(
		result,
		static_cast<int64_t>(std::numeric_limits<int>::min()),
		static_cast<int64_t>(std::numeric_limits<int>::max())));
}

PRectangle CloseButtonRect(const PRectangle &tabBounds) noexcept {
	if (!NonEmpty(tabBounds)) {
		return PRectangle::FromInts(0, 0, 0, 0);
	}
	const int tabRight = static_cast<int>(tabBounds.right);
	const int tabTop = static_cast<int>(tabBounds.top);
	const int tabBottom = static_cast<int>(tabBounds.bottom);
	const int closeRight = tabRight - kClosePadRight;
	const int closeLeft = closeRight - kCloseSize;
	const int midY = static_cast<int>(
		static_cast<int64_t>(tabTop) +
		(static_cast<int64_t>(tabBottom) - tabTop) / 2);
	const int closeTop = std::max(tabTop,
		SaturatingAdd(midY, -kCloseSize / 2));
	const int closeBottom = std::min(tabBottom,
		SaturatingAdd(closeTop, kCloseSize));
	return PRectangle::FromInts(closeLeft, closeTop, closeRight,
		closeBottom);
}

PRectangle LabelRect(const PRectangle &tabBounds,
	const PRectangle &closeButton) noexcept {
	if (!NonEmpty(tabBounds)) {
		return PRectangle::FromInts(0, 0, 0, 0);
	}
	const int left = static_cast<int>(tabBounds.left) + kLabelPadLeft;
	const int right = NonEmpty(closeButton)
		? static_cast<int>(closeButton.left) - kLabelPadRight
		: static_cast<int>(tabBounds.right) - kLabelPadRight;
	if (right <= left) {
		return PRectangle::FromInts(0, 0, 0, 0);
	}
	return PRectangle::FromInts(left, static_cast<int>(tabBounds.top),
		right, static_cast<int>(tabBounds.bottom));
}

/** Walk back so cut is not mid UTF-8 sequence. */
std::size_t Utf8Floor(std::string_view text, std::size_t index) noexcept {
	if (index >= text.size()) {
		return text.size();
	}
	while (index > 0 &&
		(static_cast<unsigned char>(text[index]) & 0xC0) == 0x80) {
		--index;
	}
	return index;
}

// Near Platform chrome: strip is slightly darker than the active tab face.
const ColourRGBA kStripFill(0xe4, 0xe4, 0xe4, 0xff);
const ColourRGBA kStripBorder(0xb8, 0xb8, 0xb8, 0xff);
const ColourRGBA kActiveFill(0xfc, 0xfc, 0xfc, 0xff);
const ColourRGBA kInactiveFill(0xe4, 0xe4, 0xe4, 0xff);
const ColourRGBA kHoverFill(0xee, 0xee, 0xee, 0xff);
const ColourRGBA kActiveAccent(0x30, 0x70, 0xb0, 0xff);
const ColourRGBA kDirtyAccent(0xc0, 0x60, 0x20, 0xff);
const ColourRGBA kText(0x20, 0x20, 0x20, 0xff);
const ColourRGBA kMutedText(0x60, 0x60, 0x60, 0xff);
const ColourRGBA kCloseFill(0xd0, 0xd0, 0xd0, 0xff);
const ColourRGBA kCloseHoverFill(0xc0, 0x70, 0x70, 0xff);
const ColourRGBA kCloseInk(0x40, 0x40, 0x40, 0xff);
const ColourRGBA kAddInk(0x40, 0x40, 0x40, 0xff);

void DrawInsideFrame(Surface &surface, const PRectangle &rc, ColourRGBA colour,
	XYPOSITION thickness) {
	if (rc.Empty() || thickness <= 0.0) {
		return;
	}
	const XYPOSITION t = std::min(thickness, std::min(rc.Width(), rc.Height()) / 2.0);
	const Fill fill(colour);
	surface.FillRectangle(PRectangle(rc.left, rc.top, rc.right, rc.top + t), fill);
	surface.FillRectangle(PRectangle(rc.left, rc.bottom - t, rc.right, rc.bottom), fill);
	surface.FillRectangle(PRectangle(rc.left, rc.top + t, rc.left + t, rc.bottom - t), fill);
	surface.FillRectangle(PRectangle(rc.right - t, rc.top + t, rc.right, rc.bottom - t), fill);
}

void DrawLeftAlignedLabel(Surface &surface, const PRectangle &rc, const Font *font,
	std::string_view text, ColourRGBA fore) {
	if (!font || text.empty() || rc.Empty()) {
		return;
	}
	const XYPOSITION ascent = surface.Ascent(font);
	const XYPOSITION height = surface.Height(font);
	const XYPOSITION x = static_cast<XYPOSITION>(static_cast<int>(rc.left));
	const XYPOSITION ybase = static_cast<XYPOSITION>(static_cast<int>(
		rc.top + (rc.Height() - height) / 2.0 + ascent));
	const PRectangle textRc(x, rc.top, rc.right, rc.bottom);
	surface.DrawTextTransparent(textRc, font, ybase, text, fore);
}

void DrawCloseGlyph(Surface &surface, const PRectangle &rc, ColourRGBA ink) {
	if (rc.Empty()) {
		return;
	}
	const XYPOSITION inset = 3.0;
	const Point a(rc.left + inset, rc.top + inset);
	const Point b(rc.right - inset, rc.bottom - inset);
	const Point c(rc.right - inset, rc.top + inset);
	const Point d(rc.left + inset, rc.bottom - inset);
	surface.LineDraw(a, b, Scintilla::Internal::Stroke(ink, 1.5));
	surface.LineDraw(c, d, Scintilla::Internal::Stroke(ink, 1.5));
}

void DrawPlusGlyph(Surface &surface, const PRectangle &rc, ColourRGBA ink) {
	if (rc.Empty()) {
		return;
	}
	const XYPOSITION cx = (rc.left + rc.right) / 2.0;
	const XYPOSITION cy = (rc.top + rc.bottom) / 2.0;
	const XYPOSITION arm = 5.0;
	surface.LineDraw(Point(cx - arm, cy), Point(cx + arm, cy),
		Scintilla::Internal::Stroke(ink, 1.5));
	surface.LineDraw(Point(cx, cy - arm), Point(cx, cy + arm),
		Scintilla::Internal::Stroke(ink, 1.5));
}

int SaturatingTabPosition(std::size_t tabIndex) noexcept {
	constexpr std::size_t maxIndex =
		static_cast<std::size_t>(std::numeric_limits<int>::max()) /
		static_cast<std::size_t>(kPreferredTabWidth);
	if (tabIndex > maxIndex) {
		return std::numeric_limits<int>::max();
	}
	return static_cast<int>(tabIndex) * kPreferredTabWidth;
}

}

int TabStripHeight() noexcept {
	return kStripHeight;
}

int TabStripPreferredTabWidth() noexcept {
	return kPreferredTabWidth;
}

int TabStripContentWidth(std::size_t tabCount) noexcept {
	return SaturatingTabPosition(tabCount);
}

int TabStripViewportWidth(int stripWidth) noexcept {
	if (stripWidth <= 0) {
		return 0;
	}
	return std::max(0, stripWidth - kAddButtonWidth);
}

int ClampTabStripScroll(int stripWidth, std::size_t tabCount, int scroll) noexcept {
	const int content = TabStripContentWidth(tabCount);
	const int viewport = TabStripViewportWidth(stripWidth);
	const int maxScroll = std::max(0, content - viewport);
	if (scroll < 0) {
		return 0;
	}
	if (scroll > maxScroll) {
		return maxScroll;
	}
	return scroll;
}

int ScrollTabStripToIndex(int stripWidth, std::size_t tabCount,
	std::size_t tabIndex, int scroll) noexcept {
	if (tabCount == 0 || tabIndex >= tabCount) {
		return ClampTabStripScroll(stripWidth, tabCount, scroll);
	}
	const int viewport = TabStripViewportWidth(stripWidth);
	const int tabLeft = SaturatingTabPosition(tabIndex);
	const int tabRight = SaturatingAdd(tabLeft, kPreferredTabWidth);
	int next = scroll;
	if (tabLeft < next) {
		next = tabLeft;
	} else if (static_cast<int64_t>(tabRight) >
		static_cast<int64_t>(next) + viewport) {
		next = tabRight - viewport;
	}
	return ClampTabStripScroll(stripWidth, tabCount, next);
}

int AdjustTabStripScroll(int stripWidth, std::size_t tabCount, int scroll,
	int delta, int step) noexcept {
	const int usedStep = step > 0 ? step : kDefaultScrollStep;
	// Positive delta (typical wheel "down"/"right") reveals content to the right.
	const int64_t adjustment =
		static_cast<int64_t>(delta) * static_cast<int64_t>(usedStep);
	const int64_t candidate = static_cast<int64_t>(scroll) + adjustment;
	const int bounded = static_cast<int>(std::clamp(
		candidate,
		static_cast<int64_t>(std::numeric_limits<int>::min()),
		static_cast<int64_t>(std::numeric_limits<int>::max())));
	return ClampTabStripScroll(stripWidth, tabCount, bounded);
}

TabStripLayout LayoutTabStrip(int stripWidth, const TabStripModel &model,
	int stripTop) noexcept {
	const PRectangle empty = PRectangle::FromInts(0, 0, 0, 0);
	TabStripLayout layout{empty, empty, empty, {}, 0, 0, 0};
	if (stripWidth <= 0) {
		return layout;
	}

	const int top = stripTop;
	const int bottom = SaturatingAdd(top, kStripHeight);
	layout.strip = PRectangle::FromInts(0, top, stripWidth, bottom);

	const int addLeft = std::max(0, stripWidth - kAddButtonWidth);
	layout.addButton = PRectangle::FromInts(addLeft, top, stripWidth, bottom);
	layout.tabsViewport = PRectangle::FromInts(0, top, addLeft, bottom);

	layout.contentWidth = TabStripContentWidth(model.tabs.size());
	const int viewport = TabStripViewportWidth(stripWidth);
	layout.maxScroll = std::max(0, layout.contentWidth - viewport);

	layout.scrollOffset = ClampTabStripScroll(
		stripWidth, model.tabs.size(), model.scrollOffset);

	layout.tabs.reserve(model.tabs.size());
	for (std::size_t i = 0; i < model.tabs.size(); ++i) {
		const TabStripTab &src = model.tabs[i];
		const int unshiftedLeft = SaturatingTabPosition(i);
		const int left = unshiftedLeft - layout.scrollOffset;
		const int right = SaturatingAdd(left, kPreferredTabWidth);
		const PRectangle bounds = PRectangle::FromInts(left, top, right, bottom);
		const PRectangle closeButton = CloseButtonRect(bounds);
		const PRectangle label = LabelRect(bounds, closeButton);
		const bool showClose =
			src.active || (src.id != 0 && src.id == model.hoveredId);
		layout.tabs.push_back(TabStripTabLayout{
			src.id,
			bounds,
			label,
			closeButton,
			src.dirty,
			src.active,
			showClose,
			src.label,
		});
	}
	return layout;
}

TabStripHitResult HitTestTabStrip(const TabStripLayout &layout, Point point) noexcept {
	if (!NonEmptyContains(layout.strip, point)) {
		return {};
	}
	if (NonEmptyContains(layout.addButton, point)) {
		return {TabStripHit::Add, 0};
	}

	// Prefer close over tab body; only hit tabs that intersect the viewport.
	for (const TabStripTabLayout &tab : layout.tabs) {
		if (!tab.bounds.Intersects(layout.tabsViewport)) {
			continue;
		}
		// Clip hit testing to the viewport so overflowed chrome is not active.
		if (point.x < layout.tabsViewport.left ||
			point.x >= layout.tabsViewport.right) {
			continue;
		}
		if (tab.showClose && NonEmptyContains(tab.closeButton, point) &&
			tab.closeButton.Intersects(layout.tabsViewport)) {
			return {TabStripHit::Close, tab.id};
		}
		if (NonEmptyContains(tab.bounds, point)) {
			return {TabStripHit::Tab, tab.id};
		}
	}

	if (NonEmptyContains(layout.tabsViewport, point) ||
		NonEmptyContains(layout.strip, point)) {
		return {TabStripHit::Strip, 0};
	}
	return {};
}

std::string TruncateTabLabel(Surface &surface, const Font *font,
	std::string_view label, XYPOSITION maxWidth) {
	if (!font || maxWidth <= 0.0 || label.empty()) {
		return {};
	}
	if (surface.WidthText(font, label) <= maxWidth) {
		return std::string(label);
	}
	constexpr std::string_view kEllipsis = "\xE2\x80\xA6"; // …
	const XYPOSITION ellipsisWidth = surface.WidthText(font, kEllipsis);
	if (ellipsisWidth > maxWidth) {
		return {};
	}
	const XYPOSITION budget = maxWidth - ellipsisWidth;
	std::size_t lo = 0;
	std::size_t hi = label.size();
	std::size_t best = 0;
	while (lo <= hi) {
		const std::size_t mid = lo + (hi - lo) / 2;
		const std::size_t cut = Utf8Floor(label, mid);
		if (cut == 0) {
			if (mid == 0) {
				break;
			}
			hi = mid - 1;
			continue;
		}
		const std::string_view prefix = label.substr(0, cut);
		if (surface.WidthText(font, prefix) <= budget) {
			best = cut;
			if (mid >= label.size()) {
				break;
			}
			lo = mid + 1;
		} else {
			if (mid == 0) {
				break;
			}
			hi = mid - 1;
		}
	}
	if (best == 0) {
		return std::string(kEllipsis);
	}
	std::string out(label.substr(0, best));
	out.append(kEllipsis);
	return out;
}

TabStripPainter::TabStripPainter() {
	labelFont = Font::Allocate(FontParameters{"system-ui", PixelSizeFromPoints(12.0f)});
}

void TabStripPainter::Paint(Surface &surface, const TabStripLayout &layout,
	const TabStripModel &model) const {
	if (layout.strip.Empty()) {
		return;
	}

	surface.FillRectangle(layout.strip, Fill(kStripFill));
	// Bottom edge separates strip from the editor client.
	surface.FillRectangle(
		PRectangle(layout.strip.left, layout.strip.bottom - 1.0,
			layout.strip.right, layout.strip.bottom),
		Fill(kStripBorder));

	const Font *font = labelFont.get();

	// Clip the scrolling tab row so overflow does not paint over the add button.
	if (NonEmpty(layout.tabsViewport)) {
		surface.SetClip(layout.tabsViewport);
		for (const TabStripTabLayout &tab : layout.tabs) {
			if (!tab.bounds.Intersects(layout.tabsViewport)) {
				continue;
			}
			const bool hovered = tab.id != 0 && tab.id == model.hoveredId;
			ColourRGBA fill = kInactiveFill;
			if (tab.active) {
				fill = kActiveFill;
			} else if (hovered) {
				fill = kHoverFill;
			}
			surface.FillRectangle(tab.bounds, Fill(fill));

			if (tab.active) {
				const PRectangle accent(
					tab.bounds.left, tab.bounds.bottom - 2.0,
					tab.bounds.right, tab.bounds.bottom);
				surface.FillRectangle(accent, Fill(kActiveAccent));
			} else if (tab.dirty) {
				const PRectangle accent(
					tab.bounds.left, tab.bounds.bottom - 2.0,
					tab.bounds.right, tab.bounds.bottom);
				surface.FillRectangle(accent, Fill(kDirtyAccent));
			}

			// Right separator between tabs.
			surface.FillRectangle(
				PRectangle(tab.bounds.right - 1.0, tab.bounds.top + 4.0,
					tab.bounds.right, tab.bounds.bottom - 4.0),
				Fill(kStripBorder));

			if (NonEmpty(tab.label) && font) {
				const std::string drawn = TruncateTabLabel(surface, font,
					tab.labelText, tab.label.Width());
				const ColourRGBA ink = tab.active ? kText : kMutedText;
				DrawLeftAlignedLabel(surface, tab.label, font, drawn, ink);
			}

			if (tab.showClose && NonEmpty(tab.closeButton)) {
				const bool closeHot = hovered && model.closeHovered;
				surface.FillRectangle(tab.closeButton,
					Fill(closeHot ? kCloseHoverFill : kCloseFill));
				DrawCloseGlyph(surface, tab.closeButton, kCloseInk);
			}
		}
		surface.PopClip();
	}

	if (NonEmpty(layout.addButton)) {
		surface.FillRectangle(layout.addButton, Fill(kStripFill));
		DrawInsideFrame(surface, layout.addButton, kStripBorder, 1.0);
		DrawPlusGlyph(surface, layout.addButton, kAddInk);
	}
}

}
