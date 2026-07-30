#include "catch.hpp"

#include "ApplicationEditor.h"
#include "ApplicationUi.h"
#include "DocumentWorkspace.h"
#include "RecentFiles.h"

using Scalpel::ApplicationEditor;
using Scalpel::ApplicationUi;
using Scalpel::BoundOverlay;
using Scalpel::DocumentFileError;
using Scalpel::DocumentFileOperation;
using Scalpel::DocumentWorkspace;
using Scalpel::RecentFiles;
using Scalpel::UnsavedCardHit;

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
