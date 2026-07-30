#include "ApplicationUi.h"

#include <stdexcept>
#include <utility>

#include "ApplicationEditor.h"
#include "RecentFiles.h"

namespace Scalpel {

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

}
