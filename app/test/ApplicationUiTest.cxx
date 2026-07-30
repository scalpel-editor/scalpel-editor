#include "catch.hpp"

#include <string>

#include "ApplicationEditor.h"
#include "ApplicationUi.h"
#include "DocumentWorkspace.h"
#include "FileErrorCard.h"
#include "MenuBar.h"
#include "RecentFiles.h"
#include "ScrollBar.h"
#include "TabStrip.h"
#include "UnsavedChangesCard.h"

using Scalpel::ApplicationEditor;
using Scalpel::ApplicationLayout;
using Scalpel::ApplicationPointerCursor;
using Scalpel::ApplicationPointerOwner;
using Scalpel::ApplicationPointerResult;
using Scalpel::ApplicationUi;
using Scalpel::BoundOverlay;
using Scalpel::BuildApplicationLayout;
using Scalpel::DocumentFileError;
using Scalpel::DocumentFileOperation;
using Scalpel::DocumentWorkspace;
using Scalpel::PointerAction;
using Scalpel::PointerInput;
using Scalpel::RecentFiles;
using Scalpel::UnsavedCardHit;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;

namespace {

PointerInput MakePointer(PointerAction action, double x, double y,
	int button = -1) {
	PointerInput input;
	input.action = action;
	input.x = x;
	input.y = y;
	input.button = button;
	return input;
}

void PrepareChromeEditor(ApplicationEditor &editor) {
	editor.LoadInitialBuffer("line one\nline two\nline three\nline four\n");
	editor.SetWrapMode(Scintilla::Wrap::None);
	const int inset =
		Scalpel::MenuBarHeight() + Scalpel::TabStripHeight();
	editor.SetTopChromeInset(inset);
	editor.RenderFrame();
}

void SeedStrip(ApplicationUi &ui, ApplicationEditor &editor) {
	ui.StripModel().tabs.push_back(Scalpel::TabStripTab{
		editor.ActiveDocument(), "main", false, true});
}

void DirtyBuffer(ApplicationEditor &editor) {
	editor.HandleKeyboardInput({Scintilla::Keys::End, Scintilla::KeyMod::Norm,
		{}, 1, true});
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>(0),
		Scintilla::KeyMod::Norm, "x", 2, true});
	REQUIRE(editor.Modified());
}

}

TEST_CASE("application UI state owns chrome and overlay defaults") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("text\n");
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");

	CHECK(&ui.Editor() == &editor);
	CHECK(&ui.Workspace() == &workspace);
	CHECK(&ui.Recent() == &recent);
	CHECK(ui.RecentStatePath().empty());

	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	CHECK(ui.MenuModel().recentFiles.empty());
	CHECK(ui.StripModel().tabs.empty());
	CHECK(ui.StripModel().hoveredId == 0);
	CHECK_FALSE(ui.StripModel().closeHovered);
	CHECK(ui.StripModel().scrollOffset == 0);
	CHECK_FALSE(ui.ScrollBars().dragging);
	CHECK(ui.ScrollBars().hover == Scalpel::ScrollBarHit::None);
	CHECK(ui.ScrollBars().pressed == Scalpel::ScrollBarHit::None);
	CHECK(ui.CardFocus() == 0);
	CHECK(ui.FileErrors().empty());
	CHECK_FALSE(ui.PointerOverChrome());
	CHECK_FALSE(ui.FileErrorPressHit());
	CHECK_FALSE(ui.PromptPressHit().has_value());
	CHECK(ui.Overlay() == BoundOverlay::None);
	CHECK(ui.LastActiveDocument() == editor.ActiveDocument());
}

TEST_CASE("application UI state seeds recent paths into the menu model") {
	RecentFiles recent;
	REQUIRE(recent.Record("/tmp/scalpel-ui-recent.txt"));
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	ApplicationUi ui(editor, workspace, recent, "/tmp/scalpel-ui-state");

	REQUIRE(ui.MenuModel().recentFiles.size() == 1);
	CHECK(ui.MenuModel().recentFiles[0] == recent.Paths()[0]);
	CHECK(ui.RecentStatePath() == "/tmp/scalpel-ui-state");
}

TEST_CASE("application UI state mutates owned fields in place") {
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");

	ui.CardFocus() = 2;
	CHECK(ui.CardFocus() == 2);

	ui.PointerOverChrome() = true;
	ui.FileErrorPressHit() = true;
	ui.PromptPressHit() = UnsavedCardHit::Save;
	ui.SetOverlay(BoundOverlay::FileError);
	ui.LastActiveDocument() = 42;
	ui.FileErrors().push_back(DocumentFileError{
		DocumentFileOperation::Open, "/missing.txt"});
	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;
	ui.StripModel().hoveredId = 7;
	ui.ScrollBars().dragging = true;

	CHECK(ui.PointerOverChrome());
	CHECK(ui.FileErrorPressHit());
	REQUIRE(ui.PromptPressHit().has_value());
	CHECK(*ui.PromptPressHit() == UnsavedCardHit::Save);
	CHECK(ui.Overlay() == BoundOverlay::FileError);
	CHECK(ui.LastActiveDocument() == 42);
	REQUIRE(ui.FileErrors().size() == 1);
	CHECK(ui.FileErrors().front().path == "/missing.txt");
	CHECK(ui.MenuModel().openMenu == Scalpel::ApplicationMenu::File);
	CHECK(ui.StripModel().hoveredId == 7);
	CHECK(ui.ScrollBars().dragging);
}

TEST_CASE("application UI layout snapshot matches component layouts and editor client") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("line one\nline two\nline three\n");
	editor.SetWrapMode(Scintilla::Wrap::None);
	const int inset =
		Scalpel::MenuBarHeight() + Scalpel::TabStripHeight();
	editor.SetTopChromeInset(inset);
	editor.RenderFrame();
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	ui.StripModel().tabs.push_back(Scalpel::TabStripTab{
		editor.ActiveDocument(), "main", false, true});
	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;

	const ApplicationLayout layout = ui.Layout();
	CHECK(layout.frameWidth == editor.FrameWidth());
	CHECK(layout.frameHeight == editor.FrameHeight());
	CHECK(layout.topChromeInset == inset);
	CHECK(layout.client == editor.EditorClientRectangle());

	const ApplicationLayout expected = BuildApplicationLayout(
		editor.FrameWidth(), editor.FrameHeight(), inset, ui.MenuModel(),
		ui.StripModel(), editor.Scrollbars(), editor.EditorClientRectangle());
	CHECK(layout.menu.bar == expected.menu.bar);
	CHECK(layout.menu.dropdown == expected.menu.dropdown);
	CHECK(layout.menu.headings.size() == expected.menu.headings.size());
	CHECK(layout.tabs.strip == expected.tabs.strip);
	CHECK(layout.tabs.addButton == expected.tabs.addButton);
	CHECK(layout.tabs.tabs.size() == 1);
	CHECK(layout.scrollBars.vertical.track ==
		editor.VerticalScrollBarRectangle());
	CHECK(layout.scrollBars.horizontal.track ==
		editor.HorizontalScrollBarRectangle());
	CHECK(layout.scrollBars.junction == editor.JunctionRectangle());
	CHECK(layout.unsavedCard.card ==
		Scalpel::LayoutUnsavedChangesCard(320, 180).card);
	CHECK(layout.fileErrorCard.dismissButton ==
		Scalpel::LayoutFileErrorCard(320, 180).dismissButton);

	// Component free functions agree with the snapshot for the same inputs.
	const Scalpel::MenuBarLayout menuOnly =
		Scalpel::LayoutMenuBar(320, 180, ui.MenuModel());
	CHECK(layout.menu.bar == menuOnly.bar);
	CHECK(layout.menu.dropdown == menuOnly.dropdown);
	const Scalpel::TabStripLayout tabsOnly =
		Scalpel::LayoutTabStrip(320, ui.StripModel(), Scalpel::MenuBarHeight());
	CHECK(layout.tabs.strip == tabsOnly.strip);
	const Scalpel::ScrollBarLayout barsOnly = Scalpel::LayoutScrollBars(320, 180,
		inset, editor.Scrollbars().vertical, editor.Scrollbars().horizontal);
	CHECK(layout.scrollBars.vertical.track == barsOnly.vertical.track);
	CHECK(layout.scrollBars.horizontal.track == barsOnly.horizontal.track);
}

TEST_CASE("application UI layout keeps editor-owned client when bars hide") {
	ApplicationEditor editor(240, 140);
	editor.LoadInitialBuffer("wrap me\n");
	const int inset =
		Scalpel::MenuBarHeight() + Scalpel::TabStripHeight();
	editor.SetTopChromeInset(inset);
	editor.SetWrapMode(Scintilla::Wrap::Word);
	editor.RenderFrame();
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");

	const ApplicationLayout layout = ui.Layout();
	CHECK(layout.client == editor.EditorClientRectangle());
	CHECK_FALSE(editor.Scrollbars().horizontal.visible);
	CHECK(layout.scrollBars.horizontal.track.Empty());
	CHECK(layout.scrollBars.junction.Empty());
	// Client bottom reaches the frame when the horizontal bar is gone.
	CHECK(layout.client.bottom == layout.frameHeight);
	CHECK(layout.client.top == inset);
}

TEST_CASE("application UI layout hit testing and paint share one snapshot") {
	using Scintilla::Internal::ColourRGBA;
	using Scintilla::Internal::Fill;
	using Scintilla::Internal::Point;
	using Scintilla::Internal::Surface;

	ApplicationEditor editor(300, 200);
	editor.LoadInitialBuffer("body\n");
	editor.SetWrapMode(Scintilla::Wrap::None);
	const int inset =
		Scalpel::MenuBarHeight() + Scalpel::TabStripHeight();
	editor.SetTopChromeInset(inset);
	editor.RenderFrame();
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	ui.StripModel().tabs.push_back(Scalpel::TabStripTab{
		editor.ActiveDocument(), "doc", false, true});

	const ApplicationLayout layout = ui.Layout();
	const Point onMenu = Point::FromInts(
		static_cast<int>(layout.menu.headings[0].bounds.left + 4),
		static_cast<int>(layout.menu.headings[0].bounds.top + 4));
	const Point onTab = Point::FromInts(
		static_cast<int>(layout.tabs.tabs[0].bounds.left + 8),
		static_cast<int>(layout.tabs.tabs[0].bounds.top + 4));
	const Point onVBar = Point::FromInts(
		static_cast<int>(layout.scrollBars.vertical.track.left + 1),
		static_cast<int>(layout.scrollBars.vertical.track.top + 8));
	const Point onClient = Point::FromInts(
		static_cast<int>(layout.client.left + 4),
		static_cast<int>(layout.client.top + 4));

	CHECK(Scalpel::HitTestMenuBar(layout.menu, onMenu).kind ==
		Scalpel::MenuBarHit::Heading);
	CHECK(Scalpel::HitTestTabStrip(layout.tabs, onTab).kind ==
		Scalpel::TabStripHit::Tab);
	CHECK(Scalpel::HitTestScrollBars(layout.scrollBars, onVBar).hit !=
		Scalpel::ScrollBarHit::None);
	CHECK(Scalpel::HitTestMenuBar(layout.menu, onClient).kind ==
		Scalpel::MenuBarHit::None);
	CHECK(Scalpel::HitTestTabStrip(layout.tabs, onClient).kind ==
		Scalpel::TabStripHit::None);
	CHECK(Scalpel::HitTestScrollBars(layout.scrollBars, onClient).hit ==
		Scalpel::ScrollBarHit::None);

	CHECK_THROWS_WITH(ui.FrameLayout(),
		"ApplicationUi::FrameLayout requires BeginFrameLayout");
	ui.BeginFrameLayout();
	const ApplicationLayout *chromeLayout = nullptr;
	const ApplicationLayout *overlayLayout = nullptr;
	editor.SetPermanentChromePainter(
		[&](Surface &surface, int, int) {
			chromeLayout = &ui.FrameLayout();
			surface.FillRectangle(chromeLayout->menu.bar,
				Fill(ColourRGBA(0x20, 0x20, 0x20, 0xff)));
			surface.FillRectangle(chromeLayout->tabs.strip,
				Fill(ColourRGBA(0x30, 0x30, 0x30, 0xff)));
			Scalpel::PaintScrollBars(surface, chromeLayout->scrollBars, {});
		});
	editor.SetOverlayPainter(
		[&](Surface &surface, int, int) {
			overlayLayout = &ui.FrameLayout();
			surface.FillRectangle(overlayLayout->unsavedCard.card,
				Fill(ColourRGBA(0x40, 0x40, 0x40, 0xff)));
		});
	(void)editor.TakeFrameDamage();
	editor.InvalidateFrame();
	editor.RenderFrame();
	REQUIRE(chromeLayout != nullptr);
	REQUIRE(overlayLayout != nullptr);
	CHECK(chromeLayout == overlayLayout);
	CHECK(chromeLayout->frameWidth == layout.frameWidth);
	CHECK(chromeLayout->frameHeight == layout.frameHeight);
	CHECK(chromeLayout->menu.bar == layout.menu.bar);
	CHECK(chromeLayout->tabs.strip == layout.tabs.strip);
	CHECK(chromeLayout->scrollBars.vertical.track ==
		layout.scrollBars.vertical.track);
	CHECK(chromeLayout->client == layout.client);
	ui.EndFrameLayout();
	CHECK_THROWS_WITH(ui.FrameLayout(),
		"ApplicationUi::FrameLayout requires BeginFrameLayout");
}

TEST_CASE("application UI pointer priority file error owns over prompt and chrome") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	ui.FileErrors().push_back(DocumentFileError{
		DocumentFileOperation::Open, "/missing.txt"});
	// Dirty close would otherwise raise the unsaved card; file error still wins.
	DirtyBuffer(editor);
	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());

	const ApplicationLayout layout = ui.Layout();
	const Point onDismiss = Point::FromInts(
		static_cast<int>(layout.fileErrorCard.dismissButton.left + 2),
		static_cast<int>(layout.fileErrorCard.dismissButton.top + 2));
	const ApplicationPointerResult press = ui.HandlePointer(
		MakePointer(PointerAction::Press, onDismiss.x, onDismiss.y, 0));
	CHECK(press.consumed);
	CHECK(press.owner == ApplicationPointerOwner::FileError);
	CHECK(press.cursor == ApplicationPointerCursor::Arrow);
	CHECK_FALSE(press.activated.has_value());
	CHECK(ui.FileErrorPressHit());

	const ApplicationPointerResult release = ui.HandlePointer(
		MakePointer(PointerAction::Release, onDismiss.x, onDismiss.y, 0));
	CHECK(release.consumed);
	CHECK(release.owner == ApplicationPointerOwner::FileError);
	CHECK(release.damage.frame);
	CHECK(ui.FileErrors().empty());
	// Prompt remains until the host drains it; pointer still never reaches chrome.
	CHECK(workspace.PromptActive());
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
}

TEST_CASE("application UI pointer priority unsaved prompt blocks menu and editor") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	DirtyBuffer(editor);
	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());

	const ApplicationLayout layout = ui.Layout();
	REQUIRE_FALSE(layout.menu.headings.empty());
	const Point onHeading = Point::FromInts(
		static_cast<int>(layout.menu.headings[0].bounds.left + 4),
		static_cast<int>(layout.menu.headings[0].bounds.top + 4));
	const ApplicationPointerResult heading = ui.HandlePointer(
		MakePointer(PointerAction::Press, onHeading.x, onHeading.y, 0));
	CHECK(heading.consumed);
	CHECK(heading.owner == ApplicationPointerOwner::UnsavedPrompt);
	CHECK(heading.cursor == ApplicationPointerCursor::Arrow);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());

	const Point onClient = Point::FromInts(
		static_cast<int>(layout.client.left + 8),
		static_cast<int>(layout.client.top + 8));
	const ApplicationPointerResult client = ui.HandlePointer(
		MakePointer(PointerAction::Press, onClient.x, onClient.y, 0));
	CHECK(client.consumed);
	CHECK(client.owner == ApplicationPointerOwner::UnsavedPrompt);
	CHECK_FALSE(editor.WindowState().mouseCaptured);

	const Point onCancel = Point::FromInts(
		static_cast<int>(layout.unsavedCard.cancelButton.left + 2),
		static_cast<int>(layout.unsavedCard.cancelButton.top + 2));
	(void)ui.HandlePointer(
		MakePointer(PointerAction::Press, onCancel.x, onCancel.y, 0));
	const ApplicationPointerResult cancel = ui.HandlePointer(
		MakePointer(PointerAction::Release, onCancel.x, onCancel.y, 0));
	CHECK(cancel.consumed);
	CHECK(cancel.owner == ApplicationPointerOwner::UnsavedPrompt);
	CHECK_FALSE(workspace.PromptActive());
}

TEST_CASE("application UI pointer priority open menu owns strip and dismisses outside") {
	ApplicationEditor editor(400, 280);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);

	ApplicationLayout layout = ui.Layout();
	const Point onFile = Point::FromInts(
		static_cast<int>(layout.menu.headings[0].bounds.left + 4),
		static_cast<int>(layout.menu.headings[0].bounds.top + 4));
	const ApplicationPointerResult open = ui.HandlePointer(
		MakePointer(PointerAction::Press, onFile.x, onFile.y, 0));
	CHECK(open.consumed);
	CHECK(open.owner == ApplicationPointerOwner::Menu);
	REQUIRE(ui.MenuModel().openMenu == Scalpel::ApplicationMenu::File);
	CHECK(open.damage.frame);
	CHECK(open.cursor == ApplicationPointerCursor::Arrow);

	ui.StripModel().hoveredId = editor.ActiveDocument();
	ui.StripModel().closeHovered = true;
	layout = ui.Layout();
	REQUIRE_FALSE(layout.tabs.tabs.empty());
	const Point onTab = Point::FromInts(
		static_cast<int>(layout.tabs.tabs[0].bounds.left + 8),
		static_cast<int>(layout.tabs.tabs[0].bounds.top + 4));
	// Prefer a strip point outside the File dropdown so geometry hits the strip.
	const double stripX = layout.menu.dropdown.right + 24.0;
	const double stripY = Scalpel::MenuBarHeight() +
		(layout.tabs.strip.Height() / 2.0);
	const Point stripPoint =
		stripX < layout.frameWidth ? Point(stripX, stripY) : onTab;
	const ApplicationPointerResult stripPress = ui.HandlePointer(
		MakePointer(PointerAction::Press, stripPoint.x, stripPoint.y, 0));
	CHECK(stripPress.consumed);
	CHECK(stripPress.owner == ApplicationPointerOwner::Menu);
	// Outside press dismissed the menu; strip hover must not stick under it.
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	CHECK(ui.StripModel().hoveredId == 0);
	CHECK_FALSE(ui.StripModel().closeHovered);
}

TEST_CASE("application UI pointer priority menu item activation returns exact result") {
	ApplicationEditor editor(400, 280);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	const Scalpel::DocumentId first = editor.ActiveDocument();

	ApplicationLayout layout = ui.Layout();
	const Point onFile = Point::FromInts(
		static_cast<int>(layout.menu.headings[0].bounds.left + 4),
		static_cast<int>(layout.menu.headings[0].bounds.top + 4));
	(void)ui.HandlePointer(
		MakePointer(PointerAction::Press, onFile.x, onFile.y, 0));
	REQUIRE(ui.MenuModel().openMenu == Scalpel::ApplicationMenu::File);

	layout = ui.Layout();
	const Scalpel::MenuBarItemLayout *newTab = nullptr;
	for (const auto &item : layout.menu.items) {
		if (item.item == Scalpel::ApplicationAction::NewTab) {
			newTab = &item;
			break;
		}
	}
	REQUIRE(newTab != nullptr);
	const Point onItem = Point::FromInts(
		static_cast<int>(newTab->row.left + 4),
		static_cast<int>(newTab->row.top + 4));
	(void)ui.HandlePointer(
		MakePointer(PointerAction::Press, onItem.x, onItem.y, 0));
	const ApplicationPointerResult activate = ui.HandlePointer(
		MakePointer(PointerAction::Release, onItem.x, onItem.y, 0));
	CHECK(activate.consumed);
	REQUIRE(activate.activated.has_value());
	CHECK(*activate.activated == Scalpel::ApplicationAction::NewTab);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	CHECK(workspace.Tabs().size() == 2);
	CHECK(editor.ActiveDocument() != first);
}

TEST_CASE("application UI pointer priority scrollbar drag owns motion over client") {
	ApplicationEditor editor(320, 200);
	std::string text;
	for (int line = 0; line < 60; line++) {
		text += "scrollbar priority line with horizontal width\n";
	}
	editor.LoadInitialBuffer(text);
	editor.SetWrapMode(Scintilla::Wrap::None);
	const int inset =
		Scalpel::MenuBarHeight() + Scalpel::TabStripHeight();
	editor.SetTopChromeInset(inset);
	editor.RenderFrame();
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);

	ApplicationLayout layout = ui.Layout();
	REQUIRE(layout.scrollBars.vertical.enabled);
	const double trackX =
		(layout.scrollBars.vertical.track.left +
			layout.scrollBars.vertical.track.right) / 2.0;
	const double thumbY =
		(layout.scrollBars.vertical.thumb.top +
			layout.scrollBars.vertical.thumb.bottom) / 2.0;
	const ApplicationPointerResult press = ui.HandlePointer(
		MakePointer(PointerAction::Press, trackX, thumbY, 0));
	CHECK(press.consumed);
	CHECK(press.owner == ApplicationPointerOwner::PermanentChrome);
	CHECK(ui.ScrollBars().dragging);
	CHECK(ui.PointerOverChrome());
	CHECK(press.cursor == ApplicationPointerCursor::Arrow);

	const ApplicationPointerResult drag = ui.HandlePointer(
		MakePointer(PointerAction::Move, trackX,
			layout.scrollBars.vertical.track.bottom + 40.0));
	CHECK(drag.consumed);
	CHECK(drag.owner == ApplicationPointerOwner::ScrollBarDrag);
	CHECK(editor.Scrollbars().vertical.position ==
		editor.Scrollbars().vertical.upperBound);

	const Point onClient = Point::FromInts(
		static_cast<int>(layout.client.left + 8),
		static_cast<int>(layout.client.top + 8));
	const ApplicationPointerResult overClient = ui.HandlePointer(
		MakePointer(PointerAction::Move, onClient.x, onClient.y));
	CHECK(overClient.consumed);
	CHECK(overClient.owner == ApplicationPointerOwner::ScrollBarDrag);
	CHECK_FALSE(editor.WindowState().mouseCaptured);

	const ApplicationPointerResult release = ui.HandlePointer(
		MakePointer(PointerAction::Release, onClient.x, onClient.y, 0));
	CHECK(release.consumed);
	CHECK(release.owner == ApplicationPointerOwner::ScrollBarDrag);
	CHECK_FALSE(ui.ScrollBars().dragging);
}

TEST_CASE("application UI pointer priority editor capture bypasses chrome") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);

	const ApplicationLayout layout = ui.Layout();
	const Point onClient = Point::FromInts(
		static_cast<int>(layout.client.left + 12),
		static_cast<int>(layout.client.top + 10));
	const ApplicationPointerResult clientPress = ui.HandlePointer(
		MakePointer(PointerAction::Press, onClient.x, onClient.y, 0));
	CHECK_FALSE(clientPress.consumed);
	CHECK(clientPress.owner == ApplicationPointerOwner::Editor);
	editor.HandlePointerInput(
		MakePointer(PointerAction::Press, onClient.x, onClient.y, 0));
	REQUIRE(editor.WindowState().mouseCaptured);

	const Point onHeading = Point::FromInts(
		static_cast<int>(layout.menu.headings[0].bounds.left + 4),
		static_cast<int>(layout.menu.headings[0].bounds.top + 4));
	const ApplicationPointerResult overChrome = ui.HandlePointer(
		MakePointer(PointerAction::Move, onHeading.x, onHeading.y));
	CHECK_FALSE(overChrome.consumed);
	CHECK(overChrome.owner == ApplicationPointerOwner::EditorCapture);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	// Hover over the bar band still requests the arrow while capture is held.
	CHECK(ui.PointerOverChrome());
	CHECK(overChrome.cursor == ApplicationPointerCursor::Arrow);

	editor.HandlePointerInput(
		MakePointer(PointerAction::Release, onHeading.x, onHeading.y, 0));
	CHECK_FALSE(editor.WindowState().mouseCaptured);
}

TEST_CASE("application UI pointer priority opening menu cancels scrollbar drag") {
	ApplicationEditor editor(320, 200);
	std::string text;
	for (int line = 0; line < 50; line++) {
		text += "cancel drag line with width for bars\n";
	}
	editor.LoadInitialBuffer(text);
	editor.SetWrapMode(Scintilla::Wrap::None);
	const int inset =
		Scalpel::MenuBarHeight() + Scalpel::TabStripHeight();
	editor.SetTopChromeInset(inset);
	editor.RenderFrame();
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);

	// Residual bar press/hover is cleared when a menu opens (not only active drag).
	ui.ScrollBars().hover = Scalpel::ScrollBarHit::Thumb;
	ui.ScrollBars().pressed = Scalpel::ScrollBarHit::Thumb;
	const ApplicationLayout layout = ui.Layout();
	const Point onFile = Point::FromInts(
		static_cast<int>(layout.menu.headings[0].bounds.left + 4),
		static_cast<int>(layout.menu.headings[0].bounds.top + 4));
	const ApplicationPointerResult open = ui.HandlePointer(
		MakePointer(PointerAction::Press, onFile.x, onFile.y, 0));
	CHECK(open.consumed);
	REQUIRE(ui.MenuModel().openMenu == Scalpel::ApplicationMenu::File);
	CHECK(ui.ScrollBars().pressed == Scalpel::ScrollBarHit::None);
	CHECK(ui.ScrollBars().hover == Scalpel::ScrollBarHit::None);
	CHECK(open.damage.scrollBars);

	// Active drag still owns heading presses until release; menu cannot steal them.
	(void)ui.HandlePointer(
		MakePointer(PointerAction::Press, onFile.x, onFile.y, 0));
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	ApplicationLayout bars = ui.Layout();
	REQUIRE(bars.scrollBars.vertical.enabled);
	const double trackX =
		(bars.scrollBars.vertical.track.left +
			bars.scrollBars.vertical.track.right) / 2.0;
	const double thumbY =
		(bars.scrollBars.vertical.thumb.top +
			bars.scrollBars.vertical.thumb.bottom) / 2.0;
	(void)ui.HandlePointer(MakePointer(PointerAction::Press, trackX, thumbY, 0));
	REQUIRE(ui.ScrollBars().dragging);
	const ApplicationPointerResult dragOwns = ui.HandlePointer(
		MakePointer(PointerAction::Press, onFile.x, onFile.y, 0));
	CHECK(dragOwns.owner == ApplicationPointerOwner::ScrollBarDrag);
	CHECK(ui.ScrollBars().dragging);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
}

TEST_CASE("application UI pointer priority client delivers to editor") {
	ApplicationEditor editor(300, 180);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);

	const ApplicationLayout layout = ui.Layout();
	const Point onClient = Point::FromInts(
		static_cast<int>(layout.client.left + 6),
		static_cast<int>(layout.client.top + 6));
	const ApplicationPointerResult move = ui.HandlePointer(
		MakePointer(PointerAction::Move, onClient.x, onClient.y));
	CHECK_FALSE(move.consumed);
	CHECK(move.owner == ApplicationPointerOwner::Editor);
	CHECK(move.cursor == ApplicationPointerCursor::Editor);
	CHECK_FALSE(ui.PointerOverChrome());
	CHECK_FALSE(move.damage.frame);
	CHECK_FALSE(move.damage.topChrome);
	CHECK_FALSE(move.damage.scrollBars);
	CHECK_FALSE(move.damage.client);
}

