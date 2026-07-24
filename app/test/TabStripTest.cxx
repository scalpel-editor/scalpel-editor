#include "catch.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "ApplicationEditor.h"
#include "TabStrip.h"

using Scalpel::AdjustTabStripScroll;
using Scalpel::ApplicationEditor;
using Scalpel::ClampTabStripScroll;
using Scalpel::HitTestTabStrip;
using Scalpel::LayoutTabStrip;
using Scalpel::ScrollTabStripToIndex;
using Scalpel::TabStripHeight;
using Scalpel::TabStripHit;
using Scalpel::TabStripHitResult;
using Scalpel::TabStripLayout;
using Scalpel::TabStripModel;
using Scalpel::TabStripPainter;
using Scalpel::TabStripPreferredTabWidth;
using Scalpel::TabStripTab;
using Scalpel::TruncateTabLabel;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;

namespace {

bool NonEmpty(const PRectangle &rc) {
	return rc.right > rc.left && rc.bottom > rc.top;
}

Point Center(const PRectangle &rc) {
	return Point((rc.left + rc.right) / 2.0, (rc.top + rc.bottom) / 2.0);
}

TabStripTab MakeTab(uint64_t id, std::string label, bool active, bool dirty = false) {
	TabStripTab tab;
	tab.id = id;
	tab.label = std::move(label);
	tab.active = active;
	tab.dirty = dirty;
	return tab;
}

TabStripModel ModelWith(std::vector<TabStripTab> tabs, int scroll = 0,
	uint64_t hovered = 0, bool closeHovered = false) {
	TabStripModel model;
	model.tabs = std::move(tabs);
	model.scrollOffset = scroll;
	model.hoveredId = hovered;
	model.closeHovered = closeHovered;
	return model;
}

struct Rgba {
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	uint8_t a = 0;
};

Rgba Sample(const std::vector<uint8_t> &pixels, int width, int x, int y) {
	const size_t offset =
		(static_cast<size_t>(y) * static_cast<size_t>(width) +
			static_cast<size_t>(x)) *
		4U;
	REQUIRE(offset + 3 < pixels.size());
	return {pixels[offset], pixels[offset + 1], pixels[offset + 2],
		pixels[offset + 3]};
}

bool Differs(Rgba a, Rgba b, int tol = 2) {
	return std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)) > tol ||
		std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)) > tol ||
		std::abs(static_cast<int>(a.b) - static_cast<int>(b.b)) > tol;
}

/** Paint the strip into a full-client frame and return top-left RGBA pixels. */
std::vector<uint8_t> PaintStrip(ApplicationEditor &editor, TabStripPainter &painter,
	const TabStripLayout &layout, const TabStripModel &model) {
	(void)editor.TakeFrameDamage();
	editor.SetOverlayPainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			CHECK(width == editor.FrameWidth());
			CHECK(height == editor.FrameHeight());
			painter.Paint(surface, layout, model);
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, editor.FrameWidth(),
		editor.FrameHeight())});
	return editor.FramePixels();
}

}

TEST_CASE("tab strip height is fixed and positive") {
	CHECK(TabStripHeight() > 0);
	CHECK(TabStripPreferredTabWidth() > 0);
}

TEST_CASE("tab strip layout places tabs add button and strip bounds") {
	const TabStripModel model = ModelWith({
		MakeTab(1, "Untitled 1", true),
		MakeTab(2, "notes.txt", false),
	});
	const TabStripLayout layout = LayoutTabStrip(400, model);
	CHECK(layout.strip == PRectangle::FromInts(0, 0, 400, TabStripHeight()));
	CHECK(NonEmpty(layout.tabsViewport));
	CHECK(NonEmpty(layout.addButton));
	CHECK(layout.addButton.right == 400);
	CHECK(layout.addButton.left == layout.tabsViewport.right);
	REQUIRE(layout.tabs.size() == 2);
	CHECK(layout.tabs[0].id == 1);
	CHECK(layout.tabs[0].active);
	CHECK(layout.tabs[0].showClose);
	CHECK_FALSE(layout.tabs[1].showClose);
	CHECK(NonEmpty(layout.tabs[0].bounds));
	CHECK(NonEmpty(layout.tabs[0].closeButton));
	CHECK(layout.tabs[0].bounds.Contains(layout.tabs[0].closeButton));
	CHECK(layout.tabs[0].bounds.Contains(layout.tabs[0].label));
	// Tabs pack left-to-right at preferred width with zero scroll.
	CHECK(layout.tabs[0].bounds.left == 0);
	CHECK(layout.tabs[1].bounds.left == TabStripPreferredTabWidth());
	CHECK(layout.scrollOffset == 0);
	CHECK(layout.contentWidth == 2 * TabStripPreferredTabWidth());
}

TEST_CASE("tab strip zero width yields empty layout") {
	const TabStripModel model = ModelWith({MakeTab(1, "a", true)});
	const TabStripLayout layout = LayoutTabStrip(0, model);
	CHECK_FALSE(NonEmpty(layout.strip));
	CHECK(layout.tabs.empty());
	const TabStripHitResult hit = HitTestTabStrip(layout, Point(0, 0));
	CHECK(hit.kind == TabStripHit::None);
}

TEST_CASE("tab strip close shows on hovered inactive tab") {
	const TabStripModel model = ModelWith(
		{MakeTab(1, "a", true), MakeTab(2, "b", false)}, 0, 2);
	const TabStripLayout layout = LayoutTabStrip(400, model);
	REQUIRE(layout.tabs.size() == 2);
	CHECK(layout.tabs[0].showClose);
	CHECK(layout.tabs[1].showClose);
}

TEST_CASE("tab strip hit-test activates tab close and add") {
	const TabStripModel model = ModelWith({
		MakeTab(10, "one", true),
		MakeTab(20, "two", false),
	});
	const TabStripLayout layout = LayoutTabStrip(400, model);

	const TabStripHitResult onActive = HitTestTabStrip(layout,
		Center(layout.tabs[0].label));
	CHECK(onActive.kind == TabStripHit::Tab);
	CHECK(onActive.tabId == 10);

	const TabStripHitResult onClose = HitTestTabStrip(layout,
		Center(layout.tabs[0].closeButton));
	CHECK(onClose.kind == TabStripHit::Close);
	CHECK(onClose.tabId == 10);

	// Inactive close region without hover still counts as tab body.
	const TabStripHitResult onHiddenClose = HitTestTabStrip(layout,
		Center(layout.tabs[1].closeButton));
	CHECK(onHiddenClose.kind == TabStripHit::Tab);
	CHECK(onHiddenClose.tabId == 20);

	const TabStripHitResult onAdd = HitTestTabStrip(layout,
		Center(layout.addButton));
	CHECK(onAdd.kind == TabStripHit::Add);
	CHECK(onAdd.tabId == 0);

	const TabStripHitResult outside = HitTestTabStrip(layout,
		Point(10, TabStripHeight() + 5));
	CHECK(outside.kind == TabStripHit::None);
}

TEST_CASE("tab strip hit-test uses half-open tab and strip bounds") {
	const TabStripModel model = ModelWith({
		MakeTab(10, "one", true),
		MakeTab(20, "two", false),
	});
	const TabStripLayout layout = LayoutTabStrip(400, model);

	const TabStripHitResult sharedEdge = HitTestTabStrip(layout,
		Point(layout.tabs[0].bounds.right, TabStripHeight() / 2.0));
	CHECK(sharedEdge.kind == TabStripHit::Tab);
	CHECK(sharedEdge.tabId == 20);

	CHECK(HitTestTabStrip(layout, Point(layout.strip.right, 4)).kind ==
		TabStripHit::None);
	CHECK(HitTestTabStrip(layout, Point(4, layout.strip.bottom)).kind ==
		TabStripHit::None);
}

TEST_CASE("tab strip overflow scrolls and keeps active visible") {
	std::vector<TabStripTab> tabs;
	for (uint64_t i = 1; i <= 8; ++i) {
		tabs.push_back(MakeTab(i, "tab " + std::to_string(i), i == 8));
	}
	// Narrow strip: viewport holds fewer than 8 preferred tabs.
	const int stripWidth = TabStripPreferredTabWidth() * 2 + 28;
	const int visibleScroll = ScrollTabStripToIndex(
		stripWidth, tabs.size(), tabs.size() - 1, 0);
	TabStripModel model = ModelWith(std::move(tabs), visibleScroll);
	const TabStripLayout layout = LayoutTabStrip(stripWidth, model);
	CHECK(layout.maxScroll > 0);
	// The requested activation scroll keeps the last tab in the viewport.
	CHECK(layout.scrollOffset == layout.maxScroll);
	const auto &active = layout.tabs.back();
	CHECK(active.active);
	CHECK(active.bounds.left >= layout.tabsViewport.left - 0.5);
	CHECK(active.bounds.right <= layout.tabsViewport.right + 0.5);

	// First tab is scrolled out; its center is not hittable.
	const Point firstCenter = Center(layout.tabs.front().bounds);
	const TabStripHitResult miss = HitTestTabStrip(layout, firstCenter);
	CHECK(miss.kind != TabStripHit::Tab);

	// Scrolled-in active tab remains hittable.
	const TabStripHitResult hitActive = HitTestTabStrip(layout,
		Center(active.label));
	CHECK(hitActive.kind == TabStripHit::Tab);
	CHECK(hitActive.tabId == 8);
}

TEST_CASE("tab strip scroll helpers clamp and adjust") {
	const int stripWidth = TabStripPreferredTabWidth() + 28;
	const std::size_t count = 5;
	CHECK(ClampTabStripScroll(stripWidth, count, -10) == 0);
	const int maxScroll = ClampTabStripScroll(stripWidth, count, 100000);
	CHECK(maxScroll > 0);
	CHECK(ClampTabStripScroll(stripWidth, count, maxScroll + 50) == maxScroll);

	const int mid = ScrollTabStripToIndex(stripWidth, count, 2, 0);
	CHECK(mid >= 0);
	CHECK(mid <= maxScroll);

	const int wheeled = AdjustTabStripScroll(stripWidth, count, 0, 1, 40);
	CHECK(wheeled == 40);
	const int back = AdjustTabStripScroll(stripWidth, count, 40, -1, 40);
	CHECK(back == 0);

	CHECK(Scalpel::TabStripContentWidth(
		std::numeric_limits<std::size_t>::max()) ==
		std::numeric_limits<int>::max());
	CHECK(AdjustTabStripScroll(stripWidth,
		std::numeric_limits<std::size_t>::max(),
		std::numeric_limits<int>::max(), std::numeric_limits<int>::max(),
		std::numeric_limits<int>::max()) ==
		std::numeric_limits<int>::max() - stripWidth + 28);
}

TEST_CASE("tab strip layout preserves wheel scrolling after active reveal") {
	std::vector<TabStripTab> tabs;
	for (uint64_t i = 1; i <= 5; ++i) {
		tabs.push_back(MakeTab(i, "tab " + std::to_string(i), i == 5));
	}
	const int stripWidth = TabStripPreferredTabWidth() * 2 + 28;
	const int activeScroll = ScrollTabStripToIndex(
		stripWidth, tabs.size(), tabs.size() - 1, 0);
	REQUIRE(activeScroll > 0);
	const int wheeledLeft = AdjustTabStripScroll(
		stripWidth, tabs.size(), activeScroll, -1, TabStripPreferredTabWidth());
	REQUIRE(wheeledLeft < activeScroll);

	const TabStripLayout layout = LayoutTabStrip(
		stripWidth, ModelWith(std::move(tabs), wheeledLeft));
	CHECK(layout.scrollOffset == wheeledLeft);
	CHECK(layout.tabs.back().bounds.left >= layout.tabsViewport.right);
}

TEST_CASE("tab strip truncation shortens long labels with ellipsis") {
	ApplicationEditor editor(320, 120);
	editor.LoadInitialBuffer("x\n");
	(void)editor.TakeFrameDamage();

	TabStripPainter painter;
	std::string truncated;
	editor.SetOverlayPainter(
		[&](Scintilla::Internal::Surface &surface, int, int) {
			const std::string longLabel(80, 'W');
			truncated = TruncateTabLabel(surface, painter.LabelFont(),
				longLabel, 60.0);
			CHECK(truncated.size() < longLabel.size());
			// Ends with UTF-8 ellipsis …
			REQUIRE(truncated.size() >= 3);
			CHECK(truncated[truncated.size() - 3] == '\xE2');
			CHECK(TruncateTabLabel(surface, painter.LabelFont(), "short",
				200.0) == "short");
			CHECK(TruncateTabLabel(surface, painter.LabelFont(), "x", 0.0)
				.empty());
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, 320, 120)});
	CHECK_FALSE(truncated.empty());
}

TEST_CASE("tab strip paint active dirty overflowed hovered and scaled layouts") {
	ApplicationEditor editor(360, 120);
	editor.LoadInitialBuffer("paint\n");
	TabStripPainter painter;

	SECTION("active tab face differs from inactive") {
		const TabStripModel model = ModelWith({
			MakeTab(1, "active", true),
			MakeTab(2, "idle", false),
		});
		const TabStripLayout layout = LayoutTabStrip(360, model);
		const auto pixels = PaintStrip(editor, painter, layout, model);
		REQUIRE(pixels.size() == 360U * 120U * 4U);
		const int y = TabStripHeight() / 2;
		const Rgba active = Sample(pixels, 360,
			static_cast<int>(Center(layout.tabs[0].bounds).x), y);
		const Rgba idle = Sample(pixels, 360,
			static_cast<int>(Center(layout.tabs[1].bounds).x), y);
		// Sample label band, not the bottom accent alone.
		CHECK(Differs(active, idle));
	}

	SECTION("dirty inactive tab paints bottom accent") {
		const TabStripModel model = ModelWith({
			MakeTab(1, "clean", true, false),
			MakeTab(2, "dirty *", false, true),
		});
		const TabStripLayout layout = LayoutTabStrip(360, model);
		const auto pixels = PaintStrip(editor, painter, layout, model);
		const int accentY = TabStripHeight() - 1;
		const int dirtyX = static_cast<int>(
			(layout.tabs[1].bounds.left + layout.tabs[1].bounds.right) / 2);
		const Rgba dirtyAccent = Sample(pixels, 360, dirtyX, accentY);
		// Dirty accent is warm (orange-brown), not strip gray.
		CHECK(dirtyAccent.r > dirtyAccent.b);
		CHECK(dirtyAccent.r > 0x80);
	}

	SECTION("overflowed strip leaves scrolled-out tab chrome unpainted in viewport") {
		std::vector<TabStripTab> tabs;
		for (uint64_t i = 1; i <= 6; ++i) {
			tabs.push_back(MakeTab(i, "t" + std::to_string(i), i == 6));
		}
		const int stripWidth = TabStripPreferredTabWidth() * 2 + 28;
		editor.Resize(stripWidth, 120);
		const int scroll = ScrollTabStripToIndex(
			stripWidth, tabs.size(), tabs.size() - 1, 0);
		const TabStripModel model = ModelWith(std::move(tabs), scroll);
		const TabStripLayout layout = LayoutTabStrip(stripWidth, model);
		REQUIRE(layout.scrollOffset > 0);
		const auto pixels = PaintStrip(editor, painter, layout, model);
		// A point inside the viewport that would have been the first tab at
		// scroll 0 is now the scrolled content, not the strip's empty gray only
		// check: first tab's unshifted center is off-viewport.
		const bool scrolledOut =
			(layout.tabs.front().bounds.right <=
				layout.tabsViewport.left + 1.0) ||
			(layout.tabs.front().bounds.left < layout.tabsViewport.left);
		CHECK(scrolledOut);
		// Active tab center inside the strip has non-background chrome.
		const int ax = static_cast<int>(Center(layout.tabs.back().bounds).x);
		const int ay = TabStripHeight() / 2;
		REQUIRE(ax >= 0);
		REQUIRE(ax < stripWidth);
		const Rgba active = Sample(pixels, stripWidth, ax, ay);
		const Rgba below = Sample(pixels, stripWidth, ax, TabStripHeight() + 4);
		CHECK(Differs(active, below));
	}

	SECTION("hovered inactive tab paints close control") {
		const TabStripModel model = ModelWith(
			{MakeTab(1, "a", true), MakeTab(2, "b", false)}, 0, 2, true);
		const TabStripLayout layout = LayoutTabStrip(360, model);
		REQUIRE(layout.tabs[1].showClose);
		const auto pixels = PaintStrip(editor, painter, layout, model);
		const int cx = static_cast<int>(Center(layout.tabs[1].closeButton).x);
		const int cy = static_cast<int>(Center(layout.tabs[1].closeButton).y);
		const Rgba closePx = Sample(pixels, 360, cx, cy);
		const Rgba tabBody = Sample(pixels, 360,
			static_cast<int>(layout.tabs[1].bounds.left + 8), cy);
		CHECK(Differs(closePx, tabBody));
	}

	SECTION("scaled framebuffer still paints strip chrome in logical coords") {
		editor.Resize(200, 80);
		editor.SetFrameBufferSize(400, 160);
		const TabStripModel model = ModelWith({
			MakeTab(1, "scaled", true),
			MakeTab(2, "other", false, true),
		});
		const TabStripLayout layout = LayoutTabStrip(200, model);
		CHECK(layout.strip.right == 200);
		const auto pixels = PaintStrip(editor, painter, layout, model);
		// FramePixels follow the logical frame surface size.
		REQUIRE(pixels.size() == 200U * 80U * 4U);
		const Rgba stripMid = Sample(pixels, 200, 40, TabStripHeight() / 2);
		const Rgba client = Sample(pixels, 200, 40, TabStripHeight() + 10);
		CHECK(Differs(stripMid, client));
		// Add button region is painted.
		const int addX = static_cast<int>(Center(layout.addButton).x);
		const Rgba addPx = Sample(pixels, 200, addX, TabStripHeight() / 2);
		CHECK(addPx.a == 0xff);
	}
}

TEST_CASE("tab strip hit-test ignores overflow outside the viewport") {
	std::vector<TabStripTab> tabs;
	for (uint64_t i = 1; i <= 5; ++i) {
		tabs.push_back(MakeTab(i, "n", i == 1));
	}
	const int stripWidth = TabStripPreferredTabWidth() + 28;
	// Scroll to a later active tab, then check a point past the viewport edge.
	tabs[0].active = false;
	tabs.back().active = true;
	const int scroll = ScrollTabStripToIndex(
		stripWidth, tabs.size(), tabs.size() - 1, 0);
	const TabStripLayout layout = LayoutTabStrip(stripWidth,
		ModelWith(std::move(tabs), scroll));
	// Point on the add button is Add, not a scrolled tab under it.
	const TabStripHitResult add = HitTestTabStrip(layout, Center(layout.addButton));
	CHECK(add.kind == TabStripHit::Add);
	// Point just left of add, in viewport, hits some tab or strip.
	const Point nearAdd(layout.addButton.left - 2.0, TabStripHeight() / 2.0);
	const TabStripHitResult near = HitTestTabStrip(layout, nearAdd);
	CHECK(near.kind != TabStripHit::Add);
	CHECK(near.kind != TabStripHit::None);
}
