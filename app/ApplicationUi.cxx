#include "ApplicationUi.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "ApplicationAction.h"
#include "ApplicationEditor.h"
#include "RecentFiles.h"

namespace Scalpel {

namespace {

void MarkTopChrome(ApplicationPointerDamage &damage,
	ApplicationEditor &editor) {
	damage.topChrome = true;
	editor.InvalidateTopChrome();
}

void MarkScrollBars(ApplicationPointerDamage &damage,
	ApplicationEditor &editor) {
	damage.scrollBars = true;
	editor.InvalidateScrollBars();
}

void MarkClient(ApplicationPointerDamage &damage, ApplicationEditor &editor) {
	damage.client = true;
	editor.InvalidateClient();
}

void MarkFrame(ApplicationPointerDamage &damage, ApplicationEditor &editor) {
	damage.frame = true;
	editor.InvalidateFrame();
}

void DismissOpenMenu(MenuBarModel &menuModel, ApplicationEditor &editor,
	ApplicationPointerDamage &damage) {
	if (!menuModel.openMenu.has_value()) {
		return;
	}
	CloseMenuBar(menuModel);
	MarkFrame(damage, editor);
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
	ApplicationEditor &editor, ApplicationPointerDamage &damage) {
	if (menuModel.recentFiles == recent.Paths()) {
		return;
	}
	menuModel.recentFiles = recent.Paths();
	MarkTopChrome(damage, editor);
	// Recent rows are identified by index. If the MRU list rewrites while
	// Recent is open, focus, hover, and press would still point at the old
	// index. Dismiss so the next open rebuilds from the new list.
	if (menuModel.openMenu == ApplicationMenu::Recent) {
		DismissOpenMenu(menuModel, editor, damage);
	}
}

void ActivateMenuBarItem(MenuBarItemId item, MenuBarModel &menuModel,
	RecentFiles &recent, const std::string &recentStatePath,
	DocumentWorkspace &workspace, ApplicationEditor &editor,
	ApplicationPointerDamage &damage) {
	switch (item.kind) {
	case MenuBarItemKind::ApplicationAction:
		DispatchApplicationAction(item.action, workspace, editor);
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
			SyncRecentMenu(recent, menuModel, editor, damage);
		}
		break;
	case MenuBarItemKind::EmptyRecentFiles:
		break;
	}
}

void CancelScrollBarShellInteraction(ScrollBarInteraction &interaction,
	ApplicationEditor &editor, ApplicationPointerDamage &damage) {
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
		MarkScrollBars(damage, editor);
	}
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
	ApplicationEditor &editor, bool &pointerOverChrome,
	ApplicationPointerDamage &damage) {
	const ScrollBarPointerResult result =
		HandleScrollBarPointer(interaction, scrollBars, input);
	if (result.barDirty) {
		MarkScrollBars(damage, editor);
	}
	ApplyScrollBarShellRequest(editor, result.request);
	if (result.pointerOverScrollBar) {
		pointerOverChrome = true;
	}
	return result.consumed;
}

TabStripHitResult UpdateTabStripPointerState(const PointerInput &input,
	const ApplicationLayout &layout, TabStripModel &model,
	ApplicationEditor &editor, bool &pointerOverChrome,
	ApplicationPointerDamage &damage) {
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
		MarkTopChrome(damage, editor);
	}
	return hit;
}

void HandleFileErrorPointer(const PointerInput &input,
	const FileErrorCardLayout &layout, std::deque<DocumentFileError> &errors,
	ApplicationEditor &editor, bool &pressHit,
	ApplicationPointerDamage &damage) {
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
			MarkFrame(damage, editor);
		} else {
			pressHit = false;
		}
	}
}

void HandlePromptPointer(const PointerInput &input,
	const UnsavedChangesCardLayout &layout,
	std::optional<UnsavedCardHit> &pressHit, DocumentWorkspace &workspace,
	ApplicationEditor &editor, int &cardFocus,
	ApplicationPointerDamage &damage) {
	const Scintilla::Internal::Point point(input.x, input.y);
	if (input.action == PointerAction::Press && input.button == 0) {
		pressHit = HitTestUnsavedChangesCard(layout, point);
		if (*pressHit == UnsavedCardHit::Save) {
			cardFocus = 0;
			MarkClient(damage, editor);
		} else if (*pressHit == UnsavedCardHit::Discard) {
			cardFocus = 1;
			MarkClient(damage, editor);
		} else if (*pressHit == UnsavedCardHit::Cancel) {
			cardFocus = 2;
			MarkClient(damage, editor);
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
	bool &pointerOverChrome) {
	ApplicationPointerResult result;
	const bool captured = editor.WindowState().mouseCaptured;

	// Active scrollbar drag owns the sequence before top chrome (not when the
	// editor already holds a selection capture).
	if (!captured && scrollBarInteraction.dragging) {
		result.owner = ApplicationPointerOwner::ScrollBarDrag;
		result.consumed = HandleScrollBarShellPointer(input, layout.scrollBars,
			scrollBarInteraction, editor, pointerOverChrome, result.damage);
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
		CancelScrollBarShellInteraction(
			scrollBarInteraction, editor, result.damage);
	}
	if (menuResult.barDirty) {
		MarkTopChrome(result.damage, editor);
	}
	if (menuResult.frameDirty) {
		// Open/close and dropdown hover need a full frame so the overlay path
		// cannot leave a stale panel.
		MarkFrame(result.damage, editor);
	}
	if (menuResult.activated) {
		// Dropdown is already closed before document or persistent state changes.
		ActivateMenuBarItem(*menuResult.activated, menuModel, recent,
			recentStatePath, workspace, editor, result.damage);
		result.activated = menuResult.activated;
	}

	// A selection drag that began in the editor still owns motion and release
	// over chrome. Scintilla must see release to drop capture; the menu
	// handler already refused to consume those events.
	if (captured) {
		result.owner = ApplicationPointerOwner::EditorCapture;
		(void)UpdateTabStripPointerState(input, layout, stripModel, editor,
			pointerOverChrome, result.damage);
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
			MarkTopChrome(result.damage, editor);
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
			pointerOverChrome, result.damage);
		if (menuResult.pointerOverMenu) {
			pointerOverChrome = true;
		}
		result.consumed = true;
		result.cursor = CursorFromChrome(pointerOverChrome);
		return result;
	}

	const TabStripHitResult stripHit = UpdateTabStripPointerState(input, layout,
		stripModel, editor, pointerOverChrome, result.damage);
	if (menuResult.pointerOverMenu) {
		pointerOverChrome = true;
	}

	const bool inStrip = stripHit.kind != TabStripHit::None;
	const bool inTopChrome = inStrip || PointerInTopChrome(input, layout);
	if (inTopChrome) {
		result.owner = ApplicationPointerOwner::PermanentChrome;
		if (!inStrip) {
			// Menu bar band already handled above when it owns the hit.
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
					MarkTopChrome(result.damage, editor);
				}
			}
			result.consumed = true;
			result.cursor = CursorFromChrome(pointerOverChrome);
			return result;
		}

		if (input.action == PointerAction::Press && input.button == 0) {
			// Tab work must not leave a menu open (for example after a race with
			// a dismissal release that already cleared openMenu).
			DismissOpenMenu(menuModel, editor, result.damage);
			CancelScrollBarShellInteraction(
				scrollBarInteraction, editor, result.damage);
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
		scrollBarInteraction, editor, pointerOverChrome, result.damage)) {
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
	Scintilla::Internal::PRectangle client) noexcept {
	ApplicationLayout layout;
	layout.frameWidth = frameWidth;
	layout.frameHeight = frameHeight;
	layout.topChromeInset = topChromeInset;
	layout.menu = LayoutMenuBar(frameWidth, frameHeight, menuModel);
	layout.tabs = LayoutTabStrip(frameWidth, stripModel, MenuBarHeight());
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
}

ApplicationLayout ApplicationUi::Layout() const {
	return BuildApplicationLayout(editor->FrameWidth(), editor->FrameHeight(),
		editor->TopChromeInset(), menuModel, stripModel, editor->Scrollbars(),
		editor->EditorClientRectangle());
}

void ApplicationUi::BeginFrameLayout() {
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

ApplicationPointerResult ApplicationUi::HandlePointer(
	const PointerInput &input) {
	// Refresh model values copied into MenuBarLayout before taking the one
	// snapshot used by every hit test for this event.
	(void)UpdateMenuBarActionState(menuModel, *editor);
	const ApplicationLayout layout = Layout();

	if (!fileErrors.empty()) {
		ApplicationPointerResult result;
		result.owner = ApplicationPointerOwner::FileError;
		result.cursor = ApplicationPointerCursor::Arrow;
		result.consumed = true;
		CancelScrollBarShellInteraction(
			scrollBarInteraction, *editor, result.damage);
		HandleFileErrorPointer(input, layout.fileErrorCard, fileErrors, *editor,
			fileErrorPressHit, result.damage);
		return result;
	}

	if (workspace->PromptActive()) {
		ApplicationPointerResult result;
		result.owner = ApplicationPointerOwner::UnsavedPrompt;
		result.cursor = ApplicationPointerCursor::Arrow;
		result.consumed = true;
		CancelScrollBarShellInteraction(
			scrollBarInteraction, *editor, result.damage);
		(void)UpdateTabStripPointerState(input, layout, stripModel, *editor,
			pointerOverChrome, result.damage);
		HandlePromptPointer(input, layout.unsavedCard, promptPressHit,
			*workspace, *editor, cardFocus, result.damage);
		// Modal cards force the arrow even when strip hover updated chrome.
		result.cursor = ApplicationPointerCursor::Arrow;
		return result;
	}

	return HandleChromePointer(input, layout, menuModel, stripModel,
		scrollBarInteraction, *recent, recentStatePath, *workspace, *editor,
		pointerOverChrome);
}

}
