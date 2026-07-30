#include "catch.hpp"

#include <optional>

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
using Scalpel::ApplicationUi;
using Scalpel::BoundOverlay;
using Scalpel::BuildApplicationLayout;
using Scalpel::DocumentFileError;
using Scalpel::DocumentFileOperation;
using Scalpel::DocumentWorkspace;
using Scalpel::RecentFiles;
using Scalpel::UnsavedCardHit;
using Scintilla::Internal::PRectangle;

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

	int chromePaints = 0;
	std::optional<ApplicationLayout> painted;
	editor.SetPermanentChromePainter(
		[&](Surface &surface, int, int) {
			++chromePaints;
			painted = ui.Layout();
			surface.FillRectangle(painted->menu.bar,
				Fill(ColourRGBA(0x20, 0x20, 0x20, 0xff)));
			surface.FillRectangle(painted->tabs.strip,
				Fill(ColourRGBA(0x30, 0x30, 0x30, 0xff)));
			Scalpel::PaintScrollBars(surface, painted->scrollBars, {});
		});
	(void)editor.TakeFrameDamage();
	editor.InvalidateFrame();
	editor.RenderFrame();
	CHECK(chromePaints == 1);
	REQUIRE(painted.has_value());
	CHECK(painted->frameWidth == layout.frameWidth);
	CHECK(painted->frameHeight == layout.frameHeight);
	CHECK(painted->menu.bar == layout.menu.bar);
	CHECK(painted->tabs.strip == layout.tabs.strip);
	CHECK(painted->scrollBars.vertical.track == layout.scrollBars.vertical.track);
	CHECK(painted->client == layout.client);
}
