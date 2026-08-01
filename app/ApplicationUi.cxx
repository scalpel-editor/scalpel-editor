#include "ApplicationUi.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "ApplicationAction.h"
#include "ApplicationEditor.h"
#include "DocumentFile.h"
#include "RecentFiles.h"
#include "UniConversion.h"

namespace Scalpel {

namespace {

void DismissOpenMenu(MenuBarModel &menuModel, ApplicationEditor &editor) {
	if (!menuModel.openMenu.has_value()) {
		return;
	}
	CloseMenuBar(menuModel);
	editor.InvalidateFrame();
}

void PersistRecentFiles(const std::string &statePath,
	const RecentFiles &recent) {
	if (statePath.empty()) {
		return;
	}
	if (!SaveRecentFiles(statePath, recent)) {
		std::cerr << "scalpel-editor: failed to write recent-file state\n";
	}
}

void SyncRecentMenu(const RecentFiles &recent, MenuBarModel &menuModel,
	ApplicationEditor &editor) {
	if (menuModel.recentFiles == recent.Paths()) {
		return;
	}
	menuModel.recentFiles = recent.Paths();
	editor.InvalidateTopChrome();
	// Recent rows are identified by index. If the MRU list rewrites while
	// Recent is open, focus, hover, and press would still point at the old
	// index. Dismiss so the next open rebuilds from the new list.
	if (menuModel.openMenu == ApplicationMenu::Recent) {
		DismissOpenMenu(menuModel, editor);
	}
}

void ActivateMenuBarItem(MenuBarItemId item, MenuBarModel &menuModel,
	RecentFiles &recent, const std::string &recentStatePath,
	DocumentWorkspace &workspace, ApplicationEditor &editor,
	ApplicationUi *ui) {
	switch (item.kind) {
	case MenuBarItemKind::ApplicationAction:
		if (item.action == ApplicationAction::Find && ui) {
			ui->OpenFindBar();
		} else {
			DispatchApplicationAction(item.action, workspace, editor);
		}
		break;
	case MenuBarItemKind::RecentFile:
		if (item.recentIndex < recent.Paths().size()) {
			const std::string path = recent.Paths()[item.recentIndex];
			(void)workspace.OpenPath(path);
		}
		break;
	case MenuBarItemKind::ClearRecentFiles:
		if (recent.Clear()) {
			PersistRecentFiles(recentStatePath, recent);
			SyncRecentMenu(recent, menuModel, editor);
		}
		break;
	case MenuBarItemKind::EmptyRecentFiles:
		break;
	}
}

void CancelScrollBarShellInteraction(ScrollBarInteraction &interaction,
	ApplicationEditor &editor) {
	const bool paintChanged = interaction.dragging ||
		interaction.pressed != ScrollBarHit::None ||
		interaction.hover != ScrollBarHit::None;
	const bool wheelPending = interaction.verticalWheelRemainder != 0.0 ||
		interaction.horizontalWheelRemainder != 0.0;
	if (!paintChanged && !wheelPending) {
		return;
	}
	CancelScrollBarInteraction(interaction);
	if (paintChanged) {
		editor.InvalidateScrollBars();
	}
}

/** Card subtitle names the tab that owns the dirty-close prompt. */
[[nodiscard]] std::string UnsavedPromptSubtitle(
	const DocumentWorkspace &workspace) {
	const DocumentId promptId = workspace.PromptTab();
	for (const DocumentTabInfo &tab : workspace.Tabs()) {
		if (tab.id != promptId) {
			continue;
		}
		std::string label = tab.label;
		// Drop the dirty marker; the card already asks about unsaved changes.
		if (label.size() >= 2 &&
			label.compare(label.size() - 2, 2, " *") == 0) {
			label.resize(label.size() - 2);
		}
		return label;
	}
	if (workspace.Path().empty()) {
		return "Untitled";
	}
	return DocumentBaseName(workspace.Path());
}

[[nodiscard]] std::string_view FileErrorTitle(
	const DocumentFileError &error) noexcept {
	return error.operation == DocumentFileOperation::Open ?
		"Could not open file" :
		"Could not save file";
}

void ApplyScrollBarShellRequest(ApplicationEditor &editor,
	const ScrollBarRequest &request) {
	if (request.kind == ScrollBarRequestKind::SetVertical) {
		editor.ScrollVerticalTo(request.position);
	} else if (request.kind == ScrollBarRequestKind::SetHorizontal) {
		editor.ScrollHorizontalTo(static_cast<int>(request.position));
	}
}

[[nodiscard]] bool PointerInTopChrome(const PointerInput &input,
	const ApplicationLayout &layout) noexcept {
	if (input.action == PointerAction::Leave) {
		return false;
	}
	const int inset = layout.topChromeInset;
	if (inset <= 0) {
		return false;
	}
	return input.x >= 0.0 && input.x < static_cast<double>(layout.frameWidth) &&
		input.y >= 0.0 && input.y < static_cast<double>(inset);
}

bool HandleScrollBarShellPointer(const PointerInput &input,
	const ScrollBarLayout &scrollBars, ScrollBarInteraction &interaction,
	ApplicationEditor &editor, bool &pointerOverChrome) {
	const ScrollBarPointerResult result =
		HandleScrollBarPointer(interaction, scrollBars, input);
	if (result.barDirty) {
		editor.InvalidateScrollBars();
	}
	ApplyScrollBarShellRequest(editor, result.request);
	pointerOverChrome = result.pointerOverScrollBar;
	return result.consumed;
}

TabStripHitResult UpdateTabStripPointerState(const PointerInput &input,
	const ApplicationLayout &layout, TabStripModel &model,
	ApplicationEditor &editor, bool &pointerOverChrome) {
	TabStripHitResult hit;
	if (input.action != PointerAction::Leave) {
		hit = HitTestTabStrip(
			layout.tabs, Scintilla::Internal::Point(input.x, input.y));
	}

	// Arrow cursor over the whole permanent chrome band (menu bar + strip).
	pointerOverChrome = hit.kind != TabStripHit::None ||
		PointerInTopChrome(input, layout);
	DocumentId hoveredId = 0;
	bool closeHovered = false;
	if (hit.kind == TabStripHit::Tab || hit.kind == TabStripHit::Close) {
		hoveredId = hit.tabId;
		closeHovered = hit.kind == TabStripHit::Close;
	}
	if (hoveredId != model.hoveredId || closeHovered != model.closeHovered) {
		model.hoveredId = hoveredId;
		model.closeHovered = closeHovered;
		editor.InvalidateTopChrome();
	}
	return hit;
}

void HandleFileErrorPointer(const PointerInput &input,
	const FileErrorCardLayout &layout, std::deque<DocumentFileError> &errors,
	ApplicationEditor &editor, bool &pressHit) {
	const Scintilla::Internal::Point point(input.x, input.y);
	if (input.action == PointerAction::Press && input.button == 0) {
		pressHit = HitTestFileErrorCard(layout, point);
		return;
	}
	if (input.action == PointerAction::Release && input.button == 0) {
		const bool releaseHit = HitTestFileErrorCard(layout, point);
		if (pressHit && releaseHit) {
			if (!errors.empty()) {
				errors.pop_front();
			}
			pressHit = false;
			editor.InvalidateFrame();
		} else {
			pressHit = false;
		}
	}
}

void HandlePromptPointer(const PointerInput &input,
	const UnsavedChangesCardLayout &layout,
	std::optional<UnsavedCardHit> &pressHit, DocumentWorkspace &workspace,
	ApplicationEditor &editor, int &cardFocus) {
	const Scintilla::Internal::Point point(input.x, input.y);
	if (input.action == PointerAction::Press && input.button == 0) {
		pressHit = HitTestUnsavedChangesCard(layout, point);
		if (*pressHit == UnsavedCardHit::Save) {
			cardFocus = 0;
			editor.InvalidateClient();
		} else if (*pressHit == UnsavedCardHit::Discard) {
			cardFocus = 1;
			editor.InvalidateClient();
		} else if (*pressHit == UnsavedCardHit::Cancel) {
			cardFocus = 2;
			editor.InvalidateClient();
		}
		return;
	}
	if (input.action == PointerAction::Release && input.button == 0) {
		const UnsavedCardHit hit = HitTestUnsavedChangesCard(layout, point);
		if (pressHit && *pressHit == hit) {
			if (hit == UnsavedCardHit::Save) {
				workspace.Choose(UnsavedChoice::Save);
			} else if (hit == UnsavedCardHit::Discard) {
				workspace.Choose(UnsavedChoice::Discard);
			} else if (hit == UnsavedCardHit::Cancel) {
				workspace.Choose(UnsavedChoice::Cancel);
			}
		}
		pressHit.reset();
	}
}

ApplicationPointerCursor CursorFromChrome(bool pointerOverChrome) noexcept {
	return pointerOverChrome ? ApplicationPointerCursor::Arrow :
		ApplicationPointerCursor::Editor;
}

ApplicationPointerCursor CursorBelowModal(const PointerInput &input,
	const ApplicationLayout &layout) noexcept {
	if (input.action == PointerAction::Leave) {
		return ApplicationPointerCursor::Editor;
	}
	const bool overChrome = PointerInTopChrome(input, layout) ||
		HitTestScrollBars(layout.scrollBars,
			Scintilla::Internal::Point(input.x, input.y)).hit !=
			ScrollBarHit::None;
	return CursorFromChrome(overChrome);
}

/**
 * Apply menu, tab-strip, and scrollbar pointer transitions after modals.
 * Priority: active scrollbar drag, open menu, top chrome, scrollbar hit,
 * then editor. Editor selection capture bypasses chrome so selection release
 * is delivered; an active scrollbar drag is cancelled when a menu opens.
 */
ApplicationPointerResult HandleChromePointer(const PointerInput &input,
	const ApplicationLayout &layout, MenuBarModel &menuModel,
	TabStripModel &stripModel, ScrollBarInteraction &scrollBarInteraction,
	RecentFiles &recent, const std::string &recentStatePath,
	DocumentWorkspace &workspace, ApplicationEditor &editor,
	bool &pointerOverChrome, ApplicationUi &ui) {
	ApplicationPointerResult result;
	const bool captured = editor.WindowState().mouseCaptured;

	// Active scrollbar drag owns the sequence before top chrome (not when the
	// editor already holds a selection capture).
	if (!captured && scrollBarInteraction.dragging) {
		result.owner = ApplicationPointerOwner::ScrollBarDrag;
		result.consumed = HandleScrollBarShellPointer(input, layout.scrollBars,
			scrollBarInteraction, editor, pointerOverChrome);
		result.cursor = CursorFromChrome(pointerOverChrome);
		return result;
	}

	const bool menuWasOpen = menuModel.openMenu.has_value();
	// The caller refreshes enablement before building layout so item activation
	// and geometry read one coherent snapshot.
	const MenuBarPointerResult menuResult =
		HandleMenuBarPointer(menuModel, layout.menu, input, captured);

	if (!menuWasOpen && menuModel.openMenu.has_value()) {
		// Opening a menu cancels tentative IME; batches stay dropped while open.
		editor.CancelActiveTextInput();
		if (ui.FindBarFocused()) {
			ui.FindModel().SetFocused(false);
			editor.InvalidateTopChrome();
		}
		CancelScrollBarShellInteraction(scrollBarInteraction, editor);
	}
	if (menuResult.barDirty) {
		editor.InvalidateTopChrome();
	}
	if (menuResult.frameDirty) {
		// Open/close and dropdown hover need a full frame so the overlay path
		// cannot leave a stale panel.
		editor.InvalidateFrame();
	}
	if (menuResult.activated) {
		// Dropdown is already closed before document or persistent state changes.
		ActivateMenuBarItem(*menuResult.activated, menuModel, recent,
			recentStatePath, workspace, editor, &ui);
	}

	// A selection drag that began in the editor still owns motion and release
	// over chrome. Scintilla must see release to drop capture; the menu
	// handler already refused to consume those events.
	if (captured) {
		result.owner = ApplicationPointerOwner::EditorCapture;
		(void)UpdateTabStripPointerState(input, layout, stripModel, editor,
			pointerOverChrome);
		if (menuResult.pointerOverMenu) {
			pointerOverChrome = true;
		}
		result.consumed = false;
		result.cursor = CursorFromChrome(pointerOverChrome);
		return result;
	}

	// While a menu is open it owns presses and moves; keep strip hover clear
	// so tabs under the dropdown do not highlight. A press that just closed
	// the menu (outside dismissal) still counts as menu ownership for the
	// event so chrome below does not activate.
	if (menuModel.openMenu.has_value() ||
		(menuWasOpen && menuResult.consumed)) {
		result.owner = ApplicationPointerOwner::Menu;
		if (stripModel.hoveredId != 0 || stripModel.closeHovered) {
			stripModel.hoveredId = 0;
			stripModel.closeHovered = false;
			editor.InvalidateTopChrome();
		}
		pointerOverChrome = menuResult.pointerOverMenu ||
			PointerInTopChrome(input, layout);
		// Surface leave still needs to clear editor hover.
		if (input.action == PointerAction::Leave &&
			menuModel.openMenu.has_value()) {
			result.consumed = false;
		} else {
			result.consumed = true;
		}
		result.cursor = CursorFromChrome(pointerOverChrome);
		return result;
	}

	if (menuResult.consumed) {
		// Closed menu still swallows bar-band events (empty bar chrome).
		// Heading presses that open a menu are handled above while openMenu
		// is set.
		result.owner = ApplicationPointerOwner::PermanentChrome;
		(void)UpdateTabStripPointerState(input, layout, stripModel, editor,
			pointerOverChrome);
		if (menuResult.pointerOverMenu) {
			pointerOverChrome = true;
		}
		result.consumed = true;
		result.cursor = CursorFromChrome(pointerOverChrome);
		return result;
	}

	// Find-bar pointer routing is applied by ApplicationUi::HandlePointer after
	// this chrome helper returns an unconsumed event over the find band, or
	// via a dedicated branch when the bar is visible. The free function only
	// needs the ui reference for menu Find activation.
	(void)ui;

	const TabStripHitResult stripHit = UpdateTabStripPointerState(input, layout,
		stripModel, editor, pointerOverChrome);
	if (menuResult.pointerOverMenu) {
		pointerOverChrome = true;
	}

	const bool inStrip = stripHit.kind != TabStripHit::None;
	const bool inTopChrome = inStrip || PointerInTopChrome(input, layout);
	if (inTopChrome) {
		result.owner = ApplicationPointerOwner::PermanentChrome;
		if (!inStrip) {
			// Menu bar band already handled above when it owns the hit.
			// Empty band between controls (or find band miss) still chrome.
			result.consumed = true;
			result.cursor = CursorFromChrome(pointerOverChrome);
			return result;
		}

		if (input.action == PointerAction::Move) {
			result.consumed = true;
			result.cursor = CursorFromChrome(pointerOverChrome);
			return result;
		}

		if (input.action == PointerAction::Scroll) {
			const double amount =
				std::abs(input.deltaX) > std::abs(input.deltaY) ?
				input.deltaX :
				input.deltaY;
			int delta = 0;
			if (amount > 0.0) {
				delta = 1;
			} else if (amount < 0.0) {
				delta = -1;
			}
			if (delta != 0) {
				const int next = AdjustTabStripScroll(layout.frameWidth,
					stripModel.tabs.size(), stripModel.scrollOffset, delta,
					TabStripPreferredTabWidth() / 2);
				if (next != stripModel.scrollOffset) {
					stripModel.scrollOffset = next;
					editor.InvalidateTopChrome();
				}
			}
			result.consumed = true;
			result.cursor = CursorFromChrome(pointerOverChrome);
			return result;
		}

		if (input.action == PointerAction::Press && input.button == 0) {
			// Tab work must not leave a menu open (for example after a race with
			// a dismissal release that already cleared openMenu).
			DismissOpenMenu(menuModel, editor);
			CancelScrollBarShellInteraction(scrollBarInteraction, editor);
			if (stripHit.kind == TabStripHit::Tab) {
				workspace.ActivateTab(stripHit.tabId);
			} else if (stripHit.kind == TabStripHit::Close) {
				workspace.CloseTab(stripHit.tabId);
			} else if (stripHit.kind == TabStripHit::Add) {
				workspace.NewTab();
			}
			result.consumed = true;
			result.cursor = CursorFromChrome(pointerOverChrome);
			return result;
		}

		// Swallow other strip presses/releases so the editor does not select.
		result.consumed = true;
		result.cursor = CursorFromChrome(pointerOverChrome);
		return result;
	}

	// Below top chrome: scrollbar hit testing before the editor.
	if (HandleScrollBarShellPointer(input, layout.scrollBars,
		scrollBarInteraction, editor, pointerOverChrome)) {
		result.owner = ApplicationPointerOwner::PermanentChrome;
		result.consumed = true;
		result.cursor = CursorFromChrome(pointerOverChrome);
		return result;
	}

	// Surface leave still needs to clear editor hover.
	result.owner = ApplicationPointerOwner::Editor;
	result.consumed = false;
	result.cursor = CursorFromChrome(pointerOverChrome);
	return result;
}

}

ApplicationLayout BuildApplicationLayout(int frameWidth, int frameHeight,
	int topChromeInset, const MenuBarModel &menuModel,
	const TabStripModel &stripModel, const ScrollMetrics &scrollMetrics,
	Scintilla::Internal::PRectangle client, bool findVisible) noexcept {
	ApplicationLayout layout;
	layout.frameWidth = frameWidth;
	layout.frameHeight = frameHeight;
	layout.topChromeInset = topChromeInset;
	layout.menu = LayoutMenuBar(frameWidth, frameHeight, menuModel);
	layout.tabs = LayoutTabStrip(frameWidth, stripModel, MenuBarHeight());
	if (findVisible) {
		layout.find = LayoutFindBar(frameWidth,
			MenuBarHeight() + TabStripHeight());
	}
	layout.scrollBars = LayoutScrollBars(frameWidth, frameHeight, topChromeInset,
		scrollMetrics.vertical, scrollMetrics.horizontal);
	layout.client = client;
	layout.unsavedCard = LayoutUnsavedChangesCard(frameWidth, frameHeight);
	layout.fileErrorCard = LayoutFileErrorCard(frameWidth, frameHeight);
	return layout;
}

ApplicationUi::ApplicationUi(ApplicationEditor &editor_,
	DocumentWorkspace &workspace_,
	RecentFiles &recent_,
	std::string recentStatePath_) :
	editor(&editor_),
	workspace(&workspace_),
	recent(&recent_),
	recentStatePath(std::move(recentStatePath_)),
	lastActiveDocument(editor_.ActiveDocument()) {
	menuModel.recentFiles = recent_.Paths();
	ApplyTopChromeInset();
}

ApplicationUi::~ApplicationUi() {
	if (!ownsEditorPainters) {
		return;
	}
	editor->SetOverlayPainter(nullptr);
	editor->SetPermanentChromePainter(nullptr);
}

ApplicationLayout ApplicationUi::Layout() const {
	return BuildApplicationLayout(editor->FrameWidth(), editor->FrameHeight(),
		editor->TopChromeInset(), menuModel, stripModel, editor->Scrollbars(),
		editor->EditorClientRectangle(), findBarVisible);
}

void ApplicationUi::BeginFrameLayout() {
	// Values copied into the layout must be current before painters read them.
	(void)UpdateMenuBarActionState(menuModel, *editor);
	stripModel.scrollOffset = ClampTabStripScroll(
		editor->FrameWidth(), stripModel.tabs.size(), stripModel.scrollOffset);
	frameLayout = Layout();
}

void ApplicationUi::EndFrameLayout() noexcept {
	frameLayout.reset();
}

const ApplicationLayout &ApplicationUi::FrameLayout() const {
	if (!frameLayout.has_value()) {
		throw std::logic_error(
			"ApplicationUi::FrameLayout requires BeginFrameLayout");
	}
	return *frameLayout;
}

BoundOverlay ApplicationUi::DesiredOverlay() const noexcept {
	if (!fileErrors.empty()) {
		return BoundOverlay::FileError;
	}
	if (workspace->PromptActive()) {
		return BoundOverlay::UnsavedChanges;
	}
	if (menuModel.openMenu.has_value()) {
		return BoundOverlay::Menu;
	}
	return BoundOverlay::None;
}

void ApplicationUi::BindPainters() {
	editor->SetPermanentChromePainter(
		[this](Scintilla::Internal::Surface &surface, int, int) {
			PaintPermanentChrome(surface);
		});
	ownsEditorPainters = true;
	// Seed the overlay path from current models without a host ladder.
	(void)SynchronizeComposition();
}

BoundOverlay ApplicationUi::SynchronizeComposition() {
	// Modal cards outrank an open menu. Drop menu state so it does not linger
	// after the card closes or paint under the card.
	if ((!fileErrors.empty() || workspace->PromptActive()) &&
		menuModel.openMenu.has_value()) {
		CloseMenuBar(menuModel);
	}
	const BoundOverlay desired = DesiredOverlay();
	if (desired == overlay) {
		return overlay;
	}
	if (desired == BoundOverlay::None) {
		editor->SetOverlayPainter(nullptr);
	} else if (overlay == BoundOverlay::None) {
		// One shared overlay entry point; PaintActiveOverlay reads overlay.
		editor->SetOverlayPainter(
			[this](Scintilla::Internal::Surface &surface, int, int) {
				PaintActiveOverlay(surface);
			});
		ownsEditorPainters = true;
	}
	overlay = desired;
	// Transparent overlays and dropdowns must not leave preserved pixels.
	editor->InvalidateFrame();
	return overlay;
}

void ApplicationUi::PaintPermanentChrome(
	Scintilla::Internal::Surface &surface) const {
	const ApplicationLayout &layout = FrameLayout();
	menuPainter.PaintBar(surface, layout.menu, menuModel);
	stripPainter.Paint(surface, layout.tabs, stripModel);
	if (findBarVisible) {
		findBarPainter.Paint(surface, layout.find, findBarModel);
	}
	PaintScrollBars(surface, layout.scrollBars,
		ScrollBarPaintFromInteraction(scrollBarInteraction));
}

void ApplicationUi::PaintActiveOverlay(
	Scintilla::Internal::Surface &surface) const {
	const ApplicationLayout &layout = FrameLayout();
	switch (overlay) {
	case BoundOverlay::None:
		return;
	case BoundOverlay::Menu:
		menuPainter.PaintDropdown(surface, layout.menu, menuModel);
		return;
	case BoundOverlay::UnsavedChanges: {
		const std::string subtitle = UnsavedPromptSubtitle(*workspace);
		cardPainter.Paint(surface, layout.unsavedCard, "Save changes?",
			subtitle, cardFocus);
		return;
	}
	case BoundOverlay::FileError:
		if (fileErrors.empty()) {
			return;
		}
		fileErrorPainter.Paint(surface, layout.fileErrorCard,
			FileErrorTitle(fileErrors.front()), fileErrors.front().path);
		return;
	}
}

ApplicationPointerResult ApplicationUi::HandlePointer(
	const PointerInput &input) {
	// Refresh model values copied into MenuBarLayout before taking the one
	// snapshot used by every hit test for this event.
	(void)UpdateMenuBarActionState(menuModel, *editor);
	const ApplicationLayout layout = Layout();

	ApplicationPointerResult result;
	if (!fileErrors.empty()) {
		result.owner = ApplicationPointerOwner::FileError;
		result.consumed = true;
		CancelScrollBarShellInteraction(scrollBarInteraction, *editor);
		BlurFindField();
		HandleFileErrorPointer(input, layout.fileErrorCard, fileErrors, *editor,
			fileErrorPressHit);
		pointerCursor = CursorBelowModal(input, layout);
	} else if (workspace->PromptActive()) {
		result.owner = ApplicationPointerOwner::UnsavedPrompt;
		result.consumed = true;
		CancelScrollBarShellInteraction(scrollBarInteraction, *editor);
		BlurFindField();
		(void)UpdateTabStripPointerState(input, layout, stripModel, *editor,
			pointerOverChrome);
		HandlePromptPointer(input, layout.unsavedCard, promptPressHit,
			*workspace, *editor, cardFocus);
		pointerCursor = CursorBelowModal(input, layout);
	} else {
		const bool captured = editor->WindowState().mouseCaptured;
		// After modals and editor selection capture, a visible find bar owns
		// hits in its band before other permanent chrome and the editor.
		if (!captured && findBarVisible) {
			const FindBarHitResult findHit = HitTestFindBar(layout.find,
				Scintilla::Internal::Point(input.x, input.y));
			if (findHit.kind != FindBarHit::None) {
				const FindBarPointerResult findResult =
					HandleFindBarPointer(findBarModel, layout.find, input);
				if (findResult.fieldFocused) {
					CaptureFindOrigin();
				}
				if (findResult.dirty) {
					editor->InvalidateTopChrome();
				}
				ApplyFindBarRequests(findResult.requests);
				pointerOverChrome = true;
				pointerCursor = ApplicationPointerCursor::Arrow;
				result.owner = ApplicationPointerOwner::PermanentChrome;
				result.consumed = findResult.consumed;
				result.cursor = ApplicationPointerCursor::Arrow;
				// Leave still needs editor delivery for hover clear.
				if (input.action == PointerAction::Leave) {
					result.consumed = false;
					result.owner = ApplicationPointerOwner::Editor;
				}
				SynchronizeInteraction();
				result.cursor = CurrentPointerCursor();
				return result;
			}
			// Press in the editor client blurs the field but leaves the bar open.
			if (input.action == PointerAction::Press && input.button == 0 &&
				findBarModel.focused) {
				const bool inClient =
					layout.client.Contains(
						Scintilla::Internal::Point(input.x, input.y));
				if (inClient) {
					BlurFindField();
				}
			}
		}
		result = HandleChromePointer(
			input, layout, menuModel, stripModel,
			scrollBarInteraction, *recent, recentStatePath, *workspace, *editor,
			pointerOverChrome, *this);
		pointerCursor = result.cursor;
	}

	SynchronizeInteraction();
	// A modal that appeared during routing forces the arrow; dismissal restores
	// the cursor for the underlying chrome or editor location.
	result.cursor = CurrentPointerCursor();
	return result;
}

ApplicationPointerCursor ApplicationUi::CurrentPointerCursor() const noexcept {
	if (!fileErrors.empty() || workspace->PromptActive()) {
		return ApplicationPointerCursor::Arrow;
	}
	return pointerCursor;
}

namespace {

[[nodiscard]] bool IsSaveShortcut(const KeyboardInput &input) {
	return input.pressed &&
		input.key == static_cast<Scintilla::Keys>('S') &&
		input.modifiers == Scintilla::KeyMod::Ctrl;
}

[[nodiscard]] bool IsNextTabShortcut(const KeyboardInput &input) {
	return input.pressed &&
		input.key == Scintilla::Keys::Tab &&
		input.modifiers == Scintilla::KeyMod::Ctrl;
}

[[nodiscard]] bool IsPrevTabShortcut(const KeyboardInput &input) {
	return input.pressed &&
		input.key == Scintilla::Keys::Tab &&
		input.modifiers == (Scintilla::KeyMod::Ctrl | Scintilla::KeyMod::Shift);
}

[[nodiscard]] bool IsLetterKey(const KeyboardInput &input, char upper,
	char lower) {
	if (!input.pressed || input.modifiers != Scintilla::KeyMod::Norm) {
		return false;
	}
	if (input.key == static_cast<Scintilla::Keys>(upper) ||
		input.key == static_cast<Scintilla::Keys>(lower)) {
		return true;
	}
	return input.text == std::string(1, upper) ||
		input.text == std::string(1, lower);
}

void DismissFileError(std::deque<DocumentFileError> &errors,
	ApplicationEditor &editor, bool &pressHit) {
	if (!errors.empty()) {
		errors.pop_front();
	}
	pressHit = false;
	editor.InvalidateFrame();
}

void HandleFileErrorKeyboard(const KeyboardInput &input,
	std::deque<DocumentFileError> &errors, ApplicationEditor &editor,
	bool &pressHit) {
	if (!input.pressed) {
		return;
	}
	if (input.key == Scintilla::Keys::Escape ||
		input.key == Scintilla::Keys::Return ||
		input.key == static_cast<Scintilla::Keys>(' ') ||
		input.text == " ") {
		DismissFileError(errors, editor, pressHit);
	}
}

void HandlePromptKeyboard(const KeyboardInput &input,
	DocumentWorkspace &workspace, ApplicationEditor &editor, int &cardFocus) {
	if (!input.pressed) {
		return;
	}
	if (input.key == Scintilla::Keys::Escape) {
		workspace.Choose(UnsavedChoice::Cancel);
		return;
	}
	if (input.key == Scintilla::Keys::Tab) {
		const int delta =
			(input.modifiers == Scintilla::KeyMod::Shift) ? -1 : 1;
		cardFocus = CycleUnsavedCardFocus(cardFocus, delta);
		editor.InvalidateClient();
		return;
	}
	if (input.key == Scintilla::Keys::Left) {
		cardFocus = CycleUnsavedCardFocus(cardFocus, -1);
		editor.InvalidateClient();
		return;
	}
	if (input.key == Scintilla::Keys::Right) {
		cardFocus = CycleUnsavedCardFocus(cardFocus, 1);
		editor.InvalidateClient();
		return;
	}
	if (input.key == Scintilla::Keys::Return ||
		input.key == static_cast<Scintilla::Keys>(' ') ||
		input.text == " ") {
		const UnsavedChoice choice = cardFocus == 0 ?
			UnsavedChoice::Save :
			(cardFocus == 1 ? UnsavedChoice::Discard : UnsavedChoice::Cancel);
		workspace.Choose(choice);
		return;
	}
	if (IsSaveShortcut(input) || IsLetterKey(input, 'S', 's')) {
		workspace.Choose(UnsavedChoice::Save);
		return;
	}
	if (IsLetterKey(input, 'D', 'd')) {
		workspace.Choose(UnsavedChoice::Discard);
		return;
	}
	// Ignore other keys while the prompt owns input.
}

/**
 * Menu open accelerators and open-menu navigation before application shortcuts.
 * Returns true when the event must not reach shortcuts or the editor.
 */
bool HandleMenuBarKeyboardInput(const KeyboardInput &input,
	MenuBarModel &menuModel, RecentFiles &recent,
	const std::string &recentStatePath, DocumentWorkspace &workspace,
	ApplicationEditor &editor, ScrollBarInteraction &scrollBarInteraction,
	ApplicationUi &ui) {
	const bool menuWasOpen = menuModel.openMenu.has_value();
	// Refresh before open accelerators so FirstEnabledItem uses live state.
	(void)UpdateMenuBarActionState(menuModel, editor);
	const MenuBarKeyboardResult menuResult =
		HandleMenuBarKeyboard(menuModel, input);
	if (!menuWasOpen && menuModel.openMenu.has_value()) {
		// Opening a menu cancels tentative IME; batches stay dropped while open.
		editor.CancelActiveTextInput();
		if (ui.FindBarFocused()) {
			ui.FindModel().SetFocused(false);
			editor.InvalidateTopChrome();
		}
		CancelScrollBarShellInteraction(scrollBarInteraction, editor);
	}
	if (menuResult.barDirty) {
		editor.InvalidateTopChrome();
	}
	if (menuResult.frameDirty) {
		editor.InvalidateFrame();
	}
	if (menuResult.activated) {
		ActivateMenuBarItem(*menuResult.activated, menuModel, recent,
			recentStatePath, workspace, editor, &ui);
	}
	return menuResult.consumed;
}

}

ApplicationKeyboardResult ApplicationUi::HandleKeyboard(
	const KeyboardInput &input) {
	ApplicationKeyboardResult result;

	if (!fileErrors.empty()) {
		result.owner = ApplicationKeyboardOwner::FileError;
		CancelScrollBarShellInteraction(scrollBarInteraction, *editor);
		BlurFindField();
		HandleFileErrorKeyboard(input, fileErrors, *editor, fileErrorPressHit);
	} else if (workspace->PromptActive()) {
		result.owner = ApplicationKeyboardOwner::UnsavedPrompt;
		CancelScrollBarShellInteraction(scrollBarInteraction, *editor);
		BlurFindField();
		HandlePromptKeyboard(input, *workspace, *editor, cardFocus);
	} else if (HandleMenuBarKeyboardInput(input, menuModel, *recent,
		recentStatePath, *workspace, *editor, scrollBarInteraction, *this)) {
		// Menu accelerators and open-menu navigation run before application
		// shortcuts and editor typing.
		result.owner = ApplicationKeyboardOwner::Menu;
	} else if (const std::optional<ApplicationAction> action =
		MatchApplicationAction(input);
		action && *action == ApplicationAction::Find) {
		// Global Ctrl+F outranks a focused find field so it reselects the query.
		OpenFindBar();
		result.owner = ApplicationKeyboardOwner::ApplicationShortcut;
	} else if (findBarVisible && findBarModel.focused) {
		const FindBarKeyboardResult findResult =
			HandleFindBarKeyboard(findBarModel, input);
		if (findResult.dirty) {
			editor->InvalidateTopChrome();
		}
		if (findResult.queryChanged) {
			RunIncrementalFind();
		}
		ApplyFindBarRequests(findResult.requests);
		result.owner = ApplicationKeyboardOwner::FindBar;
	} else if (const std::optional<ApplicationAction> action =
		MatchApplicationAction(input)) {
		ActivateAction(*action);
		result.owner = ApplicationKeyboardOwner::ApplicationShortcut;
	} else if (IsPrevTabShortcut(input)) {
		workspace->CycleTab(-1);
		result.owner = ApplicationKeyboardOwner::ApplicationShortcut;
	} else if (IsNextTabShortcut(input)) {
		workspace->CycleTab(1);
		result.owner = ApplicationKeyboardOwner::ApplicationShortcut;
	} else {
		editor->HandleKeyboardInput(input);
		result.owner = ApplicationKeyboardOwner::Editor;
	}

	SynchronizeInteraction();
	return result;
}

void ApplicationUi::HandleFocus(bool focused) {
	editor->SetKeyboardFocus(focused);
	if (focused) {
		return;
	}
	// Focus loss closes chrome that should not survive leaving the surface.
	// SetKeyboardFocus already cancels tentative IME on the editor.
	DismissOpenMenu(menuModel, *editor);
	CancelScrollBarShellInteraction(scrollBarInteraction, *editor);
	fileErrorPressHit = false;
	promptPressHit.reset();
	// Keep the bar and query; only drop field focus and presses.
	BlurFindField();
}

bool ApplicationUi::ChromeOwnsInput() const noexcept {
	return !fileErrors.empty() || workspace->PromptActive() ||
		menuModel.openMenu.has_value();
}

void ApplicationUi::NotifyPromptBegan() {
	DismissOpenMenu(menuModel, *editor);
	// Opening a modal card cancels tentative IME; batches stay dropped while
	// the card is active.
	editor->CancelActiveTextInput();
	BlurFindField();
	CancelScrollBarShellInteraction(scrollBarInteraction, *editor);
	cardFocus = 0;
	promptPressHit.reset();
}

void ApplicationUi::AppendFileErrors(std::vector<DocumentFileError> errors) {
	if (errors.empty()) {
		return;
	}
	const bool becameActive = fileErrors.empty();
	for (DocumentFileError &error : errors) {
		fileErrors.push_back(std::move(error));
	}
	if (!becameActive) {
		return;
	}
	DismissOpenMenu(menuModel, *editor);
	// Opening a modal card cancels tentative IME; batches stay dropped while
	// any error remains in the queue.
	editor->CancelActiveTextInput();
	BlurFindField();
	CancelScrollBarShellInteraction(scrollBarInteraction, *editor);
	fileErrorPressHit = false;
	// The higher-priority card must not leave a press armed on the prompt
	// underneath it. Otherwise a later release could activate that button.
	promptPressHit.reset();
	editor->InvalidateFrame();
}

bool ApplicationUi::SynchronizeTabs(bool revealActive) {
	const std::vector<DocumentTabInfo> tabs = workspace->Tabs();
	bool changed = stripModel.tabs.size() != tabs.size();
	std::vector<TabStripTab> next;
	next.reserve(tabs.size());
	for (const DocumentTabInfo &info : tabs) {
		TabStripTab tab;
		tab.id = info.id;
		tab.label = info.label;
		tab.dirty = info.dirty;
		tab.active = info.active;
		next.push_back(std::move(tab));
	}
	if (!changed) {
		for (std::size_t i = 0; i < next.size(); ++i) {
			if (next[i].id != stripModel.tabs[i].id ||
				next[i].label != stripModel.tabs[i].label ||
				next[i].dirty != stripModel.tabs[i].dirty ||
				next[i].active != stripModel.tabs[i].active) {
				changed = true;
				break;
			}
		}
	}
	stripModel.tabs = std::move(next);

	bool hoverValid = stripModel.hoveredId == 0;
	for (const TabStripTab &tab : stripModel.tabs) {
		if (tab.id == stripModel.hoveredId) {
			hoverValid = true;
			break;
		}
	}
	if (!hoverValid) {
		stripModel.hoveredId = 0;
		stripModel.closeHovered = false;
		changed = true;
	}

	const int stripWidth = editor->FrameWidth();
	const int clamped = ClampTabStripScroll(
		stripWidth, stripModel.tabs.size(), stripModel.scrollOffset);
	if (clamped != stripModel.scrollOffset) {
		stripModel.scrollOffset = clamped;
		changed = true;
	}

	if (revealActive) {
		std::size_t activeIndex = 0;
		bool found = false;
		for (std::size_t i = 0; i < stripModel.tabs.size(); ++i) {
			if (stripModel.tabs[i].active) {
				activeIndex = i;
				found = true;
				break;
			}
		}
		if (found) {
			const int revealed = ScrollTabStripToIndex(stripWidth,
				stripModel.tabs.size(), activeIndex, stripModel.scrollOffset);
			if (revealed != stripModel.scrollOffset) {
				stripModel.scrollOffset = revealed;
				changed = true;
			}
		}
	}
	return changed;
}

void ApplicationUi::SynchronizeDirtyTabs() {
	if (SynchronizeTabs()) {
		editor->InvalidateTopChrome();
	}
}

void ApplicationUi::RequestClose() {
	workspace->RequestClose();
	SynchronizeInteraction();
}

void ApplicationUi::SynchronizeInteraction() {
	const DocumentId activeDocument = editor->ActiveDocument();
	const bool documentChanged = activeDocument != lastActiveDocument;
	if (documentChanged || !fileErrors.empty() || workspace->PromptActive() ||
		menuModel.openMenu.has_value()) {
		CancelScrollBarShellInteraction(scrollBarInteraction, *editor);
	}
	if (documentChanged && findBarVisible) {
		// Never reuse a byte origin from another retained document.
		CaptureFindOrigin();
		if (findBarModel.focused && !findBarModel.query.empty()) {
			RunIncrementalFind();
		}
	}
	lastActiveDocument = activeDocument;
}

int ApplicationUi::BaseTopChromeInset() const noexcept {
	return MenuBarHeight() + TabStripHeight();
}

int ApplicationUi::CurrentTopChromeInset() const noexcept {
	return BaseTopChromeInset() + (findBarVisible ? FindBarHeight() : 0);
}

void ApplicationUi::ApplyTopChromeInset() {
	const int inset = CurrentTopChromeInset();
	if (editor->TopChromeInset() != inset) {
		editor->SetTopChromeInset(inset);
	}
}

void ApplicationUi::CaptureFindOrigin() {
	findOrigin = FindOrigin{
		editor->ActiveDocument(),
		editor->GetSelectionStart(),
	};
}

void ApplicationUi::ApplyFindOutcome(ApplicationFindOutcome outcome) {
	switch (outcome) {
	case ApplicationFindOutcome::Found:
		findBarModel.SetStatus(FindBarStatus::None);
		break;
	case ApplicationFindOutcome::Wrapped:
		findBarModel.SetStatus(FindBarStatus::Wrapped);
		break;
	case ApplicationFindOutcome::NotFound:
		findBarModel.SetStatus(FindBarStatus::NoMatches);
		break;
	}
	editor->InvalidateTopChrome();
}

void ApplicationUi::ApplyFindBarRequests(
	const std::vector<FindBarRequest> &requests) {
	for (const FindBarRequest &request : requests) {
		switch (request.kind) {
		case FindBarRequestKind::SearchForward:
			RunFindForward();
			break;
		case FindBarRequestKind::SearchBackward:
			RunFindBackward();
			break;
		case FindBarRequestKind::Close:
			CloseFindBar();
			break;
		case FindBarRequestKind::ClipboardCopy:
		case FindBarRequestKind::ClipboardPaste:
			// Clipboard mediation is wired in the IME/transfer routing commit.
			break;
		}
	}
}

void ApplicationUi::RunIncrementalFind() {
	if (!findBarVisible || findBarModel.query.empty()) {
		findBarModel.SetStatus(FindBarStatus::None);
		editor->InvalidateTopChrome();
		return;
	}
	if (!findOrigin || findOrigin->document != editor->ActiveDocument()) {
		CaptureFindOrigin();
	}
	ApplyFindOutcome(
		editor->FindTextForward(findBarModel.query, findOrigin->position));
}

void ApplicationUi::RunFindForward() {
	if (!findBarVisible || findBarModel.query.empty()) {
		findBarModel.SetStatus(FindBarStatus::None);
		editor->InvalidateTopChrome();
		return;
	}
	ApplyFindOutcome(editor->FindTextForwardFromSelection(findBarModel.query));
}

void ApplicationUi::RunFindBackward() {
	if (!findBarVisible || findBarModel.query.empty()) {
		findBarModel.SetStatus(FindBarStatus::None);
		editor->InvalidateTopChrome();
		return;
	}
	ApplyFindOutcome(editor->FindTextBackwardFromSelection(findBarModel.query));
}

void ApplicationUi::BlurFindField() {
	if (!findBarModel.focused && !findBarModel.pressOrigin &&
		!findBarModel.preedit) {
		return;
	}
	findBarModel.SetFocused(false);
	editor->InvalidateTopChrome();
}

void ApplicationUi::ActivateAction(ApplicationAction action) {
	if (action == ApplicationAction::Find) {
		OpenFindBar();
		return;
	}
	DispatchApplicationAction(action, *workspace, *editor);
}

void ApplicationUi::OpenFindBar() {
	// Seed an empty query from a single-line selection of valid UTF-8.
	if (findBarModel.query.empty() && editor->HasSelection()) {
		const std::string selected = editor->GetSelText();
		if (!selected.empty() &&
			selected.find('\n') == std::string::npos &&
			selected.find('\r') == std::string::npos &&
			Scintilla::Internal::UTF8IsValid(selected)) {
			(void)findBarModel.SetQuery(selected);
		}
	}

	const bool wasVisible = findBarVisible;
	findBarVisible = true;
	findBarModel.SetFocused(true);
	CaptureFindOrigin();
	ApplyTopChromeInset();
	if (!wasVisible) {
		// Client and scrollbars move when the band appears.
		editor->InvalidateFrame();
	} else {
		editor->InvalidateTopChrome();
	}
	// Reselect the retained query for replacement typing.
	findBarModel.SelectAll();
	if (!findBarModel.query.empty()) {
		RunIncrementalFind();
	}
}

void ApplicationUi::CloseFindBar() {
	if (!findBarVisible) {
		return;
	}
	findBarModel.CancelPreedit();
	findBarModel.SetFocused(false);
	findBarModel.pressOrigin.reset();
	findBarVisible = false;
	findOrigin.reset();
	ApplyTopChromeInset();
	editor->InvalidateFrame();
}

void ApplicationUi::HandleFrameSizeChange() {
	// Width may change scroll clamping and tab layout. Keep an open menu;
	// LayoutMenuBar recomputes headings and clamps the panel. Resize and
	// scale cancel an in-progress scrollbar drag.
	CancelScrollBarShellInteraction(scrollBarInteraction, *editor);
	(void)SynchronizeTabs(true);
	editor->InvalidateTopChrome();
	editor->InvalidateScrollBars();
	if (!fileErrors.empty() || workspace->PromptActive() ||
		menuModel.openMenu.has_value()) {
		// Full frame so the card or dropdown cannot leave stale pixels.
		editor->InvalidateFrame();
	}
}

void ApplicationUi::RefreshOpenMenuActionState() {
	// Clipboard offer can arrive while a dropdown is open; flip paste
	// enablement and force a full-frame paint so the row updates.
	if (menuModel.openMenu.has_value() &&
		UpdateMenuBarActionState(menuModel, *editor)) {
		editor->InvalidateFrame();
	}
}

void ApplicationUi::PrepareForExit() {
	DismissOpenMenu(menuModel, *editor);
	CancelScrollBarShellInteraction(scrollBarInteraction, *editor);
}

std::vector<ApplicationShellEffect> ApplicationUi::TakeShellEffects() {
	std::vector<ApplicationShellEffect> effects;

	for (const DocumentShellRequest request : workspace->TakeRequests()) {
		switch (request) {
		case DocumentShellRequest::ShowOpen:
			// Portals take over interaction; the in-window menu must not stay open.
			DismissOpenMenu(menuModel, *editor);
			{
				DocumentDialogIntent dialog = workspace->BeginOpenDialog();
				effects.push_back({
					ApplicationShellEffectKind::ShowOpen,
					dialog.id,
					std::move(dialog.documentPath),
				});
			}
			break;
		case DocumentShellRequest::ShowSaveAs:
			DismissOpenMenu(menuModel, *editor);
			{
				DocumentDialogIntent dialog = workspace->BeginSaveAsDialog();
				effects.push_back({
					ApplicationShellEffectKind::ShowSaveAs,
					dialog.id,
					std::move(dialog.documentPath),
				});
			}
			break;
		case DocumentShellRequest::AcceptClose:
			effects.push_back({
				ApplicationShellEffectKind::AcceptClose, {}, {}});
			break;
		case DocumentShellRequest::PromptBegan:
			// Card input and paint priority is higher than the menu.
			NotifyPromptBegan();
			break;
		case DocumentShellRequest::RefreshTabs:
			(void)SynchronizeTabs(true);
			editor->InvalidateTopChrome();
			break;
		}
	}

	bool recentChanged = false;
	for (const std::string &path : workspace->TakeRecentPaths()) {
		recentChanged = recent->Record(path) || recentChanged;
	}
	if (recentChanged) {
		PersistRecentFiles(recentStatePath, *recent);
		SyncRecentMenu(*recent, menuModel, *editor);
	}

	std::vector<DocumentFileError> errors = workspace->TakeFileErrors();
	if (!errors.empty()) {
		AppendFileErrors(std::move(errors));
	}

	SynchronizeInteraction();
	return effects;
}

void ApplicationUi::NotifyDialogFailed(DocumentDialogId dialogId) {
	workspace->AbandonDialog(dialogId);
	SynchronizeInteraction();
}

void ApplicationUi::NotifyDialogResult(DocumentDialogId dialogId, bool accepted,
	const std::vector<std::string> &paths) {
	workspace->HandleDialogResult(dialogId, accepted, paths);
	SynchronizeInteraction();
}

}
