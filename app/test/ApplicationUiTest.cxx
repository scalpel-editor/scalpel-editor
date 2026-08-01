#include "catch.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "ApplicationEditor.h"
#include "ApplicationTextInput.h"
#include "ApplicationUi.h"
#include "DocumentFile.h"
#include "DocumentWorkspace.h"
#include "FileErrorCard.h"
#include "MenuBar.h"
#include "RecentFiles.h"
#include "ScrollBar.h"
#include "TabStrip.h"
#include "UnsavedChangesCard.h"

using Scalpel::ApplicationEditor;
using Scalpel::ApplicationKeyboardOwner;
using Scalpel::ApplicationKeyboardResult;
using Scalpel::ApplicationLayout;
using Scalpel::ApplicationPointerCursor;
using Scalpel::ApplicationPointerOwner;
using Scalpel::ApplicationPointerResult;
using Scalpel::ApplicationShellEffect;
using Scalpel::ApplicationShellEffectKind;
using Scalpel::ApplicationTextInputBatch;
using Scalpel::ApplicationTextInputPreedit;
using Scalpel::ApplicationUi;
using Scalpel::BoundOverlay;
using Scalpel::BuildApplicationLayout;
using Scalpel::DocumentFileError;
using Scalpel::DocumentFileOperation;
using Scalpel::DocumentWorkspace;
using Scalpel::KeyboardInput;
using Scalpel::PointerAction;
using Scalpel::PointerInput;
using Scalpel::RecentFiles;
using Scalpel::UnsavedCardHit;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;
using Scintilla::KeyMod;
using Scintilla::Keys;

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

KeyboardInput MakeKey(Keys key, KeyMod modifiers = KeyMod::Norm,
	bool pressed = true) {
	KeyboardInput input;
	input.key = key;
	input.modifiers = modifiers;
	input.pressed = pressed;
	return input;
}

KeyboardInput MakeLetter(char upper, KeyMod modifiers = KeyMod::Norm) {
	return MakeKey(static_cast<Keys>(upper), modifiers);
}

KeyboardInput MakeText(std::string text, KeyMod modifiers = KeyMod::Norm) {
	KeyboardInput input;
	input.key = static_cast<Keys>(0);
	input.modifiers = modifiers;
	input.text = std::move(text);
	input.pressed = true;
	return input;
}

bool NonEmpty(const PRectangle &rc) {
	return rc.right > rc.left && rc.bottom > rc.top;
}

Point Center(const PRectangle &rc) {
	return Point((rc.left + rc.right) / 2.0, (rc.top + rc.bottom) / 2.0);
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

bool HasDamage(const std::vector<PRectangle> &damage,
	const PRectangle expected) {
	return std::find(damage.begin(), damage.end(), expected) != damage.end();
}

bool HasShellEffect(const std::vector<ApplicationShellEffect> &effects,
	ApplicationShellEffectKind want) {
	return std::any_of(effects.begin(), effects.end(),
		[want](const ApplicationShellEffect &effect) {
			return effect.kind == want;
		});
}

class TempFile {
public:
	explicit TempFile(std::string_view contents) {
		char pattern[] = "/tmp/scalpel-ui-shell-XXXXXX";
		const int fd = mkstemp(pattern);
		REQUIRE(fd >= 0);
		path = pattern;
		if (!contents.empty()) {
			const ssize_t written = write(fd, contents.data(), contents.size());
			REQUIRE(written == static_cast<ssize_t>(contents.size()));
		}
		REQUIRE(close(fd) == 0);
	}
	~TempFile() {
		if (!path.empty()) {
			(void)std::remove(path.c_str());
		}
	}
	TempFile(const TempFile &) = delete;
	TempFile &operator=(const TempFile &) = delete;

	std::string path;
};

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

	ui.AppendFileErrors({DocumentFileError{
		DocumentFileOperation::Open, "/missing.txt"}});
	ui.FileErrorPressHit() = true;
	ui.PromptPressHit() = UnsavedCardHit::Save;
	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;
	ui.StripModel().hoveredId = 7;
	ui.ScrollBars().dragging = true;

	CHECK(ui.FileErrorPressHit());
	REQUIRE(ui.PromptPressHit().has_value());
	CHECK(*ui.PromptPressHit() == UnsavedCardHit::Save);
	REQUIRE(ui.FileErrors().size() == 1);
	CHECK(ui.FileErrors().front().path == "/missing.txt");
	CHECK(ui.MenuModel().openMenu == Scalpel::ApplicationMenu::File);
	CHECK(ui.StripModel().hoveredId == 7);
	CHECK(ui.ScrollBars().dragging);
	// Composition decides the bound overlay; open menu loses to file error.
	CHECK(ui.SynchronizeComposition() == BoundOverlay::FileError);
	CHECK(ui.Overlay() == BoundOverlay::FileError);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
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
	// Force an overlay so both permanent-chrome and overlay paint entry points
	// run during one RenderFrame and share the retained snapshot.
	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;
	ui.BindPainters();
	REQUIRE(ui.Overlay() == BoundOverlay::Menu);
	const ApplicationLayout *chromeLayout = nullptr;
	const ApplicationLayout *overlayLayout = nullptr;
	editor.SetPermanentChromePainter(
		[&](Surface &surface, int, int) {
			chromeLayout = &ui.FrameLayout();
			ui.PaintPermanentChrome(surface);
		});
	editor.SetOverlayPainter(
		[&](Surface &surface, int, int) {
			overlayLayout = &ui.FrameLayout();
			ui.PaintActiveOverlay(surface);
		});
	(void)editor.TakeFrameDamage();
	editor.InvalidateFrame();
	ui.BeginFrameLayout();
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
	ui.AppendFileErrors({DocumentFileError{
		DocumentFileOperation::Open, "/missing.txt"}});
	// Dirty close would otherwise raise the unsaved card; file error still wins.
	DirtyBuffer(editor);
	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());
	(void)editor.TakeFrameDamage();

	const ApplicationLayout layout = ui.Layout();
	const Point onDismiss = Point::FromInts(
		static_cast<int>(layout.fileErrorCard.dismissButton.left + 2),
		static_cast<int>(layout.fileErrorCard.dismissButton.top + 2));
	const ApplicationPointerResult press = ui.HandlePointer(
		MakePointer(PointerAction::Press, onDismiss.x, onDismiss.y, 0));
	CHECK(press.consumed);
	CHECK(press.owner == ApplicationPointerOwner::FileError);
	CHECK(press.cursor == ApplicationPointerCursor::Arrow);
	CHECK(ui.FileErrorPressHit());

	const ApplicationPointerResult release = ui.HandlePointer(
		MakePointer(PointerAction::Release, onDismiss.x, onDismiss.y, 0));
	CHECK(release.consumed);
	CHECK(release.owner == ApplicationPointerOwner::FileError);
	CHECK(ui.FileErrors().empty());
	CHECK(HasDamage(editor.TakeFrameDamage(), PRectangle::FromInts(
		0, 0, editor.FrameWidth(), editor.FrameHeight())));
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

TEST_CASE("application UI file error clears an underlying prompt press") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	DirtyBuffer(editor);
	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());
	ui.NotifyPromptBegan();
	(void)workspace.TakeRequests();

	const ApplicationLayout layout = ui.Layout();
	const Point onCancel = Point::FromInts(
		static_cast<int>(layout.unsavedCard.cancelButton.left + 2),
		static_cast<int>(layout.unsavedCard.cancelButton.top + 2));
	(void)ui.HandlePointer(
		MakePointer(PointerAction::Press, onCancel.x, onCancel.y, 0));
	REQUIRE(ui.PromptPressHit() == UnsavedCardHit::Cancel);

	ui.AppendFileErrors({DocumentFileError{
		DocumentFileOperation::Open, "/missing.txt"}});
	CHECK_FALSE(ui.PromptPressHit().has_value());
	(void)ui.HandleKeyboard(MakeKey(Keys::Escape));
	REQUIRE(ui.FileErrors().empty());

	const ApplicationPointerResult release = ui.HandlePointer(
		MakePointer(PointerAction::Release, onCancel.x, onCancel.y, 0));
	CHECK(release.owner == ApplicationPointerOwner::UnsavedPrompt);
	CHECK(release.consumed);
	CHECK(workspace.PromptActive());
}

TEST_CASE("application UI pointer priority open menu owns strip and dismisses outside") {
	ApplicationEditor editor(400, 280);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	(void)editor.TakeFrameDamage();

	ApplicationLayout layout = ui.Layout();
	const Point onFile = Point::FromInts(
		static_cast<int>(layout.menu.headings[0].bounds.left + 4),
		static_cast<int>(layout.menu.headings[0].bounds.top + 4));
	const ApplicationPointerResult open = ui.HandlePointer(
		MakePointer(PointerAction::Press, onFile.x, onFile.y, 0));
	CHECK(open.consumed);
	CHECK(open.owner == ApplicationPointerOwner::Menu);
	REQUIRE(ui.MenuModel().openMenu == Scalpel::ApplicationMenu::File);
	CHECK(open.cursor == ApplicationPointerCursor::Arrow);
	CHECK(HasDamage(editor.TakeFrameDamage(), PRectangle::FromInts(
		0, 0, editor.FrameWidth(), editor.FrameHeight())));

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

TEST_CASE("application UI pointer priority menu item activation applies action") {
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
	CHECK_FALSE(ui.PointerOverChrome());
	CHECK(release.cursor == ApplicationPointerCursor::Editor);
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
	(void)editor.TakeFrameDamage();

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
	CHECK(editor.TakeFrameDamage().empty());
}

TEST_CASE("application UI keyboard routing file error owns over prompt") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	ui.AppendFileErrors({DocumentFileError{
		DocumentFileOperation::Open, "/missing.txt"}});
	DirtyBuffer(editor);
	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());
	CHECK(ui.ChromeOwnsInput());
	(void)editor.TakeFrameDamage();

	const ApplicationKeyboardResult dismiss =
		ui.HandleKeyboard(MakeKey(Keys::Escape));
	CHECK(dismiss.owner == ApplicationKeyboardOwner::FileError);
	CHECK(ui.FileErrors().empty());
	CHECK(workspace.PromptActive());
	CHECK(HasDamage(editor.TakeFrameDamage(), PRectangle::FromInts(
		0, 0, editor.FrameWidth(), editor.FrameHeight())));
	// Prompt still owns input after the file error is dismissed.
	CHECK(ui.ChromeOwnsInput());
	const ApplicationKeyboardResult cancel =
		ui.HandleKeyboard(MakeKey(Keys::Escape));
	CHECK(cancel.owner == ApplicationKeyboardOwner::UnsavedPrompt);
	CHECK_FALSE(workspace.PromptActive());
	CHECK_FALSE(ui.ChromeOwnsInput());
}

TEST_CASE("application UI keyboard routing unsaved prompt blocks menu and editor") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	DirtyBuffer(editor);
	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());
	ui.CardFocus() = 0;
	(void)editor.TakeFrameDamage();

	// Menu open accelerators must not run while the card owns input.
	const ApplicationKeyboardResult menuKey =
		ui.HandleKeyboard(MakeKey(Keys::Menu));
	CHECK(menuKey.owner == ApplicationKeyboardOwner::UnsavedPrompt);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());

	// Typing must not reach the buffer (dirty marker left only the earlier 'x').
	const ApplicationKeyboardResult letter =
		ui.HandleKeyboard(MakeLetter('Z'));
	CHECK(letter.owner == ApplicationKeyboardOwner::UnsavedPrompt);
	CHECK(editor.Text().find('Z') == std::string::npos);

	// Focus cycle and activate Cancel.
	const ApplicationKeyboardResult tab =
		ui.HandleKeyboard(MakeKey(Keys::Tab));
	CHECK(tab.owner == ApplicationKeyboardOwner::UnsavedPrompt);
	CHECK(ui.CardFocus() == 1);
	(void)ui.HandleKeyboard(MakeKey(Keys::Tab));
	CHECK(ui.CardFocus() == 2);
	const ApplicationKeyboardResult activate =
		ui.HandleKeyboard(MakeKey(Keys::Return));
	CHECK(activate.owner == ApplicationKeyboardOwner::UnsavedPrompt);
	CHECK_FALSE(workspace.PromptActive());
}

TEST_CASE("application UI keyboard routing menu accelerators and open menu") {
	ApplicationEditor editor(400, 280);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	const Scalpel::DocumentId first = editor.ActiveDocument();
	(void)editor.TakeFrameDamage();

	const ApplicationKeyboardResult open =
		ui.HandleKeyboard(MakeKey(Keys::Menu));
	CHECK(open.owner == ApplicationKeyboardOwner::Menu);
	REQUIRE(ui.MenuModel().openMenu == Scalpel::ApplicationMenu::File);
	CHECK(ui.ChromeOwnsInput());
	CHECK(HasDamage(editor.TakeFrameDamage(), PRectangle::FromInts(
		0, 0, editor.FrameWidth(), editor.FrameHeight())));

	// While open, editor shortcuts and typing do not reach the buffer.
	const std::string textBefore = editor.Text();
	const ApplicationKeyboardResult blocked =
		ui.HandleKeyboard(MakeLetter('Z'));
	CHECK(blocked.owner == ApplicationKeyboardOwner::Menu);
	CHECK(editor.Text() == textBefore);

	// Activate New Tab from the focused File item (first enabled item).
	const ApplicationKeyboardResult activate =
		ui.HandleKeyboard(MakeKey(Keys::Return));
	CHECK(activate.owner == ApplicationKeyboardOwner::Menu);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	CHECK(workspace.Tabs().size() == 2);
	CHECK(editor.ActiveDocument() != first);
}

TEST_CASE("application UI keyboard routing application shortcuts and editor") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	const Scalpel::DocumentId first = editor.ActiveDocument();

	ui.ScrollBars().dragging = true;
	const ApplicationKeyboardResult newTab =
		ui.HandleKeyboard(MakeLetter('N', KeyMod::Ctrl));
	CHECK(newTab.owner == ApplicationKeyboardOwner::ApplicationShortcut);
	CHECK(workspace.Tabs().size() == 2);
	CHECK(editor.ActiveDocument() != first);
	CHECK_FALSE(ui.ScrollBars().dragging);

	// Ctrl+Tab cycles without going through the File/Edit action table.
	ui.ScrollBars().dragging = true;
	const ApplicationKeyboardResult cycle =
		ui.HandleKeyboard(MakeKey(Keys::Tab, KeyMod::Ctrl));
	CHECK(cycle.owner == ApplicationKeyboardOwner::ApplicationShortcut);
	CHECK(workspace.ActiveTab() == first);
	CHECK_FALSE(ui.ScrollBars().dragging);

	// Text insertion uses the text field, not the key code alone.
	KeyboardInput insert;
	insert.key = static_cast<Keys>(0);
	insert.modifiers = KeyMod::Norm;
	insert.text = "q";
	insert.pressed = true;
	const ApplicationKeyboardResult typed = ui.HandleKeyboard(insert);
	CHECK(typed.owner == ApplicationKeyboardOwner::Editor);
	CHECK(editor.Text().find('q') != std::string::npos);
}

TEST_CASE("application UI focus loss closes menu cancels bars and clears press") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);

	ui.HandleFocus(true);

	// Seed chrome that must not survive surface focus loss.
	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;
	ui.MenuModel().focusedItem = Scalpel::ApplicationAction::Save;
	ui.ScrollBars().hover = Scalpel::ScrollBarHit::Thumb;
	ui.ScrollBars().pressed = Scalpel::ScrollBarHit::Thumb;
	ui.ScrollBars().dragging = true;
	ui.FileErrorPressHit() = true;
	ui.PromptPressHit() = UnsavedCardHit::Discard;
	(void)editor.TakeFrameDamage();

	ui.HandleFocus(false);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	CHECK_FALSE(ui.MenuModel().focusedItem.has_value());
	CHECK(ui.ScrollBars().hover == Scalpel::ScrollBarHit::None);
	CHECK(ui.ScrollBars().pressed == Scalpel::ScrollBarHit::None);
	CHECK_FALSE(ui.ScrollBars().dragging);
	CHECK_FALSE(ui.FileErrorPressHit());
	CHECK_FALSE(ui.PromptPressHit().has_value());
	CHECK(HasDamage(editor.TakeFrameDamage(), PRectangle::FromInts(
		0, 0, editor.FrameWidth(), editor.FrameHeight())));
}

TEST_CASE("application UI keyboard routing cancels IME when menu or card opens") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);

	ApplicationTextInputBatch preedit;
	preedit.preedit = ApplicationTextInputPreedit{"\xC3\xA9", 2, 2};
	editor.HandleTextInputBatch(preedit);
	CHECK(editor.Text().find("\xC3\xA9") != std::string::npos);
	CHECK(editor.ImeIndicatorAt(0) != 0);
	CHECK_FALSE(ui.ChromeOwnsInput());

	// Opening a menu cancels tentative IME; host must drop batches while open.
	const ApplicationKeyboardResult open =
		ui.HandleKeyboard(MakeKey(Keys::Menu));
	CHECK(open.owner == ApplicationKeyboardOwner::Menu);
	REQUIRE(ui.MenuModel().openMenu.has_value());
	CHECK(ui.ChromeOwnsInput());
	CHECK(editor.ImeIndicatorAt(0) == 0);
	CHECK(editor.Text().find("\xC3\xA9") == std::string::npos);

	// Escape closes the menu; chrome no longer owns input.
	(void)ui.HandleKeyboard(MakeKey(Keys::Escape));
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	CHECK_FALSE(ui.ChromeOwnsInput());

	// Opening a modal card cancels tentative IME the same way.
	editor.HandleKeyboardInput({Keys::Home, KeyMod::Norm, {}, 1, true});
	editor.HandleTextInputBatch(preedit);
	REQUIRE(editor.Text().find("\xC3\xA9") != std::string::npos);
	CHECK(editor.ImeIndicatorAt(0) != 0);
	ui.NotifyPromptBegan();
	CHECK(editor.ImeIndicatorAt(0) == 0);
	CHECK(editor.Text().find("\xC3\xA9") == std::string::npos);

	editor.HandleTextInputBatch(preedit);
	REQUIRE(editor.ImeIndicatorAt(0) != 0);
	ui.AppendFileErrors({DocumentFileError{
		DocumentFileOperation::Save, "/fail.txt"}});
	CHECK(ui.ChromeOwnsInput());
	CHECK(editor.ImeIndicatorAt(0) == 0);
	CHECK(editor.Text().find("\xC3\xA9") == std::string::npos);
}

TEST_CASE("application UI overlay priority file error outranks prompt and menu") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	ui.BindPainters();
	CHECK(ui.Overlay() == BoundOverlay::None);

	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;
	CHECK(ui.SynchronizeComposition() == BoundOverlay::Menu);
	CHECK(ui.Overlay() == BoundOverlay::Menu);

	DirtyBuffer(editor);
	workspace.RequestClose();
	ui.NotifyPromptBegan();
	REQUIRE(workspace.PromptActive());
	// Prompt wins over menu; menu model is closed so it cannot linger.
	CHECK(ui.SynchronizeComposition() == BoundOverlay::UnsavedChanges);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());

	// Re-open menu while the prompt is still active; composition still closes it.
	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::Edit;
	ui.AppendFileErrors({DocumentFileError{
		DocumentFileOperation::Open, "/missing.txt"}});
	CHECK(ui.SynchronizeComposition() == BoundOverlay::FileError);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	CHECK(workspace.PromptActive());
	REQUIRE_FALSE(ui.FileErrors().empty());
}

TEST_CASE("application UI painter callbacks end with UI lifetime") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;

	{
		ApplicationUi ui(editor, workspace, recent, "");
		SeedStrip(ui, editor);
		ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;
		ui.BindPainters();
		REQUIRE(ui.Overlay() == BoundOverlay::Menu);
	}

	// ApplicationEditor outlives ApplicationUi, so both callbacks must already
	// be unbound. Rendering without a retained UI frame layout must be safe.
	(void)editor.TakeFrameDamage();
	editor.InvalidateFrame();
	CHECK_NOTHROW(editor.RenderFrame());
}

TEST_CASE("application UI current pointer cursor covers asynchronous modals") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);

	CHECK(ui.CurrentPointerCursor() == ApplicationPointerCursor::Editor);
	const ApplicationPointerResult chrome = ui.HandlePointer(
		MakePointer(PointerAction::Move, 8.0, 8.0));
	REQUIRE(chrome.cursor == ApplicationPointerCursor::Arrow);
	CHECK(ui.CurrentPointerCursor() == ApplicationPointerCursor::Arrow);
	const ApplicationPointerResult client = ui.HandlePointer(
		MakePointer(PointerAction::Move, 20.0, 100.0));
	REQUIRE(client.cursor == ApplicationPointerCursor::Editor);
	CHECK(ui.CurrentPointerCursor() == ApplicationPointerCursor::Editor);

	// A shell outcome can open a card without another pointer event.
	ui.AppendFileErrors({DocumentFileError{
		DocumentFileOperation::Open, "/missing.txt"}});
	CHECK(ui.CurrentPointerCursor() == ApplicationPointerCursor::Arrow);
	const ApplicationPointerResult modalChrome = ui.HandlePointer(
		MakePointer(PointerAction::Move, 8.0, 8.0));
	REQUIRE(modalChrome.cursor == ApplicationPointerCursor::Arrow);
	(void)ui.HandleKeyboard(MakeKey(Keys::Escape));
	// Dismissing the modal restores the cursor for its underlying location.
	CHECK(ui.CurrentPointerCursor() == ApplicationPointerCursor::Arrow);
	(void)ui.HandlePointer(MakePointer(PointerAction::Move, 20.0, 100.0));
	CHECK(ui.CurrentPointerCursor() == ApplicationPointerCursor::Editor);

	DirtyBuffer(editor);
	workspace.RequestClose();
	ui.NotifyPromptBegan();
	REQUIRE(workspace.PromptActive());
	CHECK(ui.CurrentPointerCursor() == ApplicationPointerCursor::Arrow);
	(void)ui.HandleKeyboard(MakeKey(Keys::Escape));
	CHECK_FALSE(workspace.PromptActive());
	CHECK(ui.CurrentPointerCursor() == ApplicationPointerCursor::Editor);
}

TEST_CASE("application UI overlay priority invalidates full frame on change") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	ui.BindPainters();
	(void)editor.TakeFrameDamage();

	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;
	CHECK(ui.SynchronizeComposition() == BoundOverlay::Menu);
	const std::vector<PRectangle> menuDamage = editor.TakeFrameDamage();
	REQUIRE_FALSE(menuDamage.empty());
	CHECK(HasDamage(menuDamage,
		PRectangle::FromInts(0, 0, editor.FrameWidth(), editor.FrameHeight())));

	// Same overlay again must not queue another full-frame damage.
	CHECK(ui.SynchronizeComposition() == BoundOverlay::Menu);
	CHECK(editor.TakeFrameDamage().empty());

	ui.MenuModel().openMenu.reset();
	Scalpel::CloseMenuBar(ui.MenuModel());
	CHECK(ui.SynchronizeComposition() == BoundOverlay::None);
	const std::vector<PRectangle> clearDamage = editor.TakeFrameDamage();
	REQUIRE_FALSE(clearDamage.empty());
	CHECK(HasDamage(clearDamage,
		PRectangle::FromInts(0, 0, editor.FrameWidth(), editor.FrameHeight())));

	// File error activation path also damages the full frame.
	ui.AppendFileErrors({DocumentFileError{
		DocumentFileOperation::Save, "/fail.txt"}});
	// AppendFileErrors already invalidated; SynchronizeComposition does again
	// when the bound overlay changes to FileError.
	(void)editor.TakeFrameDamage();
	CHECK(ui.SynchronizeComposition() == BoundOverlay::FileError);
	const std::vector<PRectangle> errorDamage = editor.TakeFrameDamage();
	REQUIRE_FALSE(errorDamage.empty());
	CHECK(HasDamage(errorDamage,
		PRectangle::FromInts(0, 0, editor.FrameWidth(), editor.FrameHeight())));
}

TEST_CASE("application UI overlay priority paints through bound entry points") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;
	ui.BindPainters();
	REQUIRE(ui.Overlay() == BoundOverlay::Menu);

	(void)editor.TakeFrameDamage();
	editor.InvalidateFrame();
	ui.BeginFrameLayout();
	editor.RenderFrame();
	const ApplicationLayout &frameLayout = ui.FrameLayout();
	REQUIRE_FALSE(frameLayout.menu.bar.Empty());
	REQUIRE(frameLayout.menu.dropdownMenu.has_value());
	REQUIRE_FALSE(frameLayout.menu.dropdown.Empty());
	const int barX = static_cast<int>(frameLayout.menu.bar.left + 4);
	const int barY = static_cast<int>(frameLayout.menu.bar.top + 4);
	const int dropX = static_cast<int>(
		(frameLayout.menu.dropdown.left + frameLayout.menu.dropdown.right) / 2.0);
	const int dropY = static_cast<int>(
		(frameLayout.menu.dropdown.top + frameLayout.menu.dropdown.bottom) / 2.0);
	ui.EndFrameLayout();

	// Permanent chrome paints the menu bar; open File dropdown paints through
	// the overlay entry point into the client area.
	const std::vector<uint8_t> pixels = editor.FramePixels();
	const int width = editor.FrameWidth();
	const size_t barSample =
		(static_cast<size_t>(barY) * static_cast<size_t>(width) +
			static_cast<size_t>(barX)) *
		4U;
	CHECK(pixels[barSample + 3] == 0xff);
	CHECK_FALSE((pixels[barSample + 0] == 0x00 &&
		pixels[barSample + 1] == 0x00 &&
		pixels[barSample + 2] == 0x00));

	const size_t dropSample =
		(static_cast<size_t>(dropY) * static_cast<size_t>(width) +
			static_cast<size_t>(dropX)) *
		4U;
	CHECK(pixels[dropSample + 3] == 0xff);
	CHECK_FALSE((pixels[dropSample + 0] == 0x00 &&
		pixels[dropSample + 1] == 0x00 &&
		pixels[dropSample + 2] == 0x00));
	const uint8_t dropR = pixels[dropSample + 0];
	const uint8_t dropG = pixels[dropSample + 1];
	const uint8_t dropB = pixels[dropSample + 2];

	// Closing the menu clears the overlay so the dropdown panel cannot remain.
	Scalpel::CloseMenuBar(ui.MenuModel());
	CHECK(ui.SynchronizeComposition() == BoundOverlay::None);
	(void)editor.TakeFrameDamage();
	editor.InvalidateFrame();
	ui.BeginFrameLayout();
	editor.RenderFrame();
	ui.EndFrameLayout();
	const std::vector<uint8_t> cleared = editor.FramePixels();
	CHECK_FALSE((cleared[dropSample + 0] == dropR &&
		cleared[dropSample + 1] == dropG &&
		cleared[dropSample + 2] == dropB));
}

TEST_CASE("application UI shell effects open and save as dismiss the menu") {
	ApplicationEditor editor(320, 180);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);

	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;
	workspace.RequestOpen();
	const std::vector<ApplicationShellEffect> openEffects = ui.TakeShellEffects();
	REQUIRE(HasShellEffect(openEffects, ApplicationShellEffectKind::ShowOpen));
	REQUIRE(openEffects.size() == 1);
	CHECK(openEffects[0].dialogId.value != 0);
	CHECK(openEffects[0].documentPath.empty());
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	CHECK(ui.TakeShellEffects().empty());

	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;
	workspace.RequestSaveAs();
	const std::vector<ApplicationShellEffect> saveEffects = ui.TakeShellEffects();
	REQUIRE(HasShellEffect(saveEffects, ApplicationShellEffectKind::ShowSaveAs));
	REQUIRE(saveEffects.size() == 1);
	CHECK(saveEffects[0].dialogId != openEffects[0].dialogId);
	CHECK(saveEffects[0].documentPath.empty());
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
}

TEST_CASE("application UI shell effects accept close for a clean workspace") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("clean\n");
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");

	workspace.RequestClose();
	const std::vector<ApplicationShellEffect> effects = ui.TakeShellEffects();
	REQUIRE(effects.size() == 1);
	CHECK(effects[0].kind == ApplicationShellEffectKind::AcceptClose);
	CHECK(effects[0].dialogId.value == 0);
	CHECK(ui.TakeShellEffects().empty());
}

TEST_CASE("application UI shell effects apply prompt begin without accept close") {
	ApplicationEditor editor(320, 180);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);

	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;
	ui.CardFocus() = 2;
	DirtyBuffer(editor);
	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());

	const std::vector<ApplicationShellEffect> effects = ui.TakeShellEffects();
	CHECK_FALSE(HasShellEffect(
		effects, ApplicationShellEffectKind::AcceptClose));
	// PromptBegan is applied inside TakeShellEffects, not returned as a host effect.
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	CHECK(ui.CardFocus() == 0);
	CHECK(ui.CurrentPointerCursor() == ApplicationPointerCursor::Arrow);
}

TEST_CASE("application UI shell effects refresh tabs from workspace requests") {
	ApplicationEditor editor(320, 180);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	// Seed only the initial tab so NewTab must rewrite the strip.
	REQUIRE(ui.SynchronizeTabs());
	REQUIRE(ui.StripModel().tabs.size() == 1);

	workspace.NewTab();
	const std::vector<ApplicationShellEffect> effects = ui.TakeShellEffects();
	CHECK(effects.empty());
	CHECK(ui.StripModel().tabs.size() == 2);
	CHECK(ui.StripModel().tabs.back().active);
}

TEST_CASE("application UI shell effects record recent paths and file errors") {
	TempFile file("opened\n");
	ApplicationEditor editor(320, 180);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	(void)ui.SynchronizeTabs();

	REQUIRE(workspace.OpenPath(file.path));
	const std::vector<ApplicationShellEffect> openEffects = ui.TakeShellEffects();
	CHECK(openEffects.empty());
	REQUIRE(recent.Paths().size() == 1);
	CHECK(recent.Paths()[0] == file.path);
	REQUIRE(ui.MenuModel().recentFiles.size() == 1);
	CHECK(ui.MenuModel().recentFiles[0] == file.path);

	// A missing path surfaces a file-error effect and activates the queue.
	CHECK_FALSE(workspace.OpenPath("/tmp/scalpel-ui-shell-missing-path"));
	const std::vector<ApplicationShellEffect> errorEffects = ui.TakeShellEffects();
	CHECK(errorEffects.empty());
	REQUIRE_FALSE(ui.FileErrors().empty());
	CHECK(ui.FileErrors().front().operation == DocumentFileOperation::Open);
	CHECK(ui.FileErrors().front().path ==
		"/tmp/scalpel-ui-shell-missing-path");
}

TEST_CASE("application UI shell effects portal open result refreshes tabs") {
	TempFile file("portal-open\n");
	ApplicationEditor editor(320, 180);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	(void)ui.SynchronizeTabs();
	const auto initialId = workspace.ActiveTab();

	workspace.RequestOpen();
	const std::vector<ApplicationShellEffect> openEffects = ui.TakeShellEffects();
	REQUIRE(openEffects.size() == 1);
	REQUIRE(openEffects[0].kind == ApplicationShellEffectKind::ShowOpen);
	ui.NotifyDialogResult(openEffects[0].dialogId, true, {file.path});

	const std::vector<ApplicationShellEffect> effects = ui.TakeShellEffects();
	CHECK(effects.empty());
	CHECK(workspace.ActiveTab() != initialId);
	CHECK(workspace.Path() == file.path);
	CHECK(editor.Text() == "portal-open\n");
	REQUIRE(ui.StripModel().tabs.size() == 2);
}

TEST_CASE("application UI shell effects portal cancel leaves the buffer") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("unchanged\n");
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");

	workspace.RequestOpen();
	const std::vector<ApplicationShellEffect> effects = ui.TakeShellEffects();
	REQUIRE(effects.size() == 1);
	REQUIRE(effects[0].kind == ApplicationShellEffectKind::ShowOpen);
	ui.NotifyDialogResult(effects[0].dialogId, false, {});
	CHECK(ui.TakeShellEffects().empty());
	CHECK(workspace.TabCount() == 1);
	CHECK(editor.Text() == "unchanged\n");
	CHECK(recent.Paths().empty());
}

TEST_CASE("application UI shell effects dialog startup failure rejects a result") {
	TempFile file("must not open\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("unchanged\n");
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");

	workspace.RequestOpen();
	const std::vector<ApplicationShellEffect> effects = ui.TakeShellEffects();
	REQUIRE(effects.size() == 1);
	REQUIRE(effects[0].kind == ApplicationShellEffectKind::ShowOpen);
	ui.NotifyDialogFailed(effects[0].dialogId);

	// A transport result cannot revive an application intent whose startup
	// failed.
	ui.NotifyDialogResult(effects[0].dialogId, true, {file.path});
	CHECK(ui.TakeShellEffects().empty());
	CHECK(workspace.TabCount() == 1);
	CHECK(editor.Text() == "unchanged\n");
	CHECK(recent.Paths().empty());
}

TEST_CASE("application UI shell effects save as startup failure keeps prompt") {
	ApplicationEditor editor(320, 180);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);

	DirtyBuffer(editor);
	workspace.CloseTab(workspace.ActiveTab());
	REQUIRE(workspace.PromptActive());
	(void)ui.TakeShellEffects();

	workspace.Choose(Scalpel::UnsavedChoice::Save);
	REQUIRE(workspace.AwaitingSaveAs());
	const std::vector<ApplicationShellEffect> effects = ui.TakeShellEffects();
	REQUIRE(effects.size() == 1);
	REQUIRE(effects[0].kind == ApplicationShellEffectKind::ShowSaveAs);
	// Host failed before a request ID existed. The card stays, but the Save As
	// wait clears so the user can choose again.
	ui.NotifyDialogFailed(effects[0].dialogId);
	CHECK(workspace.PromptActive());
	CHECK_FALSE(workspace.AwaitingSaveAs());
	CHECK(ui.TakeShellEffects().empty());
}

TEST_CASE("application UI shell effects portal Save As continues dirty close") {
	TempFile file("");
	ApplicationEditor editor(320, 180);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	(void)ui.SynchronizeTabs();

	DirtyBuffer(editor);
	const std::string dirtyText = editor.Text();
	workspace.CloseTab(workspace.ActiveTab());
	REQUIRE(workspace.PromptActive());
	(void)ui.TakeShellEffects();

	workspace.Choose(Scalpel::UnsavedChoice::Save);
	const std::vector<ApplicationShellEffect> dialogEffects =
		ui.TakeShellEffects();
	REQUIRE(dialogEffects.size() == 1);
	REQUIRE(dialogEffects[0].kind ==
		ApplicationShellEffectKind::ShowSaveAs);
	ui.NotifyDialogResult(dialogEffects[0].dialogId, true, {file.path});

	const std::vector<ApplicationShellEffect> effects = ui.TakeShellEffects();
	CHECK(effects.empty());
	CHECK_FALSE(workspace.PromptActive());
	// Closing the last dirty tab replaces it with a fresh untitled.
	CHECK(workspace.Path().empty());
	CHECK_FALSE(editor.Modified());
	const auto read = Scalpel::ReadDocumentFile(file.path);
	REQUIRE(read.has_value());
	CHECK(*read == dirtyText);
}

TEST_CASE("application UI workflow menu portal edit dirty close cancel editor") {
	// Controller path without Wayland: menu Open → portal → edit → dirty
	// window close → cancel → editor typing again.
	TempFile file("workflow-open\n");
	ApplicationEditor editor(400, 280);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	REQUIRE(ui.SynchronizeTabs());
	ui.BindPainters();
	CHECK(ui.Overlay() == BoundOverlay::None);

	// Menu → Open (File heading, second item after New Tab).
	REQUIRE(ui.HandleKeyboard(MakeKey(Keys::Menu)).owner ==
		ApplicationKeyboardOwner::Menu);
	REQUIRE(ui.MenuModel().openMenu == Scalpel::ApplicationMenu::File);
	CHECK(ui.ChromeOwnsInput());
	CHECK(ui.SynchronizeComposition() == BoundOverlay::Menu);
	(void)ui.HandleKeyboard(MakeKey(Keys::Down));
	const ApplicationKeyboardResult openItem =
		ui.HandleKeyboard(MakeKey(Keys::Return));
	CHECK(openItem.owner == ApplicationKeyboardOwner::Menu);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());

	std::vector<ApplicationShellEffect> effects = ui.TakeShellEffects();
	REQUIRE(effects.size() == 1);
	REQUIRE(effects[0].kind == ApplicationShellEffectKind::ShowOpen);

	// Portal accept loads the file and refreshes tabs inside ApplicationUi.
	ui.ScrollBars().dragging = true;
	ui.NotifyDialogResult(effects[0].dialogId, true, {file.path});
	CHECK_FALSE(ui.ScrollBars().dragging);
	effects = ui.TakeShellEffects();
	CHECK(effects.empty());
	CHECK(workspace.Path() == file.path);
	CHECK(editor.Text() == "workflow-open\n");
	REQUIRE(ui.StripModel().tabs.size() == 2);
	CHECK(ui.SynchronizeComposition() == BoundOverlay::None);
	CHECK_FALSE(ui.ChromeOwnsInput());

	// Edit marks the buffer dirty and updates the strip marker.
	DirtyBuffer(editor);
	ui.SynchronizeDirtyTabs();
	REQUIRE(editor.Modified());
	bool activeDirty = false;
	for (const Scalpel::TabStripTab &tab : ui.StripModel().tabs) {
		if (tab.active) {
			activeDirty = tab.dirty;
			break;
		}
	}
	CHECK(activeDirty);

	// Dirty window close surfaces the unsaved card without accepting close.
	ui.ScrollBars().dragging = true;
	ui.RequestClose();
	CHECK_FALSE(ui.ScrollBars().dragging);
	effects = ui.TakeShellEffects();
	CHECK_FALSE(HasShellEffect(
		effects, ApplicationShellEffectKind::AcceptClose));
	REQUIRE(workspace.PromptActive());
	CHECK(ui.SynchronizeComposition() == BoundOverlay::UnsavedChanges);
	CHECK(ui.ChromeOwnsInput());
	CHECK(ui.CurrentPointerCursor() == ApplicationPointerCursor::Arrow);

	// Cancel returns ownership to the editor; the dirty buffer remains.
	const ApplicationKeyboardResult cancel =
		ui.HandleKeyboard(MakeKey(Keys::Escape));
	CHECK(cancel.owner == ApplicationKeyboardOwner::UnsavedPrompt);
	CHECK_FALSE(workspace.PromptActive());
	effects = ui.TakeShellEffects();
	CHECK(effects.empty());
	CHECK(ui.SynchronizeComposition() == BoundOverlay::None);
	CHECK_FALSE(ui.ChromeOwnsInput());
	CHECK(editor.Modified());

	// Editor input is live again after the modal path.
	const std::string textBefore = editor.Text();
	KeyboardInput insert;
	insert.key = static_cast<Keys>(0);
	insert.modifiers = KeyMod::Norm;
	insert.text = "z";
	insert.pressed = true;
	const ApplicationKeyboardResult typed = ui.HandleKeyboard(insert);
	CHECK(typed.owner == ApplicationKeyboardOwner::Editor);
	CHECK(editor.Text() != textBefore);
	CHECK(editor.Text().find('z') != std::string::npos);
}

TEST_CASE("application UI font menu keyboard pointer selection and resume input") {
	using Scalpel::ApplicationAction;
	using Scalpel::ApplicationMenu;
	using Scalpel::EditorFont;
	using Scalpel::MenuBarItemLayout;

	ApplicationEditor editor(400, 280);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	REQUIRE(editor.CurrentEditorFont() == EditorFont::System);
	REQUIRE(editor.StyleFontName(
		static_cast<int>(Scintilla::StylesCommon::Default)) == "system-ui");

	// Alt+T opens Font; the radio snapshot starts on System.
	const ApplicationKeyboardResult openFont = ui.HandleKeyboard(
		MakeLetter('T', KeyMod::Alt));
	CHECK(openFont.owner == ApplicationKeyboardOwner::Menu);
	REQUIRE(ui.MenuModel().openMenu == ApplicationMenu::Font);
	CHECK(ui.MenuModel().selectedFontAction == ApplicationAction::FontSystem);

	// Keyboard: move to Serif and activate.
	(void)ui.HandleKeyboard(MakeKey(Keys::Down));
	REQUIRE(ui.MenuModel().focusedItem.has_value());
	CHECK(*ui.MenuModel().focusedItem == ApplicationAction::FontSerif);
	const ApplicationKeyboardResult chooseSerif =
		ui.HandleKeyboard(MakeKey(Keys::Return));
	CHECK(chooseSerif.owner == ApplicationKeyboardOwner::Menu);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	CHECK(editor.CurrentEditorFont() == EditorFont::Serif);
	CHECK(editor.StyleFontName(
		static_cast<int>(Scintilla::StylesCommon::Default)) == "serif");
	CHECK(editor.StyleFontName(
		static_cast<int>(Scintilla::StylesCommon::LineNumber)) == "monospace");

	// Re-open Font; the checked row follows the editor face.
	(void)ui.HandleKeyboard(MakeLetter('T', KeyMod::Alt));
	REQUIRE(ui.MenuModel().openMenu == ApplicationMenu::Font);
	CHECK(ui.MenuModel().selectedFontAction == ApplicationAction::FontSerif);
	{
		const ApplicationLayout layout = ui.Layout();
		const MenuBarItemLayout *serifRow = nullptr;
		const MenuBarItemLayout *systemRow = nullptr;
		for (const MenuBarItemLayout &item : layout.menu.items) {
			if (item.action == ApplicationAction::FontSerif) {
				serifRow = &item;
			}
			if (item.action == ApplicationAction::FontSystem) {
				systemRow = &item;
			}
		}
		REQUIRE(serifRow != nullptr);
		REQUIRE(systemRow != nullptr);
		CHECK(serifRow->selected);
		CHECK_FALSE(systemRow->selected);
	}

	// Pointer: choose Monospace from the open dropdown.
	ApplicationLayout layout = ui.Layout();
	const MenuBarItemLayout *monoRow = nullptr;
	for (const MenuBarItemLayout &item : layout.menu.items) {
		if (item.action == ApplicationAction::FontMonospace) {
			monoRow = &item;
			break;
		}
	}
	REQUIRE(monoRow != nullptr);
	const Point onMono = Point::FromInts(
		static_cast<int>(monoRow->row.left + 4),
		static_cast<int>(monoRow->row.top + 4));
	(void)ui.HandlePointer(
		MakePointer(PointerAction::Press, onMono.x, onMono.y, 0));
	const ApplicationPointerResult activate = ui.HandlePointer(
		MakePointer(PointerAction::Release, onMono.x, onMono.y, 0));
	CHECK(activate.consumed);
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	CHECK(editor.CurrentEditorFont() == EditorFont::Monospace);
	CHECK(editor.StyleFontName(
		static_cast<int>(Scintilla::StylesCommon::Default)) == "monospace");
	CHECK(editor.StyleFontName(
		static_cast<int>(Scintilla::StylesCommon::LineNumber)) == "monospace");

	// After close, typing reaches the editor again.
	const std::string before = editor.Text();
	editor.HandleKeyboardInput(
		{Keys::End, KeyMod::Ctrl, {}, 3, true});
	const ApplicationKeyboardResult typeChar = ui.HandleKeyboard(
		{static_cast<Keys>(0), KeyMod::Norm, "z", 4, true});
	CHECK(typeChar.owner == ApplicationKeyboardOwner::Editor);
	CHECK(editor.Text() == before + "z");
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
}

TEST_CASE("application UI workflow frame size and exit cleanup") {
	ApplicationEditor editor(320, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	REQUIRE(ui.SynchronizeTabs());

	ui.ScrollBars().dragging = true;
	ui.ScrollBars().hover = Scalpel::ScrollBarHit::Thumb;
	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::File;
	editor.Resize(480, 260);
	ui.HandleFrameSizeChange();
	CHECK_FALSE(ui.ScrollBars().dragging);
	CHECK(ui.ScrollBars().hover == Scalpel::ScrollBarHit::None);
	// Open menu survives resize; full-frame invalidation covers the panel.
	REQUIRE(ui.MenuModel().openMenu == Scalpel::ApplicationMenu::File);
	CHECK(HasDamage(editor.TakeFrameDamage(), PRectangle::FromInts(
		0, 0, editor.FrameWidth(), editor.FrameHeight())));

	ui.ScrollBars().dragging = true;
	ui.PrepareForExit();
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());
	CHECK_FALSE(ui.ScrollBars().dragging);

	// Open-menu enablement refresh when a clipboard offer arrives.
	ui.MenuModel().openMenu = Scalpel::ApplicationMenu::Edit;
	(void)Scalpel::UpdateMenuBarActionState(ui.MenuModel(), editor);
	const bool pasteBefore = ui.MenuModel().IsEnabled(
		Scalpel::ApplicationAction::Paste);
	editor.SetClipboardPasteAvailable(!pasteBefore);
	(void)editor.TakeFrameDamage();
	ui.RefreshOpenMenuActionState();
	CHECK(ui.MenuModel().IsEnabled(Scalpel::ApplicationAction::Paste) !=
		pasteBefore);
	CHECK(HasDamage(editor.TakeFrameDamage(), PRectangle::FromInts(
		0, 0, editor.FrameWidth(), editor.FrameHeight())));
}

TEST_CASE("application UI find bar opens focuses and closes with inset") {
	ApplicationEditor editor(400, 240);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	const int baseInset = Scalpel::MenuBarHeight() + Scalpel::TabStripHeight();
	CHECK(editor.TopChromeInset() == baseInset);
	CHECK_FALSE(ui.FindBarVisible());

	const ApplicationKeyboardResult open = ui.HandleKeyboard(
		MakeLetter('F', KeyMod::Ctrl));
	CHECK(open.owner == ApplicationKeyboardOwner::ApplicationShortcut);
	CHECK(ui.FindBarVisible());
	CHECK(ui.FindBarFocused());
	CHECK(editor.TopChromeInset() == baseInset + Scalpel::FindBarHeight());
	const ApplicationLayout openLayout = ui.Layout();
	CHECK(NonEmpty(openLayout.find.band));
	CHECK(openLayout.find.band.top ==
		Scalpel::MenuBarHeight() + Scalpel::TabStripHeight());
	CHECK(openLayout.client.top == editor.TopChromeInset());

	// Escape closes the bar and restores the base inset.
	const ApplicationKeyboardResult close = ui.HandleKeyboard(
		MakeKey(Keys::Escape));
	CHECK(close.owner == ApplicationKeyboardOwner::FindBar);
	CHECK_FALSE(ui.FindBarVisible());
	CHECK(editor.TopChromeInset() == baseInset);
}

TEST_CASE("application UI find bar seeds query and searches incrementally") {
	ApplicationEditor editor(400, 240);
	PrepareChromeEditor(editor);
	editor.LoadInitialBuffer("alpha beta alpha");
	editor.SetSel(0, 5); // "alpha"
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);

	ui.OpenFindBar();
	CHECK(ui.FindModel().query == "alpha");
	CHECK(editor.GetSelectionStart() == 0);
	CHECK(editor.GetSelectionEnd() == 5);

	// Type to replace the selected query.
	(void)ui.HandleKeyboard(MakeText("beta"));
	CHECK(ui.FindModel().query == "beta");
	CHECK(editor.GetSelectionStart() ==
		static_cast<Scintilla::Position>(editor.Text().find("beta")));
	CHECK(ui.FindModel().status == Scalpel::FindBarStatus::None);

	// Next wraps from the only match.
	(void)ui.HandleKeyboard(MakeKey(Keys::Return));
	CHECK(ui.FindModel().status == Scalpel::FindBarStatus::Wrapped);
	CHECK(editor.GetSelectionStart() ==
		static_cast<Scintilla::Position>(editor.Text().find("beta")));
}

TEST_CASE("application UI find bar next previous and not-found status") {
	ApplicationEditor editor(400, 240);
	PrepareChromeEditor(editor);
	editor.LoadInitialBuffer("one two one");
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	ui.OpenFindBar();
	// Clear seeded empty selection; type the query.
	(void)ui.HandleKeyboard(MakeText("one"));
	CHECK(editor.GetSelectionStart() == 0);

	(void)ui.HandleKeyboard(MakeKey(Keys::Return));
	CHECK(editor.GetSelectionStart() == 8);
	CHECK(ui.FindModel().status == Scalpel::FindBarStatus::None);

	(void)ui.HandleKeyboard(MakeKey(Keys::Return));
	CHECK(editor.GetSelectionStart() == 0);
	CHECK(ui.FindModel().status == Scalpel::FindBarStatus::Wrapped);

	(void)ui.HandleKeyboard(MakeKey(Keys::Return, KeyMod::Shift));
	CHECK(editor.GetSelectionStart() == 8);
	CHECK(ui.FindModel().status == Scalpel::FindBarStatus::Wrapped);

	// Replace query with a missing needle.
	ui.FindModel().SelectAll();
	(void)ui.HandleKeyboard(MakeText("zzz"));
	CHECK(ui.FindModel().status == Scalpel::FindBarStatus::NoMatches);
}

TEST_CASE("application UI find bar pointer close and editor focus transfer") {
	ApplicationEditor editor(400, 240);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	ui.OpenFindBar();
	const ApplicationLayout layout = ui.Layout();
	const Point close = Center(layout.find.closeButton);

	const ApplicationPointerResult press = ui.HandlePointer(
		MakePointer(PointerAction::Press, close.x, close.y, 0));
	CHECK(press.consumed);
	CHECK(press.owner == ApplicationPointerOwner::PermanentChrome);
	const ApplicationPointerResult release = ui.HandlePointer(
		MakePointer(PointerAction::Release, close.x, close.y, 0));
	CHECK(release.consumed);
	CHECK_FALSE(ui.FindBarVisible());

	ui.OpenFindBar();
	CHECK(ui.FindBarFocused());
	const ApplicationLayout openLayout = ui.Layout();
	const Point client = Center(openLayout.client);
	const ApplicationPointerResult editorPress = ui.HandlePointer(
		MakePointer(PointerAction::Press, client.x, client.y, 0));
	CHECK_FALSE(editorPress.consumed);
	CHECK(ui.FindBarVisible());
	CHECK_FALSE(ui.FindBarFocused());
	// Ctrl+F focuses again without closing.
	(void)ui.HandleKeyboard(MakeLetter('F', KeyMod::Ctrl));
	CHECK(ui.FindBarFocused());
}

TEST_CASE("application UI find bar defers to menu and modal priority") {
	ApplicationEditor editor(400, 240);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	ui.OpenFindBar();
	CHECK(ui.FindBarFocused());

	// Open the File menu; find field loses focus but stays visible.
	(void)ui.HandleKeyboard(MakeLetter('F', KeyMod::Alt));
	CHECK(ui.MenuModel().openMenu.has_value());
	CHECK(*ui.MenuModel().openMenu == Scalpel::ApplicationMenu::File);
	CHECK(ui.FindBarVisible());
	CHECK_FALSE(ui.FindBarFocused());

	// Dismiss menu with Escape.
	(void)ui.HandleKeyboard(MakeKey(Keys::Escape));
	CHECK_FALSE(ui.MenuModel().openMenu.has_value());

	ui.FindModel().SetFocused(true);
	// File error owns keyboard over the find field.
	ui.AppendFileErrors({DocumentFileError{
		DocumentFileOperation::Open, "/tmp/x"}});
	const ApplicationKeyboardResult err = ui.HandleKeyboard(
		MakeKey(Keys::Escape));
	CHECK(err.owner == ApplicationKeyboardOwner::FileError);
	CHECK_FALSE(ui.FindBarFocused());
	CHECK(ui.FindBarVisible());
}

TEST_CASE("application UI find bar keeps query across tab switches") {
	ApplicationEditor editor(400, 240);
	PrepareChromeEditor(editor);
	editor.LoadInitialBuffer("first document has needle");
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	(void)ui.SynchronizeTabs(true);
	ui.OpenFindBar();
	(void)ui.HandleKeyboard(MakeText("needle"));
	CHECK(ui.FindModel().query == "needle");

	workspace.NewTab();
	(void)ui.TakeShellEffects();
	(void)ui.SynchronizeTabs(true);
	editor.LoadInitialBuffer("second document has needle too");
	// Query is process-lifetime; reopening focuses and searches the new buffer.
	CHECK(ui.FindBarVisible());
	CHECK(ui.FindModel().query == "needle");
	ui.OpenFindBar();
	CHECK(editor.Text().find("needle") != std::string::npos);
	CHECK(editor.GetSelectionStart() ==
		static_cast<Scintilla::Position>(editor.Text().find("needle")));
}

TEST_CASE("application UI find bar paints in permanent chrome") {
	ApplicationEditor editor(400, 200);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	ui.BindPainters();
	ui.OpenFindBar();
	ui.FindModel().SetStatus(Scalpel::FindBarStatus::NoMatches);
	ui.BeginFrameLayout();
	editor.RenderFrame();
	ui.EndFrameLayout();
	const auto pixels = editor.FramePixels();
	REQUIRE_FALSE(pixels.empty());
	const ApplicationLayout layout = ui.Layout();
	const int midY = static_cast<int>(
		(layout.find.band.top + layout.find.band.bottom) / 2);
	const size_t offset =
		(static_cast<size_t>(midY) * static_cast<size_t>(editor.FrameWidth()) +
			4U) *
		4U;
	REQUIRE(offset + 3 < pixels.size());
	// Opaque chrome (non-zero alpha).
	CHECK(pixels[offset + 3] == 255);
}

TEST_CASE("application UI find bar text input commits preedit and cancels") {
	ApplicationEditor editor(400, 240);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	ui.OpenFindBar();
	// Clear the seeded empty selection by typing nothing; replace with IME.
	ui.FindModel().SelectAll();

	ApplicationTextInputBatch preedit;
	preedit.preedit = ApplicationTextInputPreedit{"\xC3\xA9", 2, 2};
	ui.HandleTextInputBatch(preedit);
	REQUIRE(ui.FindModel().preedit.has_value());

	ApplicationTextInputBatch commit;
	commit.commit = "ok";
	ui.HandleTextInputBatch(commit);
	CHECK(ui.FindModel().query == "ok");
	CHECK_FALSE(ui.FindModel().preedit.has_value());

	ApplicationTextInputBatch again;
	again.preedit = ApplicationTextInputPreedit{"x", 1, 1};
	ui.HandleTextInputBatch(again);
	REQUIRE(ui.FindModel().preedit.has_value());

	// Menu open cancels find preedit via ChromeOwnsInput path.
	(void)ui.HandleKeyboard(MakeLetter('F', KeyMod::Alt));
	ApplicationTextInputBatch dropped;
	dropped.preedit = ApplicationTextInputPreedit{"y", 1, 1};
	ui.HandleTextInputBatch(dropped);
	CHECK_FALSE(ui.FindModel().preedit.has_value());
	CHECK(ui.FindModel().query == "ok");
}

TEST_CASE("application UI find bar text input state reports field caret") {
	ApplicationEditor editor(400, 240);
	PrepareChromeEditor(editor);
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	ui.OpenFindBar();
	(void)ui.HandleKeyboard(MakeText("abc"));
	auto state = ui.TakeTextInputState();
	REQUIRE(state.has_value());
	REQUIRE(state->surroundingText.has_value());
	CHECK(*state->surroundingText == "abc");
	CHECK(state->cursor == 3);
	CHECK(state->cursorRectangle.height > 0);
	CHECK(state->cursorRectangle.y >=
		Scalpel::MenuBarHeight() + Scalpel::TabStripHeight());
	CHECK_FALSE(ui.TakeTextInputState().has_value());
}

TEST_CASE("application UI find bar text input uses editor when unfocused") {
	ApplicationEditor editor(400, 240);
	PrepareChromeEditor(editor);
	editor.LoadInitialBuffer("doc");
	DocumentWorkspace workspace(editor);
	RecentFiles recent;
	ApplicationUi ui(editor, workspace, recent, "");
	SeedStrip(ui, editor);
	ui.OpenFindBar();
	// Blur field by pressing in the client.
	const ApplicationLayout layout = ui.Layout();
	const Point client = Center(layout.client);
	(void)ui.HandlePointer(MakePointer(PointerAction::Press, client.x, client.y, 0));
	CHECK(ui.FindBarVisible());
	CHECK_FALSE(ui.FindBarFocused());

	ApplicationTextInputBatch commit;
	commit.commit = "Z";
	ui.HandleTextInputBatch(commit);
	const bool editorHasZ = editor.Text().find('Z') != std::string::npos;
	const bool queryHasZ = ui.FindModel().query.find('Z') != std::string::npos;
	CHECK(editorHasZ);
	CHECK_FALSE(queryHasZ);
}
