// Fixed logical layout, hit-testing, and opaque painting for the permanent tab strip
// below the menu bar. ApplicationUi owns TabStripModel; the shell feeds
// DocumentWorkspace tab snapshots into it and paints both chrome bands via
// ApplicationEditor permanent chrome. Layout accepts a top offset (MenuBarHeight)
// and stays Wayland-free.

#ifndef TABSTRIP_H
#define TABSTRIP_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "DocumentId.h"
#include "Geometry.h"
#include "Platform.h"
#include "UiStyle.h"

namespace Scalpel {

/** Fixed strip height in logical client pixels. */
[[nodiscard]] int TabStripHeight() noexcept;

/** Preferred width of one tab before overflow scrolling. */
[[nodiscard]] int TabStripPreferredTabWidth() noexcept;

/** One tab's display state for layout and paint. */
struct TabStripTab {
	DocumentId id = 0;
	std::string label;
	bool dirty = false;
	bool active = false;
};

/**
 * Transient strip input: tabs, hover, and horizontal scroll of the tab row.
 * scrollOffset is in logical pixels; Layout clamps it to the valid range.
 * Call ScrollTabStripToIndex when activation should reveal a tab.
 */
struct TabStripModel {
	std::vector<TabStripTab> tabs;
	/** Zero means no tab is hovered. */
	DocumentId hoveredId = 0;
	/** True when the pointer is over the close control of hoveredId. */
	bool closeHovered = false;
	int scrollOffset = 0;
};

/** Per-tab rectangles in full-frame coordinates after scroll is applied. */
struct TabStripTabLayout {
	DocumentId id = 0;
	Scintilla::Internal::PRectangle bounds;
	Scintilla::Internal::PRectangle label;
	Scintilla::Internal::PRectangle closeButton;
	bool dirty = false;
	bool active = false;
	/** Close is hit-testable: active tab, or inactive tab under the pointer. */
	bool showClose = false;
	/** Full label before paint-time truncation. */
	std::string labelText;
};

struct TabStripLayout {
	Scintilla::Internal::PRectangle strip;
	/** Clipped region that holds the scrolling tab row (excludes the add button). */
	Scintilla::Internal::PRectangle tabsViewport;
	Scintilla::Internal::PRectangle addButton;
	std::vector<TabStripTabLayout> tabs;
	/** Total width of the tab row before clipping. */
	int contentWidth = 0;
	/** Clamped scroll used to place tabs. */
	int scrollOffset = 0;
	/** Maximum legal scroll (contentWidth - viewport, or 0). */
	int maxScroll = 0;
};

enum class TabStripHit {
	None,
	/** Activate the named tab (body, including a hidden close region). */
	Tab,
	/** Close affordance on the named tab. */
	Close,
	/** New-tab control at the strip's trailing edge. */
	Add,
	/** Empty strip chrome (wheel may scroll). */
	Strip,
};

struct TabStripHitResult {
	TabStripHit kind = TabStripHit::None;
	DocumentId tabId = 0;
};

/**
 * Lay out the strip for stripWidth logical pixels at stripTop.
 * Height is TabStripHeight(). Zero or negative width yields an empty layout.
 * stripTop is the permanent-chrome y origin (0 when the strip is alone;
 * MenuBarHeight() when stacked under the menu bar). The bottom coordinate
 * saturates when stripTop is too close to INT_MAX for the full height.
 */
[[nodiscard]] TabStripLayout LayoutTabStrip(int stripWidth,
	const TabStripModel &model, int stripTop = 0) noexcept;

/**
 * Content width of tabCount preferred-width tabs, saturated at INT_MAX.
 * Used by scroll helpers without a full layout.
 */
[[nodiscard]] int TabStripContentWidth(std::size_t tabCount) noexcept;

[[nodiscard]] int TabStripViewportWidth(int stripWidth) noexcept;

/** Clamp scroll into [0, maxScroll] for the given strip and tab count. */
[[nodiscard]] int ClampTabStripScroll(int stripWidth, std::size_t tabCount,
	int scroll) noexcept;

/**
 * Adjust scroll so tabIndex is fully visible in the viewport when the tab is
 * narrower than the viewport; otherwise align its left edge.
 */
[[nodiscard]] int ScrollTabStripToIndex(int stripWidth, std::size_t tabCount,
	std::size_t tabIndex, int scroll) noexcept;

/**
 * Apply a horizontal wheel delta (positive scrolls content left / reveals
 * tabs to the right). Step is in logical pixels per notch unit.
 */
[[nodiscard]] int AdjustTabStripScroll(int stripWidth, std::size_t tabCount,
	int scroll, int delta, int step) noexcept;

[[nodiscard]] TabStripHitResult HitTestTabStrip(const TabStripLayout &layout,
	Scintilla::Internal::Point point) noexcept;

/**
 * Truncate label to maxWidth logical pixels using ellipsis when needed.
 * Cuts on UTF-8 character boundaries. Empty maxWidth or missing font yields {}.
 */
[[nodiscard]] std::string TruncateTabLabel(Scintilla::Internal::Surface &surface,
	const Scintilla::Internal::Font *font,
	std::string_view label,
	Scintilla::Internal::XYPOSITION maxWidth);

/**
 * Owns the strip font. Construct once beside the shell and reuse across frames.
 * Paints opaque strip chrome; does not dim the client or use the modal overlay path.
 */
class TabStripPainter final {
public:
	explicit TabStripPainter(const UiStyle &style = DefaultUiStyle());
	~TabStripPainter() = default;

	TabStripPainter(const TabStripPainter &) = delete;
	TabStripPainter &operator=(const TabStripPainter &) = delete;

	void Paint(Scintilla::Internal::Surface &surface,
		const TabStripLayout &layout,
		const TabStripModel &model) const;

	[[nodiscard]] const Scintilla::Internal::Font *LabelFont() const noexcept {
		return labelFont.get();
	}

	[[nodiscard]] const UiStyle &Style() const noexcept { return style; }

private:
	UiStyle style;
	std::shared_ptr<Scintilla::Internal::Font> labelFont;
};

}

#endif
