#include "catch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

#include "ApplicationAction.h"
#include "ApplicationEditor.h"
#include "ApplicationInput.h"
#include "ApplicationTextInput.h"
#include "DocumentWorkspace.h"
#include "MenuBar.h"
#include "RecentFiles.h"
#include "TabStrip.h"
#include "UnsavedChangesCard.h"
#include "UnsavedChangesPrompt.h"

using Scalpel::ApplicationAction;
using Scalpel::ApplicationActionCount;
using Scalpel::ApplicationActionInfo;
using Scalpel::ApplicationActionTable;
using Scalpel::ApplicationEditor;
using Scalpel::ApplicationMenu;
using Scalpel::ApplicationTextInputBatch;
using Scalpel::ApplicationTextInputPreedit;
using Scalpel::CloseMenuBar;
using Scalpel::DispatchApplicationAction;
using Scalpel::DocumentShellRequest;
using Scalpel::DocumentWorkspace;
using Scalpel::HandleMenuBarKeyboard;
using Scalpel::HandleMenuBarPointer;
using Scalpel::HitTestMenuBar;
using Scalpel::KeyboardInput;
using Scalpel::LayoutMenuBar;
using Scalpel::LayoutUnsavedChangesCard;
using Scalpel::MenuBarHeight;
using Scalpel::MenuBarHit;
using Scalpel::MenuBarHitResult;
using Scalpel::MenuBarItemId;
using Scalpel::MenuBarItemKind;
using Scalpel::MenuBarItemLayout;
using Scalpel::MenuBarKeyboardResult;
using Scalpel::MenuBarLayout;
using Scalpel::MenuBarModel;
using Scalpel::MenuBarPainter;
using Scalpel::MenuBarPointerResult;
using Scalpel::MenuBarPressKind;
using Scalpel::PointerAction;
using Scalpel::PointerInput;
using Scalpel::UnsavedChangesCardPainter;
using Scalpel::UpdateMenuBarActionState;
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

MenuBarModel ClosedBar() {
	return {};
}

MenuBarModel OpenMenu(ApplicationMenu menu) {
	MenuBarModel model;
	model.openMenu = menu;
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

std::vector<uint8_t> PaintMenu(ApplicationEditor &editor, MenuBarPainter &painter,
	const MenuBarLayout &layout, const MenuBarModel &model) {
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

std::size_t CountMenuItems(ApplicationMenu menu) {
	std::size_t n = 0;
	const ApplicationActionInfo *table = ApplicationActionTable();
	for (std::size_t i = 0; i < ApplicationActionCount(); ++i) {
		if (table[i].menu == menu) {
			++n;
		}
	}
	return n;
}

const MenuBarItemLayout *FindItem(const MenuBarLayout &layout,
	ApplicationAction action) {
	for (const auto &item : layout.items) {
		if (item.action == action) {
			return &item;
		}
	}
	return nullptr;
}

const MenuBarItemLayout *FindMenuItem(const MenuBarLayout &layout,
	MenuBarItemKind kind, std::size_t recentIndex = 0) {
	for (const auto &item : layout.items) {
		if (item.item.kind == kind &&
			(kind != MenuBarItemKind::RecentFile ||
				item.item.recentIndex == recentIndex)) {
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

MenuBarLayout Layout(const MenuBarModel &model, int width = 400,
	int height = 300) {
	return LayoutMenuBar(width, height, model);
}

KeyboardInput MakeKey(Keys key, KeyMod modifiers = KeyMod::Norm,
	bool pressed = true) {
	KeyboardInput input;
	input.key = key;
	input.modifiers = modifiers;
	input.pressed = pressed;
	return input;
}

KeyboardInput MakeLetter(char upper, KeyMod modifiers = KeyMod::Norm,
	bool pressed = true) {
	return MakeKey(static_cast<Keys>(upper), modifiers, pressed);
}

}

TEST_CASE("menu bar keyboard opens with F10 Menu and Alt accelerators") {
	MenuBarModel model;

	SECTION("F10 opens File and focuses the first enabled item") {
		const MenuBarKeyboardResult open = HandleMenuBarKeyboard(model,
			MakeKey(Keys::Menu));
		CHECK(open.consumed);
		CHECK(open.frameDirty);
		CHECK(open.barDirty);
		CHECK_FALSE(open.activated.has_value());
		REQUIRE(model.openMenu.has_value());
		CHECK(*model.openMenu == ApplicationMenu::File);
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == ApplicationAction::NewTab);
		CHECK_FALSE(model.pressOrigin.has_value());
	}

	SECTION("Alt+F opens File") {
		const MenuBarKeyboardResult open = HandleMenuBarKeyboard(model,
			MakeLetter('F', KeyMod::Alt));
		CHECK(open.consumed);
		REQUIRE(model.openMenu.has_value());
		CHECK(*model.openMenu == ApplicationMenu::File);
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == ApplicationAction::NewTab);
	}

	SECTION("Alt+E opens Edit and focuses the first enabled item") {
		model.undoEnabled = false;
		model.redoEnabled = true;
		const MenuBarKeyboardResult open = HandleMenuBarKeyboard(model,
			MakeLetter('E', KeyMod::Alt));
		CHECK(open.consumed);
		REQUIRE(model.openMenu.has_value());
		CHECK(*model.openMenu == ApplicationMenu::Edit);
		REQUIRE(model.focusedItem.has_value());
		// Undo is disabled; focus lands on the first enabled Edit item.
		CHECK(*model.focusedItem == ApplicationAction::Redo);
	}

	SECTION("unrelated keys while closed are not consumed") {
		const MenuBarKeyboardResult letter = HandleMenuBarKeyboard(model,
			MakeLetter('A'));
		CHECK_FALSE(letter.consumed);
		CHECK_FALSE(model.openMenu.has_value());

		const MenuBarKeyboardResult shortcut = HandleMenuBarKeyboard(model,
			MakeLetter('S', KeyMod::Ctrl));
		CHECK_FALSE(shortcut.consumed);
	}
}

TEST_CASE("menu bar keyboard navigates items and switches menus") {
	MenuBarModel model;
	model.openMenu = ApplicationMenu::File;
	model.focusedItem = ApplicationAction::NewTab;

	SECTION("Down and Up move among enabled items with wrapping") {
		model.hoveredItem = ApplicationAction::SaveAs;
		model.pressOrigin = Scalpel::MenuBarPressOrigin{
			Scalpel::MenuBarPressKind::Item,
			ApplicationMenu::File,
			ApplicationAction::SaveAs,
		};
		MenuBarKeyboardResult down = HandleMenuBarKeyboard(model,
			MakeKey(Keys::Down));
		CHECK(down.consumed);
		CHECK(down.frameDirty);
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == ApplicationAction::Open);
		CHECK_FALSE(model.hoveredItem.has_value());
		CHECK_FALSE(model.pressOrigin.has_value());

		down = HandleMenuBarKeyboard(model, MakeKey(Keys::Down));
		CHECK(*model.focusedItem == ApplicationAction::Save);

		// Walk to the last File item then wrap.
		model.focusedItem = ApplicationAction::Quit;
		const MenuBarKeyboardResult wrapDown = HandleMenuBarKeyboard(model,
			MakeKey(Keys::Down));
		CHECK(wrapDown.frameDirty);
		CHECK(*model.focusedItem == ApplicationAction::NewTab);

		const MenuBarKeyboardResult wrapUp = HandleMenuBarKeyboard(model,
			MakeKey(Keys::Up));
		CHECK(wrapUp.consumed);
		CHECK(*model.focusedItem == ApplicationAction::Quit);
	}

	SECTION("keyboard reopen clears stale pointer state and repaints") {
		model.hoveredItem = ApplicationAction::Open;
		model.pressOrigin = Scalpel::MenuBarPressOrigin{
			Scalpel::MenuBarPressKind::Item,
			ApplicationMenu::File,
			ApplicationAction::Open,
		};
		const MenuBarKeyboardResult reopen = HandleMenuBarKeyboard(model,
			MakeKey(Keys::Menu));
		CHECK(reopen.consumed);
		CHECK(reopen.frameDirty);
		CHECK(*model.focusedItem == ApplicationAction::NewTab);
		CHECK_FALSE(model.hoveredItem.has_value());
		CHECK_FALSE(model.pressOrigin.has_value());
	}

	SECTION("Down skips disabled items") {
		model.openMenu = ApplicationMenu::Edit;
		model.focusedItem = ApplicationAction::Undo;
		model.cutEnabled = false;
		model.copyEnabled = false;
		// Undo -> Redo -> (skip Cut, Copy) -> Paste
		(void)HandleMenuBarKeyboard(model, MakeKey(Keys::Down));
		CHECK(*model.focusedItem == ApplicationAction::Redo);
		(void)HandleMenuBarKeyboard(model, MakeKey(Keys::Down));
		CHECK(*model.focusedItem == ApplicationAction::Paste);
	}

	SECTION("Left and Right switch menus and refocus the first enabled item") {
		const MenuBarKeyboardResult toEdit = HandleMenuBarKeyboard(model,
			MakeKey(Keys::Right));
		CHECK(toEdit.consumed);
		CHECK(toEdit.frameDirty);
		REQUIRE(model.openMenu.has_value());
		CHECK(*model.openMenu == ApplicationMenu::Edit);
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == ApplicationAction::Undo);

		const MenuBarKeyboardResult toFile = HandleMenuBarKeyboard(model,
			MakeKey(Keys::Left));
		CHECK(toFile.consumed);
		CHECK(*model.openMenu == ApplicationMenu::File);
		CHECK(*model.focusedItem == ApplicationAction::NewTab);
	}
}

TEST_CASE("menu bar keyboard activates Escape and consumes while open") {
	MenuBarModel model;
	model.openMenu = ApplicationMenu::File;
	model.focusedItem = ApplicationAction::Save;

	SECTION("Enter activates the focused enabled item and closes") {
		const MenuBarKeyboardResult activate = HandleMenuBarKeyboard(model,
			MakeKey(Keys::Return));
		CHECK(activate.consumed);
		CHECK(activate.frameDirty);
		REQUIRE(activate.activated.has_value());
		CHECK(*activate.activated == ApplicationAction::Save);
		CHECK_FALSE(model.openMenu.has_value());
		CHECK_FALSE(model.focusedItem.has_value());
	}

	SECTION("Enter on a disabled focus does not activate") {
		model.openMenu = ApplicationMenu::Edit;
		model.focusedItem = ApplicationAction::Cut;
		model.cutEnabled = false;
		const MenuBarKeyboardResult refused = HandleMenuBarKeyboard(model,
			MakeKey(Keys::Return));
		CHECK(refused.consumed);
		CHECK_FALSE(refused.activated.has_value());
		REQUIRE(model.openMenu.has_value());
		CHECK(*model.focusedItem == ApplicationAction::Cut);
	}

	SECTION("Escape closes without activating") {
		const MenuBarKeyboardResult closed = HandleMenuBarKeyboard(model,
			MakeKey(Keys::Escape));
		CHECK(closed.consumed);
		CHECK(closed.frameDirty);
		CHECK_FALSE(closed.activated.has_value());
		CHECK_FALSE(model.openMenu.has_value());
	}

	SECTION("key releases and unrelated keys are consumed while open") {
		const MenuBarKeyboardResult release = HandleMenuBarKeyboard(model,
			MakeKey(Keys::Down, KeyMod::Norm, false));
		CHECK(release.consumed);
		CHECK(*model.focusedItem == ApplicationAction::Save);

		const MenuBarKeyboardResult letter = HandleMenuBarKeyboard(model,
			MakeLetter('X'));
		CHECK(letter.consumed);
		CHECK_FALSE(letter.activated.has_value());
		REQUIRE(model.openMenu.has_value());
		CHECK(*model.focusedItem == ApplicationAction::Save);

		const MenuBarKeyboardResult shortcut = HandleMenuBarKeyboard(model,
			MakeLetter('S', KeyMod::Ctrl));
		CHECK(shortcut.consumed);
		CHECK_FALSE(shortcut.activated.has_value());
	}
}

TEST_CASE("menu bar pointer opens toggles and switches headings") {
	MenuBarModel model;
	const int width = 400;
	const int height = 300;

	SECTION("left press on File opens the File menu") {
		const MenuBarLayout closed = Layout(model, width, height);
		const Point file = Center(closed.headings[0].bounds);
		const MenuBarPointerResult open = HandleMenuBarPointer(model, closed,
			MakePointer(PointerAction::Press, file.x, file.y, 0), false);
		CHECK(open.consumed);
		CHECK(open.frameDirty);
		CHECK_FALSE(open.activated.has_value());
		REQUIRE(model.openMenu.has_value());
		CHECK(*model.openMenu == ApplicationMenu::File);
		CHECK(model.pressOrigin.has_value());
		CHECK(model.pressOrigin->kind == MenuBarPressKind::Heading);
	}

	SECTION("pressing the open heading again closes it") {
		model.openMenu = ApplicationMenu::File;
		const MenuBarLayout openLayout = Layout(model, width, height);
		const Point file = Center(openLayout.headings[0].bounds);
		const MenuBarPointerResult closed = HandleMenuBarPointer(model,
			openLayout, MakePointer(PointerAction::Press, file.x, file.y, 0),
			false);
		CHECK(closed.consumed);
		CHECK(closed.frameDirty);
		CHECK_FALSE(model.openMenu.has_value());
		CHECK_FALSE(model.pressOrigin.has_value());
	}

	SECTION("moving across headings while open switches menus") {
		model.openMenu = ApplicationMenu::File;
		const MenuBarLayout fileLayout = Layout(model, width, height);
		const Point edit = Center(fileLayout.headings[1].bounds);
		const MenuBarPointerResult switched = HandleMenuBarPointer(model,
			fileLayout, MakePointer(PointerAction::Move, edit.x, edit.y),
			false);
		CHECK(switched.consumed);
		CHECK(switched.frameDirty);
		REQUIRE(model.openMenu.has_value());
		CHECK(*model.openMenu == ApplicationMenu::Edit);
		CHECK(model.hoveredHeading.has_value());
		CHECK(*model.hoveredHeading == ApplicationMenu::Edit);
		CHECK_FALSE(model.hoveredItem.has_value());
	}

	SECTION("press on Edit while File is open switches without closing") {
		model.openMenu = ApplicationMenu::File;
		const MenuBarLayout fileLayout = Layout(model, width, height);
		const Point edit = Center(fileLayout.headings[1].bounds);
		const MenuBarPointerResult switched = HandleMenuBarPointer(model,
			fileLayout, MakePointer(PointerAction::Press, edit.x, edit.y, 0),
			false);
		CHECK(switched.consumed);
		CHECK(switched.frameDirty);
		REQUIRE(model.openMenu.has_value());
		CHECK(*model.openMenu == ApplicationMenu::Edit);
	}
}

TEST_CASE("menu bar pointer item hover and press release activation") {
	MenuBarModel model;
	model.openMenu = ApplicationMenu::File;
	const MenuBarLayout layout = Layout(model);
	const auto *save = FindItem(layout, ApplicationAction::Save);
	const auto *open = FindItem(layout, ApplicationAction::Open);
	REQUIRE(save);
	REQUIRE(open);
	const Point savePt = Center(save->row);
	const Point openPt = Center(open->row);

	SECTION("move over an item sets hoveredItem") {
		const MenuBarPointerResult hover = HandleMenuBarPointer(model, layout,
			MakePointer(PointerAction::Move, savePt.x, savePt.y), false);
		CHECK(hover.consumed);
		CHECK(hover.frameDirty);
		CHECK(hover.pointerOverMenu);
		REQUIRE(model.hoveredItem.has_value());
		CHECK(*model.hoveredItem == ApplicationAction::Save);
	}

	SECTION("matching press and release activates and closes") {
		const MenuBarPointerResult press = HandleMenuBarPointer(model, layout,
			MakePointer(PointerAction::Press, savePt.x, savePt.y, 0), false);
		CHECK(press.consumed);
		CHECK_FALSE(press.activated.has_value());
		REQUIRE(model.pressOrigin.has_value());
		CHECK(model.pressOrigin->kind == MenuBarPressKind::Item);
		CHECK(model.pressOrigin->action == ApplicationAction::Save);

		// Release layout still open File with Save.
		const MenuBarPointerResult release = HandleMenuBarPointer(model, layout,
			MakePointer(PointerAction::Release, savePt.x, savePt.y, 0), false);
		CHECK(release.consumed);
		CHECK(release.frameDirty);
		REQUIRE(release.activated.has_value());
		CHECK(*release.activated == ApplicationAction::Save);
		CHECK_FALSE(model.openMenu.has_value());
		CHECK_FALSE(model.pressOrigin.has_value());
	}

	SECTION("press on one item and release on another does not activate") {
		(void)HandleMenuBarPointer(model, layout,
			MakePointer(PointerAction::Press, savePt.x, savePt.y, 0), false);
		const MenuBarPointerResult release = HandleMenuBarPointer(model, layout,
			MakePointer(PointerAction::Release, openPt.x, openPt.y, 0), false);
		CHECK(release.consumed);
		CHECK_FALSE(release.activated.has_value());
		// Menu stays open after a mismatched release.
		REQUIRE(model.openMenu.has_value());
		CHECK(*model.openMenu == ApplicationMenu::File);
		CHECK_FALSE(model.pressOrigin.has_value());
	}
}

TEST_CASE("menu bar pointer rejects disabled items") {
	MenuBarModel model;
	model.openMenu = ApplicationMenu::Edit;
	model.undoEnabled = false;
	const MenuBarLayout layout = Layout(model);
	const auto *undo = FindItem(layout, ApplicationAction::Undo);
	REQUIRE(undo);
	CHECK_FALSE(undo->enabled);
	const Point undoPt = Center(undo->row);

	(void)HandleMenuBarPointer(model, layout,
		MakePointer(PointerAction::Press, undoPt.x, undoPt.y, 0), false);
	const MenuBarPointerResult release = HandleMenuBarPointer(model, layout,
		MakePointer(PointerAction::Release, undoPt.x, undoPt.y, 0), false);
	CHECK(release.consumed);
	CHECK_FALSE(release.activated.has_value());
	// Disabled rows do not close the menu.
	REQUIRE(model.openMenu.has_value());
	CHECK(*model.openMenu == ApplicationMenu::Edit);
}

TEST_CASE("menu bar pointer outside dismissal without click-through") {
	MenuBarModel model;
	model.openMenu = ApplicationMenu::File;
	const MenuBarLayout layout = Layout(model);

	SECTION("press outside bar and dropdown closes and is consumed") {
		const MenuBarPointerResult outside = HandleMenuBarPointer(model, layout,
			MakePointer(PointerAction::Press, 200, 250, 0), false);
		CHECK(outside.consumed);
		CHECK(outside.frameDirty);
		CHECK_FALSE(model.openMenu.has_value());
		CHECK_FALSE(outside.activated.has_value());
		REQUIRE(model.pressOrigin.has_value());
		CHECK(model.pressOrigin->kind == MenuBarPressKind::Dismissal);

		const MenuBarLayout closed = Layout(model);
		const MenuBarPointerResult release = HandleMenuBarPointer(model, closed,
			MakePointer(PointerAction::Release, 200, 250, 0), false);
		CHECK(release.consumed);
		CHECK_FALSE(release.activated.has_value());
		CHECK_FALSE(model.pressOrigin.has_value());
	}

	SECTION("press on empty bar chrome while open closes and is consumed") {
		const MenuBarPointerResult bar = HandleMenuBarPointer(model, layout,
			MakePointer(PointerAction::Press, 300, MenuBarHeight() / 2.0, 0),
			false);
		CHECK(bar.consumed);
		CHECK(bar.frameDirty);
		CHECK_FALSE(model.openMenu.has_value());
	}

	SECTION("press on dropdown padding closes without activating") {
		const Point pad(layout.dropdown.left + 2, layout.dropdown.top + 1);
		const MenuBarPointerResult padPress = HandleMenuBarPointer(model,
			layout, MakePointer(PointerAction::Press, pad.x, pad.y, 0), false);
		CHECK(padPress.consumed);
		CHECK_FALSE(model.openMenu.has_value());
		CHECK_FALSE(padPress.activated.has_value());
	}

	SECTION("closed menu does not consume presses in the editor body") {
		CloseMenuBar(model);
		const MenuBarLayout closed = Layout(model);
		const MenuBarPointerResult body = HandleMenuBarPointer(model, closed,
			MakePointer(PointerAction::Press, 200, 200, 0), false);
		CHECK_FALSE(body.consumed);
		CHECK_FALSE(model.openMenu.has_value());
	}
}

TEST_CASE("menu bar pointer leave clears hover and keeps menu open") {
	MenuBarModel model;
	model.openMenu = ApplicationMenu::File;
	model.hoveredHeading = ApplicationMenu::File;
	model.hoveredItem = ApplicationAction::Save;
	model.pressOrigin = {MenuBarPressKind::Item, ApplicationMenu::File,
		ApplicationAction::Save};
	const MenuBarLayout layout = Layout(model);

	const MenuBarPointerResult leave = HandleMenuBarPointer(model, layout,
		MakePointer(PointerAction::Leave, 0, 0), false);
	CHECK_FALSE(leave.consumed);
	CHECK_FALSE(leave.pointerOverMenu);
	CHECK(leave.barDirty);
	CHECK(leave.frameDirty);
	REQUIRE(model.openMenu.has_value());
	CHECK(*model.openMenu == ApplicationMenu::File);
	CHECK_FALSE(model.hoveredHeading.has_value());
	CHECK_FALSE(model.hoveredItem.has_value());
	CHECK_FALSE(model.pressOrigin.has_value());
}

TEST_CASE("menu bar pointer respects editor capture") {
	MenuBarModel model;
	const MenuBarLayout closed = Layout(model);
	const Point file = Center(closed.headings[0].bounds);

	// Capture blocks open.
	const MenuBarPointerResult press = HandleMenuBarPointer(model, closed,
		MakePointer(PointerAction::Press, file.x, file.y, 0), true);
	CHECK_FALSE(press.consumed);
	CHECK_FALSE(model.openMenu.has_value());

	// Capture blocks activation release while a menu is already open.
	model.openMenu = ApplicationMenu::File;
	model.pressOrigin = {MenuBarPressKind::Item, ApplicationMenu::File,
		ApplicationAction::Save};
	const MenuBarLayout openLayout = Layout(model);
	const auto *save = FindItem(openLayout, ApplicationAction::Save);
	REQUIRE(save);
	const Point savePt = Center(save->row);
	const MenuBarPointerResult release = HandleMenuBarPointer(model, openLayout,
		MakePointer(PointerAction::Release, savePt.x, savePt.y, 0), true);
	CHECK_FALSE(release.consumed);
	CHECK_FALSE(release.activated.has_value());
	// Capture path does not clear open state; the drag still owns the pointer.
	REQUIRE(model.openMenu.has_value());
}

TEST_CASE("menu bar pointer reports arrow cursor over bar and dropdown") {
	MenuBarModel model;
	const MenuBarLayout closed = Layout(model);
	const Point file = Center(closed.headings[0].bounds);
	const MenuBarPointerResult overHeading = HandleMenuBarPointer(model, closed,
		MakePointer(PointerAction::Move, file.x, file.y), false);
	CHECK(overHeading.pointerOverMenu);
	CHECK(overHeading.consumed);
	REQUIRE(model.hoveredHeading.has_value());
	CHECK(*model.hoveredHeading == ApplicationMenu::File);

	model.openMenu = ApplicationMenu::File;
	const MenuBarLayout openLayout = Layout(model);
	const auto *save = FindItem(openLayout, ApplicationAction::Save);
	REQUIRE(save);
	const Point savePt = Center(save->row);
	const MenuBarPointerResult overItem = HandleMenuBarPointer(model, openLayout,
		MakePointer(PointerAction::Move, savePt.x, savePt.y), false);
	CHECK(overItem.pointerOverMenu);
	CHECK(overItem.consumed);

	const MenuBarPointerResult overBody = HandleMenuBarPointer(model, openLayout,
		MakePointer(PointerAction::Move, 50, 280), false);
	// Open menu consumes body moves so editor hover does not run underneath.
	CHECK(overBody.consumed);
	CHECK_FALSE(overBody.pointerOverMenu);
}

TEST_CASE("menu bar height is fixed and positive") {
	CHECK(MenuBarHeight() > 0);
	CHECK(MenuBarHeight() < 40);
}

TEST_CASE("menu bar layout places File Edit and Recent headings") {
	const MenuBarLayout layout = LayoutMenuBar(400, 300, ClosedBar());
	CHECK(layout.bar == PRectangle::FromInts(0, 0, 400, MenuBarHeight()));
	REQUIRE(layout.headings.size() == 3);
	CHECK(layout.headings[0].menu == ApplicationMenu::File);
	CHECK(layout.headings[0].label == "File");
	CHECK(layout.headings[1].menu == ApplicationMenu::Edit);
	CHECK(layout.headings[1].label == "Edit");
	CHECK(layout.headings[2].menu == ApplicationMenu::Recent);
	CHECK(layout.headings[2].label == "Recent");
	CHECK(NonEmpty(layout.headings[0].bounds));
	CHECK(NonEmpty(layout.headings[1].bounds));
	CHECK(NonEmpty(layout.headings[2].bounds));
	CHECK(layout.headings[0].bounds.left == 0);
	CHECK(layout.headings[0].bounds.right == layout.headings[1].bounds.left);
	CHECK(layout.headings[1].bounds.right == layout.headings[2].bounds.left);
	CHECK(layout.headings[2].bounds.right <= 400);
	CHECK_FALSE(NonEmpty(layout.dropdown));
	CHECK(layout.items.empty());
}

TEST_CASE("menu bar Recent dropdown shows paths clear and empty state") {
	MenuBarModel model = OpenMenu(ApplicationMenu::Recent);

	SECTION("empty list has one disabled placeholder") {
		const MenuBarLayout layout = Layout(model);
		REQUIRE(layout.items.size() == 1);
		const MenuBarItemLayout &empty = layout.items.front();
		CHECK(empty.item.kind == MenuBarItemKind::EmptyRecentFiles);
		CHECK(empty.labelText == "No Recent Files");
		CHECK_FALSE(empty.enabled);
		CHECK_FALSE(empty.separatorBefore);
	}

	SECTION("paths show filename first and Clear after a separator") {
		model.recentFiles = {
			"/work/project/notes.txt",
			"/other/draft.md",
		};
		const MenuBarLayout layout = Layout(model, 600);
		REQUIRE(layout.items.size() == 3);
		CHECK(layout.dropdown.Width() == 440);
		CHECK(layout.items[0].item == MenuBarItemId::RecentFile(0));
		CHECK(layout.items[0].labelText == "notes.txt \xe2\x80\x94 /work/project");
		CHECK(layout.items[1].item == MenuBarItemId::RecentFile(1));
		const MenuBarItemLayout *clear =
			FindMenuItem(layout, MenuBarItemKind::ClearRecentFiles);
		REQUIRE(clear);
		CHECK(clear->labelText == "Clear Recent Files");
		CHECK(clear->separatorBefore);
		CHECK(clear->enabled);
	}

	SECTION("wide Recent dropdown clamps to a narrow frame") {
		model.recentFiles = {"/work/notes.txt"};
		const MenuBarLayout layout = Layout(model, 180);
		CHECK(layout.dropdown.left >= 0);
		CHECK(layout.dropdown.right <= 180);
	}
}

TEST_CASE("menu bar Recent pointer and keyboard activate dynamic rows") {
	MenuBarModel model = OpenMenu(ApplicationMenu::Recent);
	model.recentFiles = {"/work/one.txt", "/work/two.txt"};

	SECTION("matching pointer press and release activates a recent path") {
		const MenuBarLayout layout = Layout(model, 600);
		const MenuBarItemLayout *first =
			FindMenuItem(layout, MenuBarItemKind::RecentFile, 0);
		REQUIRE(first);
		const Point point = Center(first->row);
		const MenuBarPointerResult press = HandleMenuBarPointer(model, layout,
			MakePointer(PointerAction::Press, point.x, point.y, 0), false);
		CHECK(press.consumed);
		const MenuBarPointerResult release = HandleMenuBarPointer(model, layout,
			MakePointer(PointerAction::Release, point.x, point.y, 0), false);
		REQUIRE(release.activated.has_value());
		CHECK(release.activated->kind == MenuBarItemKind::RecentFile);
		CHECK(release.activated->recentIndex == 0);
		CHECK_FALSE(model.openMenu.has_value());
	}

	SECTION("Alt R focuses Recent and arrows include Clear with wrapping") {
		CloseMenuBar(model);
		const MenuBarKeyboardResult open =
			HandleMenuBarKeyboard(model, MakeLetter('R', KeyMod::Alt));
		CHECK(open.consumed);
		CHECK(model.openMenu == ApplicationMenu::Recent);
		REQUIRE(model.focusedItem.has_value());
		CHECK(*model.focusedItem == MenuBarItemId::RecentFile(0));

		(void)HandleMenuBarKeyboard(model, MakeKey(Keys::Down));
		CHECK(*model.focusedItem == MenuBarItemId::RecentFile(1));
		(void)HandleMenuBarKeyboard(model, MakeKey(Keys::Down));
		CHECK(model.focusedItem->kind == MenuBarItemKind::ClearRecentFiles);
		(void)HandleMenuBarKeyboard(model, MakeKey(Keys::Down));
		CHECK(*model.focusedItem == MenuBarItemId::RecentFile(0));

		model.focusedItem = MenuBarItemId::ClearRecentFiles();
		const MenuBarKeyboardResult activate =
			HandleMenuBarKeyboard(model, MakeKey(Keys::Return));
		REQUIRE(activate.activated.has_value());
		CHECK(activate.activated->kind == MenuBarItemKind::ClearRecentFiles);
	}

	SECTION("empty Recent menu has no keyboard focus") {
		CloseMenuBar(model);
		model.recentFiles.clear();
		(void)HandleMenuBarKeyboard(model, MakeLetter('R', KeyMod::Alt));
		CHECK(model.openMenu == ApplicationMenu::Recent);
		CHECK_FALSE(model.focusedItem.has_value());
	}
}

TEST_CASE("menu bar zero width yields empty layout") {
	const MenuBarLayout layout = LayoutMenuBar(0, 300, OpenMenu(ApplicationMenu::File));
	CHECK_FALSE(NonEmpty(layout.bar));
	CHECK(layout.headings.empty());
	CHECK(HitTestMenuBar(layout, Point(0, 0)).kind == MenuBarHit::None);
}

TEST_CASE("menu bar File dropdown lists actions separators and columns") {
	const MenuBarLayout layout = LayoutMenuBar(400, 300, OpenMenu(ApplicationMenu::File));
	CHECK(NonEmpty(layout.dropdown));
	CHECK(layout.dropdown.top == MenuBarHeight());
	// Dropdown hangs under the File heading when space allows.
	CHECK(layout.dropdown.left == layout.headings[0].bounds.left);
	REQUIRE(layout.items.size() == CountMenuItems(ApplicationMenu::File));

	const auto *newTab = FindItem(layout, ApplicationAction::NewTab);
	const auto *open = FindItem(layout, ApplicationAction::Open);
	const auto *save = FindItem(layout, ApplicationAction::Save);
	const auto *saveAs = FindItem(layout, ApplicationAction::SaveAs);
	const auto *closeTab = FindItem(layout, ApplicationAction::CloseTab);
	const auto *quit = FindItem(layout, ApplicationAction::Quit);
	REQUIRE(newTab);
	REQUIRE(open);
	REQUIRE(save);
	REQUIRE(saveAs);
	REQUIRE(closeTab);
	REQUIRE(quit);

	CHECK_FALSE(newTab->separatorBefore);
	CHECK_FALSE(open->separatorBefore);
	CHECK(save->separatorBefore);
	CHECK_FALSE(saveAs->separatorBefore);
	CHECK(closeTab->separatorBefore);
	CHECK(quit->separatorBefore);
	CHECK(NonEmpty(save->separator));
	CHECK(NonEmpty(newTab->label));
	CHECK(NonEmpty(newTab->shortcut));
	CHECK(newTab->labelText == "New Tab");
	CHECK(newTab->shortcutText == "Ctrl+N");
	CHECK(newTab->label.right <= newTab->shortcut.left + 0.5);
	// Rows stack top to bottom.
	CHECK(newTab->row.bottom <= open->row.top + 0.5);
	CHECK(open->row.bottom <= save->row.top + 0.5);
}

TEST_CASE("menu bar Edit dropdown reflects enablement flags") {
	MenuBarModel model = OpenMenu(ApplicationMenu::Edit);
	model.undoEnabled = false;
	model.cutEnabled = false;
	model.pasteEnabled = true;
	const MenuBarLayout layout = LayoutMenuBar(400, 300, model);
	REQUIRE(layout.items.size() == CountMenuItems(ApplicationMenu::Edit));

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
	CHECK(selectAll->separatorBefore);
}

TEST_CASE("menu bar narrow window clamps dropdown into the frame") {
	// Frame narrower than the preferred dropdown and headings.
	const int width = 120;
	const MenuBarLayout layout = LayoutMenuBar(width, 200,
		OpenMenu(ApplicationMenu::Edit));
	REQUIRE(NonEmpty(layout.dropdown));
	CHECK(layout.dropdown.left >= 0);
	CHECK(layout.dropdown.right <= width);
	CHECK(layout.dropdown.Width() <= width);
	// Edit is the second heading; dropdown may shift left off the heading.
	const bool clampedToRight = layout.dropdown.right == width;
	const bool alignedToEdit =
		layout.dropdown.left == layout.headings[1].bounds.left;
	CHECK((clampedToRight || alignedToEdit));

	// Extremely narrow: bar still lays out what fits.
	const MenuBarLayout tiny = LayoutMenuBar(20, 100, OpenMenu(ApplicationMenu::File));
	CHECK(NonEmpty(tiny.bar));
	CHECK(tiny.bar.right == 20);
	if (NonEmpty(tiny.dropdown)) {
		CHECK(tiny.dropdown.left >= 0);
		CHECK(tiny.dropdown.right <= 20);
	}
}

TEST_CASE("menu bar short frame clamps dropdown bottom") {
	MenuBarModel model = OpenMenu(ApplicationMenu::File);
	// Barely taller than the bar; dropdown must not extend past the frame.
	const int height = MenuBarHeight() + 40;
	const MenuBarLayout layout = LayoutMenuBar(400, height, model);
	REQUIRE(NonEmpty(layout.dropdown));
	CHECK(layout.dropdown.bottom <= height);
	CHECK(layout.dropdown.top == MenuBarHeight());

	SECTION("no dropdown fits when the frame ends at the bar") {
		const MenuBarLayout barOnly =
			LayoutMenuBar(400, MenuBarHeight(), model);
		CHECK_FALSE(NonEmpty(barOnly.dropdown));
		CHECK_FALSE(barOnly.dropdownMenu.has_value());
		CHECK(barOnly.items.empty());
	}
}

TEST_CASE("menu bar hit-test headings items and outside") {
	const MenuBarLayout closed = LayoutMenuBar(400, 300, ClosedBar());
	const MenuBarHitResult onFile = HitTestMenuBar(closed,
		Center(closed.headings[0].bounds));
	CHECK(onFile.kind == MenuBarHit::Heading);
	CHECK(onFile.menu == ApplicationMenu::File);

	const MenuBarHitResult onEdit = HitTestMenuBar(closed,
		Center(closed.headings[1].bounds));
	CHECK(onEdit.kind == MenuBarHit::Heading);
	CHECK(onEdit.menu == ApplicationMenu::Edit);

	// Empty bar chrome past the headings.
	const MenuBarHitResult onBar = HitTestMenuBar(closed,
		Point(300, MenuBarHeight() / 2.0));
	CHECK(onBar.kind == MenuBarHit::Bar);

	const MenuBarHitResult outside = HitTestMenuBar(closed,
		Point(10, MenuBarHeight() + 5));
	CHECK(outside.kind == MenuBarHit::None);

	const MenuBarLayout open = LayoutMenuBar(400, 300, OpenMenu(ApplicationMenu::File));
	const auto *save = FindItem(open, ApplicationAction::Save);
	REQUIRE(save);
	const MenuBarHitResult onItem = HitTestMenuBar(open, Center(save->row));
	CHECK(onItem.kind == MenuBarHit::Item);
	CHECK(onItem.action == ApplicationAction::Save);
	CHECK(onItem.menu == ApplicationMenu::File);

	// Separator band is not an item activation.
	if (NonEmpty(save->separator)) {
		const MenuBarHitResult onSep = HitTestMenuBar(open,
			Point((save->separator.left + save->separator.right) / 2.0,
				save->separator.top));
		CHECK(onSep.kind == MenuBarHit::Dropdown);
	}

	// Point inside dropdown padding but not on a row.
	const MenuBarHitResult onPad = HitTestMenuBar(open,
		Point(open.dropdown.left + 2, open.dropdown.top + 1));
	CHECK(onPad.kind == MenuBarHit::Dropdown);

	const MenuBarLayout edit = LayoutMenuBar(400, 300,
		OpenMenu(ApplicationMenu::Edit));
	const MenuBarHitResult onEditPad = HitTestMenuBar(edit,
		Point(edit.dropdown.left + 2, edit.dropdown.top + 1));
	CHECK(onEditPad.kind == MenuBarHit::Dropdown);
	CHECK(onEditPad.menu == ApplicationMenu::Edit);
}

TEST_CASE("menu bar hit-test uses half-open heading bounds") {
	const MenuBarLayout layout = LayoutMenuBar(400, 300, ClosedBar());
	const MenuBarHitResult sharedEdge = HitTestMenuBar(layout,
		Point(layout.headings[0].bounds.right, MenuBarHeight() / 2.0));
	CHECK(sharedEdge.kind == MenuBarHit::Heading);
	CHECK(sharedEdge.menu == ApplicationMenu::Edit);

	CHECK(HitTestMenuBar(layout, Point(layout.bar.right, 4)).kind ==
		MenuBarHit::None);
	CHECK(HitTestMenuBar(layout, Point(4, layout.bar.bottom)).kind ==
		MenuBarHit::None);
}

TEST_CASE("menu bar paint closed open hovered focused and disabled") {
	ApplicationEditor editor(360, 220);
	editor.LoadInitialBuffer("menu paint\n");
	MenuBarPainter painter;

	SECTION("closed bar differs from editor body") {
		const MenuBarModel model = ClosedBar();
		const MenuBarLayout layout = LayoutMenuBar(360, 220, model);
		const auto pixels = PaintMenu(editor, painter, layout, model);
		REQUIRE(pixels.size() == 360U * 220U * 4U);
		const Rgba barPx = Sample(pixels, 360, 20, MenuBarHeight() / 2);
		const Rgba bodyPx = Sample(pixels, 360, 20, MenuBarHeight() + 20);
		CHECK(Differs(barPx, bodyPx));
		CHECK(barPx.a == 0xff);
	}

	SECTION("open heading highlight differs from idle heading") {
		MenuBarModel open = OpenMenu(ApplicationMenu::File);
		const MenuBarLayout openLayout = LayoutMenuBar(360, 220, open);
		const auto openPixels = PaintMenu(editor, painter, openLayout, open);
		const int fileX = static_cast<int>(Center(openLayout.headings[0].bounds).x);
		const int editX = static_cast<int>(Center(openLayout.headings[1].bounds).x);
		const int y = MenuBarHeight() / 2;
		const Rgba fileOpen = Sample(openPixels, 360, fileX, y);
		const Rgba editIdle = Sample(openPixels, 360, editX, y);
		CHECK(Differs(fileOpen, editIdle));
	}

	SECTION("hovered heading paints distinctly") {
		MenuBarModel model = ClosedBar();
		model.hoveredHeading = ApplicationMenu::Edit;
		const MenuBarLayout layout = LayoutMenuBar(360, 220, model);
		const auto pixels = PaintMenu(editor, painter, layout, model);
		const int fileX = static_cast<int>(Center(layout.headings[0].bounds).x);
		const int editX = static_cast<int>(Center(layout.headings[1].bounds).x);
		const int y = MenuBarHeight() / 2;
		CHECK(Differs(Sample(pixels, 360, fileX, y), Sample(pixels, 360, editX, y)));
	}

	SECTION("dropdown panel and focus fill differ from the bar") {
		MenuBarModel model = OpenMenu(ApplicationMenu::Edit);
		model.focusedItem = ApplicationAction::Copy;
		const MenuBarLayout layout = LayoutMenuBar(360, 220, model);
		const auto *copy = FindItem(layout, ApplicationAction::Copy);
		REQUIRE(copy);
		const auto pixels = PaintMenu(editor, painter, layout, model);
		const int rowX = static_cast<int>(Center(copy->row).x);
		const int rowY = static_cast<int>(Center(copy->row).y);
		const Rgba focused = Sample(pixels, 360, rowX, rowY);
		const Rgba barPx = Sample(pixels, 360, 200, MenuBarHeight() / 2);
		CHECK(Differs(focused, barPx));
		// Focused row is cooler (blue-tinted) than a neutral bar gray.
		CHECK(focused.b >= focused.r);
		// Focus fill must not cover the panel's right border.
		const Rgba edge = Sample(pixels, 360,
			static_cast<int>(layout.dropdown.right) - 1, rowY);
		CHECK(edge.r == 0xa0);
		CHECK(edge.g == 0xa0);
		CHECK(edge.b == 0xa0);
		CHECK(edge.a == 0xff);
	}

	SECTION("disabled item ink is lighter than enabled item ink") {
		MenuBarModel model = OpenMenu(ApplicationMenu::Edit);
		model.undoEnabled = false;
		model.redoEnabled = true;
		const MenuBarLayout layout = LayoutMenuBar(360, 220, model);
		const auto *undo = FindItem(layout, ApplicationAction::Undo);
		const auto *redo = FindItem(layout, ApplicationAction::Redo);
		REQUIRE(undo);
		REQUIRE(redo);
		const auto pixels = PaintMenu(editor, painter, layout, model);
		// Sample the label column, not the focus/hover fill.
		const int undoX = static_cast<int>(undo->label.left + 4);
		const int undoY = static_cast<int>(Center(undo->row).y);
		const int redoX = static_cast<int>(redo->label.left + 4);
		const int redoY = static_cast<int>(Center(redo->row).y);
		const Rgba disabled = Sample(pixels, 360, undoX, undoY);
		const Rgba enabled = Sample(pixels, 360, redoX, redoY);
		// Disabled text is lighter gray; luminance of disabled >= enabled.
		const int disabledLum = static_cast<int>(disabled.r) + disabled.g + disabled.b;
		const int enabledLum = static_cast<int>(enabled.r) + enabled.g + enabled.b;
		CHECK(disabledLum >= enabledLum);
	}

	SECTION("hover item fill differs from unhovered row") {
		MenuBarModel model = OpenMenu(ApplicationMenu::File);
		model.hoveredItem = ApplicationAction::Open;
		const MenuBarLayout layout = LayoutMenuBar(360, 220, model);
		const auto *openItem = FindItem(layout, ApplicationAction::Open);
		const auto *newTab = FindItem(layout, ApplicationAction::NewTab);
		REQUIRE(openItem);
		REQUIRE(newTab);
		const auto pixels = PaintMenu(editor, painter, layout, model);
		const Rgba hovered = Sample(pixels, 360,
			static_cast<int>(Center(openItem->row).x),
			static_cast<int>(Center(openItem->row).y));
		const Rgba idle = Sample(pixels, 360,
			static_cast<int>(Center(newTab->row).x),
			static_cast<int>(Center(newTab->row).y));
		CHECK(Differs(hovered, idle));
	}

	SECTION("scaled framebuffer still paints bar chrome in logical coords") {
		editor.Resize(200, 120);
		editor.SetFrameBufferSize(400, 240);
		const MenuBarModel model = OpenMenu(ApplicationMenu::File);
		const MenuBarLayout layout = LayoutMenuBar(200, 120, model);
		CHECK(layout.bar.right == 200);
		const auto pixels = PaintMenu(editor, painter, layout, model);
		REQUIRE(pixels.size() == 200U * 120U * 4U);
		const Rgba barMid = Sample(pixels, 200, 20, MenuBarHeight() / 2);
		const Rgba client = Sample(pixels, 200, 20, MenuBarHeight() + 10);
		CHECK(Differs(barMid, client));
		CHECK(barMid.a == 0xff);
	}
}

TEST_CASE("menu bar model enablement matches edit flags") {
	MenuBarModel model;
	CHECK(model.IsEnabled(ApplicationAction::Save));
	CHECK(model.IsEnabled(ApplicationAction::Undo));
	model.undoEnabled = false;
	model.copyEnabled = false;
	CHECK_FALSE(model.IsEnabled(ApplicationAction::Undo));
	CHECK_FALSE(model.IsEnabled(ApplicationAction::Copy));
	CHECK(model.IsEnabled(ApplicationAction::Paste));
	CHECK(model.IsEnabled(ApplicationAction::Quit));
}

TEST_CASE("menu bar action state follows editor enablement") {
	using Scalpel::UpdateMenuBarActionState;

	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("state");
	MenuBarModel model;
	// Defaults look fully enabled until refreshed from the editor.
	CHECK(model.IsEnabled(ApplicationAction::Undo));
	CHECK(UpdateMenuBarActionState(model, editor));
	CHECK_FALSE(model.IsEnabled(ApplicationAction::Undo));
	CHECK_FALSE(model.IsEnabled(ApplicationAction::Redo));
	CHECK_FALSE(model.IsEnabled(ApplicationAction::Cut));
	CHECK_FALSE(model.IsEnabled(ApplicationAction::Copy));
	CHECK_FALSE(model.IsEnabled(ApplicationAction::Paste));
	CHECK(model.IsEnabled(ApplicationAction::SelectAll));
	CHECK(model.IsEnabled(ApplicationAction::Save));
	// Stable while nothing changes.
	CHECK_FALSE(UpdateMenuBarActionState(model, editor));

	editor.HandleKeyboardInput(
		{Keys::End, KeyMod::Norm, {}, 1, true});
	editor.HandleKeyboardInput(
		{static_cast<Keys>(0), KeyMod::Norm, "x", 2, true});
	CHECK(UpdateMenuBarActionState(model, editor));
	CHECK(model.IsEnabled(ApplicationAction::Undo));

	editor.SetClipboardPasteAvailable(true);
	CHECK(UpdateMenuBarActionState(model, editor));
	CHECK(model.IsEnabled(ApplicationAction::Paste));
	model.openMenu = ApplicationMenu::Edit;
	model.focusedItem = ApplicationAction::Paste;
	editor.SetClipboardPasteAvailable(false);
	CHECK(UpdateMenuBarActionState(model, editor));
	CHECK_FALSE(model.IsEnabled(ApplicationAction::Paste));
	CHECK(model.focusedItem == ApplicationAction::Undo);

	// Layout reads the refreshed flags into item rows.
	const MenuBarLayout layout = LayoutMenuBar(400, 300, model);
	const auto *undo = FindItem(layout, ApplicationAction::Undo);
	const auto *paste = FindItem(layout, ApplicationAction::Paste);
	REQUIRE(undo);
	REQUIRE(paste);
	CHECK(undo->enabled);
	CHECK_FALSE(paste->enabled);
}

TEST_CASE("menu bar editor integration stacks chrome above the inset client") {
	using Scalpel::LayoutTabStrip;
	using Scalpel::TabStripHeight;
	using Scalpel::TabStripModel;
	using Scalpel::TabStripPainter;
	using Scalpel::TabStripTab;

	const int menuH = MenuBarHeight();
	const int stripH = TabStripHeight();
	const int inset = menuH + stripH;
	ApplicationEditor editor(320, 160);
	editor.LoadInitialBuffer("editor body\nsecond line\n");
	editor.SetTopChromeInset(inset);
	(void)editor.TakeFrameDamage();

	MenuBarPainter menuPainter;
	TabStripPainter stripPainter;
	const MenuBarModel menuModel = ClosedBar();
	TabStripModel stripModel;
	TabStripTab active;
	active.id = 1;
	active.label = "active";
	active.active = true;
	stripModel.tabs.push_back(active);

	int chromePaints = 0;
	editor.SetPermanentChromePainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			++chromePaints;
			CHECK(width == 320);
			CHECK(height == 160);
			const MenuBarLayout menuLayout =
				LayoutMenuBar(width, height, menuModel);
			menuPainter.PaintBar(surface, menuLayout, menuModel);
			const auto stripLayout =
				LayoutTabStrip(width, stripModel, menuH);
			stripPainter.Paint(surface, stripLayout, stripModel);
		});

	// Full frame: both chrome bands and the editor paint.
	editor.RenderFrame({PRectangle::FromInts(0, 0, 320, 160)});
	CHECK(chromePaints == 1);
	CHECK(editor.LastPaintRectangle() ==
		PRectangle::FromInts(0, inset, 320, 160));
	CHECK(editor.TopChromeRectangle() ==
		PRectangle::FromInts(0, 0, 320, inset));

	const auto pixels = editor.FramePixels();
	REQUIRE(pixels.size() == 320U * 160U * 4U);
	const Rgba barPx = Sample(pixels, 320, 24, menuH / 2);
	const Rgba stripPx = Sample(pixels, 320, 40, menuH + stripH / 2);
	const Rgba bodyPx = Sample(pixels, 320, 160, inset + 12);
	CHECK(barPx.a == 0xff);
	CHECK(stripPx.a == 0xff);
	CHECK(Differs(barPx, bodyPx));
	CHECK(Differs(stripPx, bodyPx));

	// Editor-only damage keeps permanent chrome out of the paint path.
	editor.RenderFrame({PRectangle::FromInts(20, inset + 4, 80, inset + 30)});
	CHECK(chromePaints == 1);

	// Bar-only damage (menu band) repaints chrome without expanding editor paint.
	editor.RenderFrame({PRectangle::FromInts(0, 0, 320, menuH)});
	CHECK(chromePaints == 2);
	CHECK(editor.LastPaintRectangle().Height() == 0);

	// Strip-only damage (tab band) also repaints chrome alone.
	editor.RenderFrame({PRectangle::FromInts(0, menuH, 320, inset)});
	CHECK(chromePaints == 3);
	CHECK(editor.LastPaintRectangle().Height() == 0);

	// InvalidateTopChrome damages the whole stacked band.
	(void)editor.TakeFrameDamage();
	editor.InvalidateTopChrome();
	const auto damage = editor.TakeFrameDamage();
	REQUIRE_FALSE(damage.empty());
	CHECK(std::find(damage.begin(), damage.end(),
		PRectangle::FromInts(0, 0, 320, inset)) != damage.end());
}

TEST_CASE("menu bar editor integration overlay draws above stacked chrome") {
	using Scalpel::LayoutTabStrip;
	using Scalpel::TabStripHeight;
	using Scalpel::TabStripModel;
	using Scalpel::TabStripPainter;
	using Scalpel::TabStripTab;

	const int menuH = MenuBarHeight();
	const int stripH = TabStripHeight();
	const int inset = menuH + stripH;
	ApplicationEditor editor(280, 120);
	editor.LoadInitialBuffer("body\n");
	editor.SetTopChromeInset(inset);
	(void)editor.TakeFrameDamage();

	MenuBarPainter menuPainter;
	TabStripPainter stripPainter;
	const MenuBarModel menuModel = ClosedBar();
	TabStripModel stripModel;
	TabStripTab tab;
	tab.id = 1;
	tab.label = "tab";
	tab.active = true;
	stripModel.tabs.push_back(tab);

	editor.SetPermanentChromePainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			const MenuBarLayout menuLayout =
				LayoutMenuBar(width, height, menuModel);
			menuPainter.PaintBar(surface, menuLayout, menuModel);
			const auto stripLayout =
				LayoutTabStrip(width, stripModel, menuH);
			stripPainter.Paint(surface, stripLayout, stripModel);
		});

	bool overlayCalled = false;
	editor.SetOverlayPainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			overlayCalled = true;
			// Opaque mark spanning both chrome bands.
			surface.FillRectangle(
				PRectangle::FromInts(width / 2 - 10, 2, width / 2 + 10,
					inset - 2),
				Scintilla::Internal::Fill(
					Scintilla::Internal::ColourRGBA(0xff, 0x00, 0xff, 0xff)));
			(void)height;
		});

	editor.RenderFrame({PRectangle::FromInts(0, 0, 280, 120)});
	CHECK(overlayCalled);
	CHECK(editor.LastPaintRectangle() ==
		PRectangle::FromInts(0, inset, 280, 120));

	const auto pixels = editor.FramePixels();
	const Rgba overBar = Sample(pixels, 280, 140, menuH / 2);
	const Rgba overStrip = Sample(pixels, 280, 140, menuH + stripH / 2);
	CHECK(overBar.r == 0xff);
	CHECK(overBar.b == 0xff);
	CHECK(overStrip.r == 0xff);
	CHECK(overStrip.b == 0xff);
	// Away from the overlay mark, chrome still shows.
	const Rgba barEdge = Sample(pixels, 280, 20, menuH / 2);
	CHECK(Differs(overBar, barEdge));
}

TEST_CASE("menu bar shell integration cancels IME when a menu opens") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("base");

	ApplicationTextInputBatch preedit;
	preedit.preedit = ApplicationTextInputPreedit{"\xC3\xA9", 2, 2};
	editor.HandleTextInputBatch(preedit);
	CHECK(editor.Text() == "\xC3\xA9" "base");
	CHECK(editor.ImeIndicatorAt(0) != 0);

	MenuBarModel model;
	(void)UpdateMenuBarActionState(model, editor);
	const MenuBarKeyboardResult open =
		HandleMenuBarKeyboard(model, MakeKey(Keys::Menu));
	REQUIRE(model.openMenu.has_value());
	CHECK(*model.openMenu == ApplicationMenu::File);
	CHECK(open.frameDirty);
	// Shell cancels tentative IME on the open transition.
	editor.CancelActiveTextInput();
	CHECK(editor.Text() == "base");
	CHECK(editor.ImeIndicatorAt(0) == 0);

	// While open, further IME batches must not reach the editor (main drops
	// them). Simulate the drop by not calling HandleTextInputBatch.
	ApplicationTextInputBatch ignored;
	ignored.preedit = ApplicationTextInputPreedit{"x", 1, 1};
	// After cancel, a deliberate batch would re-enter preedit; chrome owns
	// input until close, so the shell leaves the buffer alone.
	CHECK(editor.Text() == "base");
	(void)ignored;
}

TEST_CASE("menu bar shell integration closes before portal prompt and tab work") {
	ApplicationEditor editor(360, 200);
	editor.LoadInitialBuffer("shell\n");
	DocumentWorkspace workspace(editor);
	MenuBarModel model;
	(void)UpdateMenuBarActionState(model, editor);

	SECTION("Open from the menu closes the dropdown before the portal request") {
		model.openMenu = ApplicationMenu::File;
		model.focusedItem = ApplicationAction::Open;
		const MenuBarKeyboardResult result =
			HandleMenuBarKeyboard(model, MakeKey(Keys::Return));
		REQUIRE(result.activated.has_value());
		CHECK(*result.activated == ApplicationAction::Open);
		CHECK_FALSE(model.openMenu.has_value());
		DispatchApplicationAction(result.activated->action, workspace, editor);
		const auto requests = workspace.TakeRequests();
		bool showOpen = false;
		for (const DocumentShellRequest request : requests) {
			if (request == DocumentShellRequest::ShowOpen) {
				showOpen = true;
			}
		}
		CHECK(showOpen);
	}

	SECTION("dirty Quit closes the menu before the unsaved prompt begins") {
		editor.HandleKeyboardInput(
			{static_cast<Keys>(0), KeyMod::Norm, "!", 1, true});
		REQUIRE(editor.Modified());
		model.openMenu = ApplicationMenu::File;
		model.focusedItem = ApplicationAction::Quit;
		const MenuBarKeyboardResult result =
			HandleMenuBarKeyboard(model, MakeKey(Keys::Return));
		REQUIRE(result.activated.has_value());
		CHECK(*result.activated == ApplicationAction::Quit);
		CHECK_FALSE(model.openMenu.has_value());
		DispatchApplicationAction(result.activated->action, workspace, editor);
		CHECK(workspace.PromptActive());
		const auto requests = workspace.TakeRequests();
		bool promptBegan = false;
		for (const DocumentShellRequest request : requests) {
			if (request == DocumentShellRequest::PromptBegan) {
				promptBegan = true;
			}
		}
		CHECK(promptBegan);
	}

	SECTION("shell dismisses a leftover open menu when a prompt begins") {
		// Compositor close can raise a prompt while a menu is still open.
		editor.HandleKeyboardInput(
			{static_cast<Keys>(0), KeyMod::Norm, "?", 2, true});
		REQUIRE(editor.Modified());
		model.openMenu = ApplicationMenu::Edit;
		model.focusedItem = ApplicationAction::Undo;
		workspace.RequestClose();
		CHECK(workspace.PromptActive());
		// main closes the menu on PromptBegan so the card owns the overlay.
		CloseMenuBar(model);
		CHECK_FALSE(model.openMenu.has_value());
		CHECK_FALSE(model.focusedItem.has_value());
	}

	SECTION("tab activation dismisses an open menu before switching") {
		const auto first = workspace.ActiveTab();
		workspace.NewTab();
		(void)workspace.TakeRequests();
		const auto second = workspace.ActiveTab();
		REQUIRE(second != first);
		model.openMenu = ApplicationMenu::File;
		model.focusedItem = ApplicationAction::NewTab;
		// Shell closes the menu before ActivateTab so chrome state stays clean.
		CloseMenuBar(model);
		workspace.ActivateTab(first);
		CHECK_FALSE(model.openMenu.has_value());
		CHECK(workspace.ActiveTab() == first);
	}
}

TEST_CASE("menu bar shell integration focus loss and force-close clear the menu") {
	MenuBarModel model = OpenMenu(ApplicationMenu::File);
	model.focusedItem = ApplicationAction::Save;
	model.hoveredItem = ApplicationAction::Open;
	model.pressOrigin = {MenuBarPressKind::Item, ApplicationMenu::File,
		ApplicationAction::Open};

	// Focus loss and force-close both dismiss without running an action.
	CloseMenuBar(model);
	CHECK_FALSE(model.openMenu.has_value());
	CHECK_FALSE(model.focusedItem.has_value());
	CHECK_FALSE(model.hoveredItem.has_value());
	CHECK_FALSE(model.pressOrigin.has_value());
	// Heading hover may remain for bar paint after a soft close; force-close
	// also leaves the process, so no further paint is required.
}

TEST_CASE("menu bar shell integration keeps open menu across resize and scale") {
	ApplicationEditor editor(400, 280);
	editor.LoadInitialBuffer("resize\n");
	const int inset = MenuBarHeight() + Scalpel::TabStripHeight();
	editor.SetTopChromeInset(inset);

	MenuBarModel model = OpenMenu(ApplicationMenu::File);
	model.focusedItem = ApplicationAction::SaveAs;
	(void)UpdateMenuBarActionState(model, editor);

	const MenuBarLayout wide = LayoutMenuBar(400, 280, model);
	REQUIRE(NonEmpty(wide.dropdown));
	REQUIRE(model.openMenu.has_value());

	// Resize keeps the selected menu when it still fits; layout reclamps.
	editor.Resize(120, 160);
	const MenuBarLayout narrow =
		LayoutMenuBar(editor.FrameWidth(), editor.FrameHeight(), model);
	CHECK(model.openMenu.has_value());
	CHECK(*model.openMenu == ApplicationMenu::File);
	CHECK(model.focusedItem == ApplicationAction::SaveAs);
	REQUIRE(NonEmpty(narrow.dropdown));
	CHECK(narrow.dropdown.left >= 0);
	CHECK(narrow.dropdown.right <= editor.FrameWidth());
	CHECK(narrow.dropdown.bottom <= editor.FrameHeight());

	// Framebuffer scale keeps logical layout; open state is unchanged.
	editor.SetFrameBufferSize(240, 320);
	const MenuBarLayout scaled =
		LayoutMenuBar(editor.FrameWidth(), editor.FrameHeight(), model);
	CHECK(model.openMenu.has_value());
	CHECK(scaled.bar.right == editor.FrameWidth());
	REQUIRE(NonEmpty(scaled.dropdown));
}

TEST_CASE("menu bar shell integration card overlay wins and clear leaves no dropdown") {
	using Scalpel::TabStripHeight;
	using Scalpel::TabStripModel;
	using Scalpel::TabStripPainter;
	using Scalpel::TabStripTab;

	const int menuH = MenuBarHeight();
	const int stripH = TabStripHeight();
	const int inset = menuH + stripH;
	ApplicationEditor editor(320, 200);
	editor.LoadInitialBuffer("overlay\n");
	editor.SetTopChromeInset(inset);
	(void)editor.TakeFrameDamage();

	MenuBarPainter menuPainter;
	UnsavedChangesCardPainter cardPainter;
	TabStripPainter stripPainter;
	MenuBarModel menuModel = OpenMenu(ApplicationMenu::File);
	menuModel.focusedItem = ApplicationAction::NewTab;
	TabStripModel stripModel;
	TabStripTab tab;
	tab.id = 1;
	tab.label = "t";
	tab.active = true;
	stripModel.tabs.push_back(tab);

	editor.SetPermanentChromePainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			const MenuBarLayout menuLayout =
				LayoutMenuBar(width, height, menuModel);
			menuPainter.PaintBar(surface, menuLayout, menuModel);
			const auto stripLayout =
				Scalpel::LayoutTabStrip(width, stripModel, menuH);
			stripPainter.Paint(surface, stripLayout, stripModel);
			(void)height;
		});

	const MenuBarLayout openLayout =
		LayoutMenuBar(320, 200, menuModel);
	REQUIRE(NonEmpty(openLayout.dropdown));
	const Point dropCenter = Center(openLayout.dropdown);

	// Menu dropdown bound as overlay.
	editor.SetOverlayPainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			const MenuBarLayout layout =
				LayoutMenuBar(width, height, menuModel);
			menuPainter.PaintDropdown(surface, layout, menuModel);
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, 320, 200)});
	const auto withMenu = editor.FramePixels();
	const Rgba menuPx = Sample(withMenu, 320,
		static_cast<int>(dropCenter.x), static_cast<int>(dropCenter.y));
	CHECK(menuPx.a == 0xff);

	// Unsaved card wins the overlay slot; menu model is dismissed.
	CloseMenuBar(menuModel);
	const auto cardLayout = LayoutUnsavedChangesCard(320, 200);
	editor.SetOverlayPainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			cardPainter.Paint(surface, cardLayout, "Save changes?", "overlay",
				0);
			(void)width;
			(void)height;
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, 320, 200)});
	const auto withCard = editor.FramePixels();
	const Rgba cardAtDrop = Sample(withCard, 320,
		static_cast<int>(dropCenter.x), static_cast<int>(dropCenter.y));
	// Dropdown panel is gone; card scrim or chrome fills that sample.
	CHECK(Differs(menuPx, cardAtDrop));

	// Clearing the overlay and painting leaves no stale dropdown pixels.
	editor.SetOverlayPainter(nullptr);
	editor.InvalidateClient();
	editor.RenderFrame({PRectangle::FromInts(0, 0, 320, 200)});
	const auto cleared = editor.FramePixels();
	const Rgba clearPx = Sample(cleared, 320,
		static_cast<int>(dropCenter.x), static_cast<int>(dropCenter.y));
	CHECK(Differs(menuPx, clearPx));
	// Sample is in the editor client (below chrome), not opaque menu chrome.
	CHECK(dropCenter.y >= inset);
}

TEST_CASE("menu bar shell integration input priority while open blocks the strip") {
	using Scalpel::HitTestTabStrip;
	using Scalpel::LayoutTabStrip;
	using Scalpel::TabStripHit;
	using Scalpel::TabStripModel;
	using Scalpel::TabStripTab;

	const int menuH = MenuBarHeight();
	MenuBarModel menuModel = OpenMenu(ApplicationMenu::File);
	TabStripModel stripModel;
	TabStripTab tab;
	tab.id = 1;
	tab.label = "blocked";
	tab.active = true;
	stripModel.tabs.push_back(tab);

	const MenuBarLayout menuLayout = LayoutMenuBar(400, 300, menuModel);
	const auto stripLayout = LayoutTabStrip(400, stripModel, menuH);
	REQUIRE_FALSE(stripLayout.tabs.empty());
	// Press on the strip band to the right of the File dropdown so geometry
	// still hits the strip, but not a menu item row.
	REQUIRE(NonEmpty(menuLayout.dropdown));
	const double pressX = menuLayout.dropdown.right + 40.0;
	REQUIRE(pressX < 400.0);
	const double pressY = menuH + (stripLayout.strip.Height() / 2.0);
	const Point press(pressX, pressY);
	const auto stripHit = HitTestTabStrip(stripLayout, press);
	// May be Tab, Add, or strip chrome; any non-None shows the strip owns the
	// geometry that main must not deliver while the menu is open.
	CHECK(stripHit.kind != TabStripHit::None);

	const MenuBarPointerResult menuResult = HandleMenuBarPointer(menuModel,
		menuLayout, MakePointer(PointerAction::Press, press.x, press.y, 0),
		false);
	CHECK(menuResult.consumed);
	CHECK_FALSE(menuModel.openMenu.has_value());
	// Outside press dismissed the menu and was consumed (no click-through to tabs).
	CHECK(menuResult.activated == std::nullopt);
}

TEST_CASE("menu bar editor integration narrow resize and framebuffer scale") {
	using Scalpel::LayoutTabStrip;
	using Scalpel::TabStripHeight;
	using Scalpel::TabStripModel;
	using Scalpel::TabStripPainter;
	using Scalpel::TabStripTab;

	const int menuH = MenuBarHeight();
	const int stripH = TabStripHeight();
	const int inset = menuH + stripH;
	ApplicationEditor editor(360, 140);
	editor.LoadInitialBuffer("scale\n");
	editor.SetTopChromeInset(inset);
	(void)editor.TakeFrameDamage();

	MenuBarPainter menuPainter;
	TabStripPainter stripPainter;
	MenuBarModel menuModel = ClosedBar();
	TabStripModel stripModel;
	TabStripTab tab;
	tab.id = 1;
	tab.label = "n";
	tab.active = true;
	stripModel.tabs.push_back(tab);

	editor.SetPermanentChromePainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			const MenuBarLayout menuLayout =
				LayoutMenuBar(width, height, menuModel);
			CHECK(menuLayout.bar.right == width);
			CHECK(menuLayout.bar.bottom == menuH);
			menuPainter.PaintBar(surface, menuLayout, menuModel);
			const auto stripLayout =
				LayoutTabStrip(width, stripModel, menuH);
			CHECK(stripLayout.strip.top == menuH);
			CHECK(stripLayout.strip.right == width);
			stripPainter.Paint(surface, stripLayout, stripModel);
		});

	// Narrow resize clamps heading and strip layout into the frame.
	editor.Resize(80, 100);
	CHECK(editor.TopChromeInset() == inset);
	CHECK(editor.TopChromeRectangle() ==
		PRectangle::FromInts(0, 0, 80, inset));
	(void)editor.TakeFrameDamage();
	editor.RenderFrame({PRectangle::FromInts(0, 0, 80, 100)});
	{
		const MenuBarLayout narrow =
			LayoutMenuBar(80, 100, menuModel);
		CHECK(narrow.bar.right == 80);
		REQUIRE(narrow.headings.size() == 3);
		CHECK(narrow.headings[0].bounds.left == 0);
		CHECK(narrow.headings[0].bounds.right <= 80);
		const auto stripLayout = LayoutTabStrip(80, stripModel, menuH);
		CHECK(stripLayout.strip.right == 80);
		CHECK(stripLayout.addButton.right == 80);
	}
	const auto narrowPixels = editor.FramePixels();
	REQUIRE(narrowPixels.size() == 80U * 100U * 4U);
	const Rgba narrowBar = Sample(narrowPixels, 80, 4, menuH / 2);
	const Rgba narrowStrip = Sample(narrowPixels, 80, 4, menuH + stripH / 2);
	CHECK(narrowBar.a == 0xff);
	CHECK(narrowStrip.a == 0xff);

	// Framebuffer scale keeps logical chrome coordinates.
	editor.Resize(200, 120);
	editor.SetFrameBufferSize(400, 240);
	(void)editor.TakeFrameDamage();
	editor.RenderFrame({PRectangle::FromInts(0, 0, 200, 120)});
	CHECK(editor.BufferWidth() == 400);
	CHECK(editor.BufferHeight() == 240);
	const auto scaled = editor.FramePixels();
	REQUIRE(scaled.size() == 200U * 120U * 4U);
	const Rgba barMid = Sample(scaled, 200, 20, menuH / 2);
	const Rgba stripMid = Sample(scaled, 200, 20, menuH + stripH / 2);
	const Rgba client = Sample(scaled, 200, 20, inset + 10);
	CHECK(Differs(barMid, client));
	CHECK(Differs(stripMid, client));
	CHECK(barMid.a == 0xff);
}

// Combined menu-bar product path: keyboard open, pointer selection, edit-state
// refresh, portal Open, dirty Quit cancel, then editor input again. Mirrors the
// shell sequence main drives rather than isolated unit cases.

namespace {

bool HasShellRequest(const std::vector<DocumentShellRequest> &requests,
	DocumentShellRequest want) {
	for (const DocumentShellRequest request : requests) {
		if (request == want) {
			return true;
		}
	}
	return false;
}

}

TEST_CASE("menu bar workflow keyboard open pointer edit portal quit cancel") {
	using Scalpel::UnsavedChoice;
	using Scalpel::UnsavedPending;

	ApplicationEditor editor(400, 260);
	editor.LoadInitialBuffer("workflow\n");
	DocumentWorkspace workspace(editor);
	MenuBarModel model;
	(void)UpdateMenuBarActionState(model, editor);
	const int width = editor.FrameWidth();
	const int height = editor.FrameHeight();

	// Keyboard open: F10 opens File and focuses the first actionable item.
	const MenuBarKeyboardResult openFile =
		HandleMenuBarKeyboard(model, MakeKey(Keys::Menu));
	CHECK(openFile.consumed);
	REQUIRE(model.openMenu.has_value());
	CHECK(*model.openMenu == ApplicationMenu::File);
	REQUIRE(model.focusedItem.has_value());
	CHECK(*model.focusedItem == ApplicationAction::NewTab);

	// Switch to Edit with Right; enablement still reflects an empty history.
	const MenuBarKeyboardResult toEdit =
		HandleMenuBarKeyboard(model, MakeKey(Keys::Right));
	CHECK(toEdit.consumed);
	REQUIRE(model.openMenu.has_value());
	CHECK(*model.openMenu == ApplicationMenu::Edit);
	CHECK_FALSE(model.IsEnabled(ApplicationAction::Undo));
	// Escape closes so typing reaches the editor.
	const MenuBarKeyboardResult closeEdit =
		HandleMenuBarKeyboard(model, MakeKey(Keys::Escape));
	CHECK(closeEdit.consumed);
	CHECK_FALSE(model.openMenu.has_value());

	// Edit-state changes: typing dirties the buffer and enables Undo.
	// LoadInitialBuffer leaves the caret at 0, so the character inserts first.
	editor.HandleKeyboardInput(
		{static_cast<Keys>(0), KeyMod::Norm, "!", 1, true});
	REQUIRE(editor.Modified());
	REQUIRE(editor.Text() == "!workflow\n");
	(void)UpdateMenuBarActionState(model, editor);
	CHECK(model.IsEnabled(ApplicationAction::Undo));
	CHECK(model.IsEnabled(ApplicationAction::SelectAll));

	// Pointer selection on File → Open: press/release activates and closes.
	const Point fileHeading = Center(LayoutMenuBar(width, height, model)
		.headings[0]
		.bounds);
	const MenuBarPointerResult openHeading = HandleMenuBarPointer(model,
		LayoutMenuBar(width, height, model),
		MakePointer(PointerAction::Press, fileHeading.x, fileHeading.y, 0),
		false);
	CHECK(openHeading.consumed);
	REQUIRE(model.openMenu.has_value());
	CHECK(*model.openMenu == ApplicationMenu::File);
	MenuBarLayout fileLayout = LayoutMenuBar(width, height, model);
	const auto *openItem = FindItem(fileLayout, ApplicationAction::Open);
	REQUIRE(openItem);
	const Point openPt = Center(openItem->row);
	(void)HandleMenuBarPointer(model, fileLayout,
		MakePointer(PointerAction::Press, openPt.x, openPt.y, 0), false);
	const MenuBarPointerResult openRelease = HandleMenuBarPointer(model,
		fileLayout,
		MakePointer(PointerAction::Release, openPt.x, openPt.y, 0), false);
	REQUIRE(openRelease.activated.has_value());
	CHECK(*openRelease.activated == ApplicationAction::Open);
	CHECK_FALSE(model.openMenu.has_value());

	// Portal action: Open closes the menu first, then the workspace requests
	// the dialog; accepting a path creates a sibling tab.
	DispatchApplicationAction(openRelease.activated->action, workspace, editor);
	CHECK(HasShellRequest(workspace.TakeRequests(),
		DocumentShellRequest::ShowOpen));
	char openPattern[] = "/tmp/scalpel-menu-wf-XXXXXX";
	const int openFd = mkstemp(openPattern);
	REQUIRE(openFd >= 0);
	const std::string openPath = openPattern;
	const char openBody[] = "from portal\n";
	REQUIRE(write(openFd, openBody, sizeof(openBody) - 1) ==
		static_cast<ssize_t>(sizeof(openBody) - 1));
	REQUIRE(close(openFd) == 0);
	workspace.RegisterOpenRequest(501);
	workspace.HandlePortalResult(501, true, {openPath});
	CHECK(workspace.TabCount() == 2);
	CHECK(workspace.Path() == openPath);
	CHECK(editor.Text() == "from portal\n");
	CHECK_FALSE(editor.Modified());
	(void)workspace.TakeRequests();
	(void)std::remove(openPath.c_str());

	// Return to the dirty first tab; dirty Quit from the menu opens the card.
	const auto startup = workspace.Tabs().front().id;
	workspace.ActivateTab(startup);
	REQUIRE(editor.Modified());
	REQUIRE(editor.Text() == "!workflow\n");
	(void)workspace.TakeRequests();
	(void)UpdateMenuBarActionState(model, editor);
	const MenuBarKeyboardResult reopen =
		HandleMenuBarKeyboard(model, MakeLetter('F', KeyMod::Alt));
	CHECK(reopen.consumed);
	REQUIRE(model.openMenu.has_value());
	model.focusedItem = ApplicationAction::Quit;
	const MenuBarKeyboardResult quit =
		HandleMenuBarKeyboard(model, MakeKey(Keys::Return));
	REQUIRE(quit.activated.has_value());
	CHECK(*quit.activated == ApplicationAction::Quit);
	CHECK_FALSE(model.openMenu.has_value());
	DispatchApplicationAction(quit.activated->action, workspace, editor);
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK(HasShellRequest(workspace.TakeRequests(),
		DocumentShellRequest::PromptBegan));
	// Shell keeps the card on the overlay; menu stays closed.
	CHECK_FALSE(model.openMenu.has_value());

	// Dirty Quit cancellation leaves tabs and the dirty buffer intact.
	workspace.Choose(UnsavedChoice::Cancel);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.TabCount() == 2);
	CHECK(editor.Text() == "!workflow\n");
	CHECK(editor.Modified());
	CHECK_FALSE(HasShellRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));

	// Return to editor input: with the menu and prompt closed, typing works.
	// Reactivation restores the caret after the inserted character.
	editor.HandleKeyboardInput(
		{static_cast<Keys>(0), KeyMod::Norm, "?", 2, true});
	CHECK(editor.Text() == "!?workflow\n");
	CHECK(editor.Modified());
	// A closed menu does not consume pointer presses in the editor body.
	const MenuBarLayout closed = LayoutMenuBar(width, height, model);
	const MenuBarPointerResult body = HandleMenuBarPointer(model, closed,
		MakePointer(PointerAction::Press, 200, 200, 0), false);
	CHECK_FALSE(body.consumed);
	CHECK_FALSE(model.openMenu.has_value());
}

TEST_CASE("menu bar recent files workflow opens promotes reports and clears") {
	char firstPattern[] = "/tmp/scalpel-recent-first-XXXXXX";
	const int firstFd = mkstemp(firstPattern);
	REQUIRE(firstFd >= 0);
	const std::string firstPath = firstPattern;
	const char firstBody[] = "first recent\n";
	REQUIRE(write(firstFd, firstBody, sizeof(firstBody) - 1) ==
		static_cast<ssize_t>(sizeof(firstBody) - 1));
	REQUIRE(close(firstFd) == 0);

	char secondPattern[] = "/tmp/scalpel-recent-second-XXXXXX";
	const int secondFd = mkstemp(secondPattern);
	REQUIRE(secondFd >= 0);
	const std::string secondPath = secondPattern;
	const char secondBody[] = "second recent\n";
	REQUIRE(write(secondFd, secondBody, sizeof(secondBody) - 1) ==
		static_cast<ssize_t>(sizeof(secondBody) - 1));
	REQUIRE(close(secondFd) == 0);

	char statePattern[] = "/tmp/scalpel-recent-state-XXXXXX";
	const int stateFd = mkstemp(statePattern);
	REQUIRE(stateFd >= 0);
	REQUIRE(close(stateFd) == 0);
	const std::string statePath = statePattern;

	Scalpel::RecentFiles recent;
	(void)recent.Record(firstPath);
	(void)recent.Record(secondPath);
	REQUIRE(Scalpel::SaveRecentFiles(statePath, recent));
	MenuBarModel model;
	model.recentFiles = recent.Paths();

	ApplicationEditor editor(400, 260);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	// Alt+R opens Recent. Down chooses the older entry and Enter returns its
	// stable index without routing it through ApplicationAction.
	(void)HandleMenuBarKeyboard(model, MakeLetter('R', KeyMod::Alt));
	REQUIRE(model.focusedItem == MenuBarItemId::RecentFile(0));
	(void)HandleMenuBarKeyboard(model, MakeKey(Keys::Down));
	REQUIRE(model.focusedItem == MenuBarItemId::RecentFile(1));
	const MenuBarKeyboardResult activate =
		HandleMenuBarKeyboard(model, MakeKey(Keys::Return));
	REQUIRE(activate.activated.has_value());
	REQUIRE(activate.activated->kind == MenuBarItemKind::RecentFile);
	REQUIRE(activate.activated->recentIndex == 1);
	REQUIRE(workspace.OpenPath(
		model.recentFiles[activate.activated->recentIndex]));
	CHECK(editor.Text() == "first recent\n");

	const std::vector<std::string> used = workspace.TakeRecentPaths();
	REQUIRE(used.size() == 1);
	CHECK(recent.Record(used.front()));
	CHECK(recent.Paths().front() == firstPath);
	REQUIRE(Scalpel::SaveRecentFiles(statePath, recent));
	CHECK(Scalpel::LoadRecentFiles(statePath).Paths() == recent.Paths());

	// A stale path yields a user-presentable error and is not promoted.
	REQUIRE(std::remove(secondPath.c_str()) == 0);
	CHECK_FALSE(workspace.OpenPath(secondPath));
	const std::vector<Scalpel::DocumentFileError> errors =
		workspace.TakeFileErrors();
	REQUIRE(errors.size() == 1);
	CHECK(errors.front().operation == Scalpel::DocumentFileOperation::Open);
	CHECK(errors.front().path == secondPath);
	CHECK(workspace.TakeRecentPaths().empty());

	CHECK(recent.Clear());
	REQUIRE(Scalpel::SaveRecentFiles(statePath, recent));
	CHECK(Scalpel::LoadRecentFiles(statePath).Paths().empty());

	(void)std::remove(firstPath.c_str());
	(void)std::remove(statePath.c_str());
}
