#include "ApplicationUi.h"

#include <utility>

#include "ApplicationEditor.h"
#include "RecentFiles.h"

namespace Scalpel {

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

}
