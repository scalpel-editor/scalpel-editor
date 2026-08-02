#include "catch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ApplicationAction.h"
#include "ApplicationEditor.h"
#include "ApplicationInput.h"
#include "ContextMenu.h"
#include "DocumentWorkspace.h"

using Scalpel::ApplicationAction;
using Scalpel::ApplicationEditor;
using Scalpel::CloseContextMenu;
using Scalpel::ContextMenuHit;
using Scalpel::ContextMenuHitResult;
using Scalpel::ContextMenuItemLayout;
using Scalpel::ContextMenuKeyboardResult;
using Scalpel::ContextMenuLayout;
using Scalpel::ContextMenuModel;
using Scalpel::ContextMenuPainter;
using Scalpel::ContextMenuPointerResult;
using Scalpel::ContextMenuPreferredHeight;
using Scalpel::ContextMenuPreferredWidth;
using Scalpel::ContextMenuPressKind;
using Scalpel::DocumentWorkspace;
using Scalpel::HandleContextMenuKeyboard;
using Scalpel::HandleContextMenuPointer;
using Scalpel::HitTestContextMenu;
using Scalpel::KeyboardInput;
using Scalpel::LayoutContextMenu;
using Scalpel::OpenContextMenu;
using Scalpel::PointerAction;
using Scalpel::PointerInput;
using Scalpel::UpdateContextMenuActionState;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;
using Scintilla::KeyMod;
using Scintilla::Keys;

namespace {

bool NonEmpty(const PRectangle &rc) {
	return rc.right > rc.left && rc.bottom > rc.top;
}

Point Center(const PRectangle &rc) {
	return Point((rc.left + rc.right) / 2.0, (rc.top + rc.bottom) / 2.0);
}

ContextMenuModel OpenMenu() {
	ContextMenuModel model;
	OpenContextMenu(model);
	return model;
}

const ContextMenuItemLayout *FindItem(const ContextMenuLayout &layout,
	ApplicationAction action) {
	for (const auto &item : layout.items) {
		if (item.action == action) {
			return &item;
		}
	}
	return nullptr;
}

PointerInput MakePointer(PointerAction action, double x, double y,
	int button = -1) {
	PointerInput input;
	input.action = action;
	input.x = x;
	input.y = y;
	input.button = button;
	return input;
}

KeyboardInput MakeKey(Keys key, KeyMod modifiers = KeyMod::Norm,
	bool pressed = true) {
	KeyboardInput input;
	input.key = key;
	input.modifiers = modifiers;
	input.pressed = pressed;
	return input;
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

std::vector<uint8_t> PaintMenu(ApplicationEditor &editor,
	ContextMenuPainter &painter, const ContextMenuLayout &layout,
	const ContextMenuModel &model) {
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

TEST_CASE("Context menu rows order labels separators and preferred size") {
	const ContextMenuModel model = OpenMenu();
	const ContextMenuLayout layout = LayoutContextMenu(model);

	CHECK(layout.requestedWidth == ContextMenuPreferredWidth());
	CHECK(layout.requestedHeight == ContextMenuPreferredHeight());
	CHECK(layout.requestedWidth == 240);
	CHECK(NonEmpty(layout.panel));
	CHECK(layout.panel.left == 0);
	CHECK(layout.panel.top == 0);
	CHECK(static_cast<int>(layout.panel.Width()) == layout.requestedWidth);
	CHECK(static_cast<int>(layout.panel.Height()) == layout.requestedHeight);
	REQUIRE(layout.items.size() == 6);

	const ApplicationAction expected[] = {
		ApplicationAction::Undo,
		ApplicationAction::Redo,
		ApplicationAction::Cut,
		ApplicationAction::Copy,
		ApplicationAction::Paste,
		ApplicationAction::SelectAll,
	};
	for (std::size_t i = 0; i < 6; ++i) {
		CHECK(layout.items[i].action == expected[i]);
	}

	const auto *undo = FindItem(layout, ApplicationAction::Undo);
	const auto *redo = FindItem(layout, ApplicationAction::Redo);
	const auto *cut = FindItem(layout, ApplicationAction::Cut);
	const auto *copy = FindItem(layout, ApplicationAction::Copy);
	const auto *paste = FindItem(layout, ApplicationAction::Paste);
	const auto *selectAll = FindItem(layout, ApplicationAction::SelectAll);
	REQUIRE(undo);
	REQUIRE(redo);
	REQUIRE(cut);
	REQUIRE(copy);
	REQUIRE(paste);
	REQUIRE(selectAll);

	// Groups: Undo/Redo | Cut/Copy/Paste | Select All (from ApplicationAction).
	CHECK_FALSE(undo->separatorBefore);
	CHECK_FALSE(redo->separatorBefore);
	CHECK(cut->separatorBefore);
	CHECK_FALSE(copy->separatorBefore);
	CHECK_FALSE(paste->separatorBefore);
	CHECK(selectAll->separatorBefore);
	CHECK(NonEmpty(cut->separator));
	CHECK(NonEmpty(selectAll->separator));

	CHECK(undo->labelText == "Undo");
	CHECK(undo->shortcutText == "Ctrl+Z");
	CHECK(redo->labelText == "Redo");
	CHECK(redo->shortcutText == "Ctrl+Y");
	CHECK(cut->labelText == "Cut");
	CHECK(cut->shortcutText == "Ctrl+X");
	CHECK(copy->labelText == "Copy");
	CHECK(copy->shortcutText == "Ctrl+C");
	CHECK(paste->labelText == "Paste");
	CHECK(paste->shortcutText == "Ctrl+V");
	CHECK(selectAll->labelText == "Select All");
	CHECK(selectAll->shortcutText == "Ctrl+A");

	CHECK(NonEmpty(undo->label));
	CHECK(NonEmpty(undo->shortcut));
	CHECK(undo->label.right <= undo->shortcut.left + 0.5);
	CHECK(undo->row.bottom <= redo->row.top + 0.5);
	CHECK(redo->row.bottom <= cut->row.top + 0.5);
	CHECK(paste->row.bottom <= selectAll->row.top + 0.5);
}

TEST_CASE("Context menu closed layout keeps requested size without rows") {
	ContextMenuModel model;
	const ContextMenuLayout layout = LayoutContextMenu(model);
	CHECK_FALSE(model.open);
	CHECK_FALSE(NonEmpty(layout.panel));
	CHECK(layout.items.empty());
	CHECK(layout.requestedWidth == ContextMenuPreferredWidth());
	CHECK(layout.requestedHeight == ContextMenuPreferredHeight());
}

TEST_CASE("Context menu enablement flags drive layout and model") {
	ContextMenuModel model = OpenMenu();
	model.undoEnabled = false;
	model.cutEnabled = false;
	model.pasteEnabled = true;
	const ContextMenuLayout layout = LayoutContextMenu(model);

	const auto *undo = FindItem(layout, ApplicationAction::Undo);
	const auto *cut = FindItem(layout, ApplicationAction::Cut);
	const auto *paste = FindItem(layout, ApplicationAction::Paste);
	const auto *selectAll = FindItem(layout, ApplicationAction::SelectAll);
	REQUIRE(undo);
	REQUIRE(cut);
	REQUIRE(paste);
	REQUIRE(selectAll);
	CHECK_FALSE(undo->enabled);
	CHECK_FALSE(cut->enabled);
	CHECK(paste->enabled);
	CHECK(selectAll->enabled);
	CHECK_FALSE(model.IsEnabled(ApplicationAction::Undo));
	CHECK(model.IsEnabled(ApplicationAction::Paste));
	CHECK_FALSE(model.IsEnabled(ApplicationAction::Find));
}

TEST_CASE("Context menu action state follows editor enablement") {
	ApplicationEditor editor(400, 300);
	editor.LoadInitialBuffer("enablement\n");
	DocumentWorkspace workspace(editor);
	ContextMenuModel model = OpenMenu();

	// Fresh document: Undo off, Select All on; paste follows clipboard offer.
	editor.SetClipboardPasteAvailable(false);
	CHECK(UpdateContextMenuActionState(model, editor));
	CHECK_FALSE(model.undoEnabled);
	CHECK(model.selectAllEnabled);
	CHECK_FALSE(model.pasteEnabled);

	editor.SetClipboardPasteAvailable(true);
	CHECK(UpdateContextMenuActionState(model, editor));
	CHECK(model.pasteEnabled);

	// Second refresh with no change returns false.
	CHECK_FALSE(UpdateContextMenuActionState(model, editor));

	// Focus moves off a row that becomes disabled.
	model.focusedItem = ApplicationAction::Paste;
	editor.SetClipboardPasteAvailable(false);
	CHECK(UpdateContextMenuActionState(model, editor));
	REQUIRE(model.focusedItem.has_value());
	CHECK(*model.focusedItem != ApplicationAction::Paste);
	CHECK(model.IsEnabled(*model.focusedItem));
}

TEST_CASE("Context menu exact bounds are popup-local at the origin") {
	const ContextMenuLayout layout = LayoutContextMenu(OpenMenu());
	CHECK(layout.panel == PRectangle::FromInts(0, 0, layout.requestedWidth,
		layout.requestedHeight));
	for (const auto &item : layout.items) {
		CHECK(item.row.left >= layout.panel.left);
		CHECK(item.row.right <= layout.panel.right);
		CHECK(item.row.top >= layout.panel.top);
		CHECK(item.row.bottom <= layout.panel.bottom);
	}
}

TEST_CASE("Context menu narrow size clips width and drops overflowing rows") {
	const ContextMenuModel model = OpenMenu();
	const int preferredW = ContextMenuPreferredWidth();
	const int preferredH = ContextMenuPreferredHeight();

	const ContextMenuLayout narrow = LayoutContextMenu(model, 80, 0);
	CHECK(narrow.requestedWidth == preferredW);
	CHECK(narrow.requestedHeight == preferredH);
	REQUIRE(NonEmpty(narrow.panel));
	CHECK(narrow.panel.Width() == 80);
	CHECK(static_cast<int>(narrow.panel.Height()) == preferredH);
	// All six rows still fit; only width is constrained (label column shrinks).
	REQUIRE(narrow.items.size() == 6);
	const auto *undo = FindItem(narrow, ApplicationAction::Undo);
	REQUIRE(undo);
	CHECK(undo->label.Width() <
		FindItem(LayoutContextMenu(model), ApplicationAction::Undo)->label.Width());

	// Height too short for every row: keep requested sizes, drop overflow.
	const ContextMenuLayout shortPanel = LayoutContextMenu(model, 0, 40);
	CHECK(shortPanel.requestedHeight == preferredH);
	REQUIRE(NonEmpty(shortPanel.panel));
	CHECK(shortPanel.panel.bottom <= 40);
	CHECK(shortPanel.items.size() < 6);
	CHECK_FALSE(shortPanel.items.empty());

	const ContextMenuLayout empty = LayoutContextMenu(model, 0, 0);
	// max 0 means preferred, not empty.
	CHECK(static_cast<int>(empty.panel.Width()) == preferredW);
	CHECK(empty.items.size() == 6);
}

TEST_CASE("Context menu hit-test items panel padding and outside") {
	const ContextMenuLayout layout = LayoutContextMenu(OpenMenu());
	const auto *cut = FindItem(layout, ApplicationAction::Cut);
	REQUIRE(cut);

	const ContextMenuHitResult onItem =
		HitTestContextMenu(layout, Center(cut->row));
	CHECK(onItem.kind == ContextMenuHit::Item);
	CHECK(onItem.action == ApplicationAction::Cut);

	// Separator band is panel chrome, not an item.
	if (NonEmpty(cut->separator)) {
		const ContextMenuHitResult onSep = HitTestContextMenu(layout,
			Point((cut->separator.left + cut->separator.right) / 2.0,
				cut->separator.top));
		CHECK(onSep.kind == ContextMenuHit::Panel);
	}

	// Top padding inside the panel.
	const ContextMenuHitResult onPad = HitTestContextMenu(layout,
		Point(layout.panel.left + 2, layout.panel.top + 1));
	CHECK(onPad.kind == ContextMenuHit::Panel);

	const ContextMenuHitResult outside = HitTestContextMenu(layout,
		Point(layout.panel.right + 5, layout.panel.bottom + 5));
	CHECK(outside.kind == ContextMenuHit::None);

	// Half-open right/bottom edges are outside.
	CHECK(HitTestContextMenu(layout,
		Point(layout.panel.right, layout.panel.top + 2)).kind ==
		ContextMenuHit::None);
	CHECK(HitTestContextMenu(layout,
		Point(layout.panel.left + 2, layout.panel.bottom)).kind ==
		ContextMenuHit::None);
}

TEST_CASE("Context menu pointer hover press release activation") {
	ContextMenuModel model = OpenMenu();
	const ContextMenuLayout layout = LayoutContextMenu(model);
	const auto *copy = FindItem(layout, ApplicationAction::Copy);
	const auto *paste = FindItem(layout, ApplicationAction::Paste);
	REQUIRE(copy);
	REQUIRE(paste);
	const Point copyPt = Center(copy->row);
	const Point pastePt = Center(paste->row);

	SECTION("move over an item sets hoveredItem") {
		const ContextMenuPointerResult hover = HandleContextMenuPointer(model,
			layout, MakePointer(PointerAction::Move, copyPt.x, copyPt.y));
		CHECK(hover.consumed);
		CHECK(hover.dirty);
		REQUIRE(model.hoveredItem.has_value());
		CHECK(*model.hoveredItem == ApplicationAction::Copy);
	}

	SECTION("matching press and release activates and closes") {
		const ContextMenuPointerResult press = HandleContextMenuPointer(model,
			layout, MakePointer(PointerAction::Press, copyPt.x, copyPt.y, 0));
		CHECK(press.consumed);
		CHECK_FALSE(press.activated.has_value());
		REQUIRE(model.pressOrigin.has_value());
		CHECK(model.pressOrigin->kind == ContextMenuPressKind::Item);
		CHECK(model.pressOrigin->action == ApplicationAction::Copy);

		const ContextMenuPointerResult release = HandleContextMenuPointer(model,
			layout, MakePointer(PointerAction::Release, copyPt.x, copyPt.y, 0));
		CHECK(release.consumed);
		CHECK(release.dirty);
		REQUIRE(release.activated.has_value());
		CHECK(*release.activated == ApplicationAction::Copy);
		CHECK_FALSE(model.open);
		CHECK_FALSE(model.pressOrigin.has_value());
	}

	SECTION("press on one item and release on another does not activate") {
		(void)HandleContextMenuPointer(model, layout,
			MakePointer(PointerAction::Press, copyPt.x, copyPt.y, 0));
		const ContextMenuPointerResult release = HandleContextMenuPointer(model,
			layout,
			MakePointer(PointerAction::Release, pastePt.x, pastePt.y, 0));
		CHECK(release.consumed);
		CHECK_FALSE(release.activated.has_value());
		CHECK(model.open);
		CHECK_FALSE(model.pressOrigin.has_value());
	}
}

TEST_CASE("Context menu pointer rejects disabled items") {
	ContextMenuModel model = OpenMenu();
	model.undoEnabled = false;
	const ContextMenuLayout layout = LayoutContextMenu(model);
	const auto *undo = FindItem(layout, ApplicationAction::Undo);
	REQUIRE(undo);
	CHECK_FALSE(undo->enabled);
	const Point undoPt = Center(undo->row);

	(void)HandleContextMenuPointer(model, layout,
		MakePointer(PointerAction::Press, undoPt.x, undoPt.y, 0));
	const ContextMenuPointerResult release = HandleContextMenuPointer(model,
		layout, MakePointer(PointerAction::Release, undoPt.x, undoPt.y, 0));
	CHECK(release.consumed);
	CHECK_FALSE(release.activated.has_value());
	CHECK(model.open);
}

TEST_CASE("Context menu pointer outside dismissal without click-through") {
	ContextMenuModel model = OpenMenu();
	const ContextMenuLayout layout = LayoutContextMenu(model);

	SECTION("press outside panel closes and is consumed") {
		const ContextMenuPointerResult outside = HandleContextMenuPointer(model,
			layout, MakePointer(PointerAction::Press, 400, 400, 0));
		CHECK(outside.consumed);
		CHECK(outside.dirty);
		CHECK_FALSE(model.open);
		CHECK_FALSE(outside.activated.has_value());
		REQUIRE(model.pressOrigin.has_value());
		CHECK(model.pressOrigin->kind == ContextMenuPressKind::Dismissal);

		const ContextMenuLayout closed = LayoutContextMenu(model);
		const ContextMenuPointerResult release = HandleContextMenuPointer(model,
			closed, MakePointer(PointerAction::Release, 400, 400, 0));
		CHECK(release.consumed);
		CHECK_FALSE(release.activated.has_value());
		CHECK_FALSE(model.pressOrigin.has_value());
		CHECK_FALSE(model.open);
	}

	SECTION("press on panel padding closes without activating") {
		const Point pad(layout.panel.left + 2, layout.panel.top + 1);
		const ContextMenuPointerResult padPress = HandleContextMenuPointer(
			model, layout, MakePointer(PointerAction::Press, pad.x, pad.y, 0));
		CHECK(padPress.consumed);
		CHECK_FALSE(model.open);
		CHECK_FALSE(padPress.activated.has_value());
	}

	SECTION("closed menu does not consume editor body presses") {
		CloseContextMenu(model);
		const ContextMenuLayout closed = LayoutContextMenu(model);
		const ContextMenuPointerResult body = HandleContextMenuPointer(model,
			closed, MakePointer(PointerAction::Press, 10, 10, 0));
		CHECK_FALSE(body.consumed);
		CHECK_FALSE(model.open);
	}
}

TEST_CASE("Context menu keyboard navigates wraps Home End Enter Escape") {
	ContextMenuModel model = OpenMenu();
	// Open focuses the first enabled item (Undo when all enabled).
	REQUIRE(model.focusedItem.has_value());
	CHECK(*model.focusedItem == ApplicationAction::Undo);

	SECTION("Down and Up wrap among enabled items") {
		const ContextMenuKeyboardResult down = HandleContextMenuKeyboard(model,
			MakeKey(Keys::Down));
		CHECK(down.consumed);
		CHECK(down.dirty);
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == ApplicationAction::Redo);

		// Jump to Select All then Down wraps to Undo.
		model.focusedItem = ApplicationAction::SelectAll;
		const ContextMenuKeyboardResult wrap = HandleContextMenuKeyboard(model,
			MakeKey(Keys::Down));
		CHECK(wrap.consumed);
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == ApplicationAction::Undo);

		const ContextMenuKeyboardResult up = HandleContextMenuKeyboard(model,
			MakeKey(Keys::Up));
		CHECK(up.consumed);
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == ApplicationAction::SelectAll);
	}

	SECTION("Down skips disabled items") {
		model.redoEnabled = false;
		model.cutEnabled = false;
		const ContextMenuKeyboardResult down = HandleContextMenuKeyboard(model,
			MakeKey(Keys::Down));
		CHECK(down.consumed);
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == ApplicationAction::Copy);
	}

	SECTION("Home and End jump to first and last enabled") {
		model.focusedItem = ApplicationAction::Paste;
		const ContextMenuKeyboardResult home = HandleContextMenuKeyboard(model,
			MakeKey(Keys::Home));
		CHECK(home.consumed);
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == ApplicationAction::Undo);

		const ContextMenuKeyboardResult end = HandleContextMenuKeyboard(model,
			MakeKey(Keys::End));
		CHECK(end.consumed);
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == ApplicationAction::SelectAll);

		model.undoEnabled = false;
		model.selectAllEnabled = false;
		(void)HandleContextMenuKeyboard(model, MakeKey(Keys::Home));
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == ApplicationAction::Redo);
		(void)HandleContextMenuKeyboard(model, MakeKey(Keys::End));
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == ApplicationAction::Paste);
	}

	SECTION("Enter activates focused enabled item and closes") {
		model.focusedItem = ApplicationAction::SelectAll;
		const ContextMenuKeyboardResult enter = HandleContextMenuKeyboard(model,
			MakeKey(Keys::Return));
		CHECK(enter.consumed);
		CHECK(enter.dirty);
		REQUIRE(enter.activated.has_value());
		CHECK(*enter.activated == ApplicationAction::SelectAll);
		CHECK_FALSE(model.open);
	}

	SECTION("Enter on a disabled focus does not activate") {
		model.focusedItem = ApplicationAction::Undo;
		model.undoEnabled = false;
		const ContextMenuKeyboardResult enter = HandleContextMenuKeyboard(model,
			MakeKey(Keys::Return));
		CHECK(enter.consumed);
		CHECK_FALSE(enter.activated.has_value());
		CHECK(model.open);
	}

	SECTION("Escape closes without activating") {
		const ContextMenuKeyboardResult esc = HandleContextMenuKeyboard(model,
			MakeKey(Keys::Escape));
		CHECK(esc.consumed);
		CHECK(esc.dirty);
		CHECK_FALSE(esc.activated.has_value());
		CHECK_FALSE(model.open);
	}

	SECTION("releases and unrelated keys are consumed while open") {
		const ContextMenuKeyboardResult release = HandleContextMenuKeyboard(
			model, MakeKey(Keys::Down, KeyMod::Norm, false));
		CHECK(release.consumed);
		CHECK_FALSE(release.dirty);

		const ContextMenuKeyboardResult letter = HandleContextMenuKeyboard(model,
			MakeKey(static_cast<Keys>('A')));
		CHECK(letter.consumed);
		CHECK_FALSE(letter.activated.has_value());
		CHECK(model.open);
	}

	SECTION("closed menu ignores keyboard") {
		CloseContextMenu(model);
		const ContextMenuKeyboardResult key = HandleContextMenuKeyboard(model,
			MakeKey(Keys::Down));
		CHECK_FALSE(key.consumed);
		CHECK_FALSE(model.open);
	}
}

TEST_CASE("Context menu paint hovered focused disabled and panel fill") {
	ApplicationEditor editor(ContextMenuPreferredWidth(),
		ContextMenuPreferredHeight());
	editor.LoadInitialBuffer("paint\n");
	ContextMenuPainter painter;
	ContextMenuModel model = OpenMenu();
	model.undoEnabled = false;
	model.focusedItem = ApplicationAction::Copy;
	model.hoveredItem = ApplicationAction::Paste;
	const ContextMenuLayout layout = LayoutContextMenu(model);
	const std::vector<uint8_t> pixels = PaintMenu(editor, painter, layout, model);
	REQUIRE(pixels.size() ==
		static_cast<size_t>(editor.FrameWidth()) *
			static_cast<size_t>(editor.FrameHeight()) * 4U);

	const auto *undo = FindItem(layout, ApplicationAction::Undo);
	const auto *redo = FindItem(layout, ApplicationAction::Redo);
	const auto *copy = FindItem(layout, ApplicationAction::Copy);
	const auto *paste = FindItem(layout, ApplicationAction::Paste);
	REQUIRE(undo);
	REQUIRE(redo);
	REQUIRE(copy);
	REQUIRE(paste);

	// Sample idle panel chrome (Redo row has neither hover nor focus).
	const Point idlePt = Center(redo->row);
	const Rgba panel = Sample(pixels, editor.FrameWidth(),
		static_cast<int>(idlePt.x), static_cast<int>(idlePt.y));
	// Panel fill is near-white (0xfc); not pure black.
	CHECK(panel.r > 200);
	CHECK(panel.a == 255);

	const Point copyPt = Center(copy->row);
	const Point pastePt = Center(paste->row);
	const Rgba focused = Sample(pixels, editor.FrameWidth(),
		static_cast<int>(copyPt.x), static_cast<int>(copyPt.y));
	const Rgba hovered = Sample(pixels, editor.FrameWidth(),
		static_cast<int>(pastePt.x), static_cast<int>(pastePt.y));
	// Focus and hover fills differ from plain panel.
	CHECK(Differs(focused, panel));
	CHECK(Differs(hovered, panel));
	// Focused row is cooler (blue-tinted) than a neutral panel gray.
	CHECK(focused.b >= focused.r);

	// Focus fill must not cover the panel's right border.
	const Rgba edge = Sample(pixels, editor.FrameWidth(),
		static_cast<int>(layout.panel.right) - 1,
		static_cast<int>(copyPt.y));
	CHECK(edge.r == 0xa0);
	CHECK(edge.g == 0xa0);
	CHECK(edge.b == 0xa0);
	CHECK(edge.a == 0xff);

	// Disabled item ink is lighter than enabled item ink (label column).
	const int undoX = static_cast<int>(undo->label.left + 4);
	const int undoY = static_cast<int>(Center(undo->row).y);
	const int redoX = static_cast<int>(redo->label.left + 4);
	const int redoY = static_cast<int>(Center(redo->row).y);
	const Rgba disabled = Sample(pixels, editor.FrameWidth(), undoX, undoY);
	const Rgba enabled = Sample(pixels, editor.FrameWidth(), redoX, redoY);
	const int disabledLum =
		static_cast<int>(disabled.r) + disabled.g + disabled.b;
	const int enabledLum =
		static_cast<int>(enabled.r) + enabled.g + enabled.b;
	CHECK(disabledLum >= enabledLum);
}

TEST_CASE("Context menu open focuses first enabled item") {
	ContextMenuModel model;
	model.undoEnabled = false;
	model.redoEnabled = false;
	OpenContextMenu(model);
	CHECK(model.open);
	REQUIRE(model.focusedItem.has_value());
	CHECK(*model.focusedItem == ApplicationAction::Cut);
	CHECK_FALSE(model.hoveredItem.has_value());
	CHECK_FALSE(model.pressOrigin.has_value());

	CloseContextMenu(model);
	CHECK_FALSE(model.open);
	CHECK_FALSE(model.focusedItem.has_value());
}
