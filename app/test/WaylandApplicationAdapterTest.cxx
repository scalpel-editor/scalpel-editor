#include "catch.hpp"

#include <optional>
#include <string>
#include <vector>

#include "ApplicationEditor.h"
#include "ApplicationSession.h"
#include "ApplicationUi.h"
#include "DocumentWorkspace.h"
#include "RecentFiles.h"
#include "WaylandApplicationAdapter.h"

using Scalpel::ActiveFileDialogs;
using Scalpel::ApplicationEditor;
using Scalpel::ApplicationShellEffect;
using Scalpel::ApplicationShellEffectKind;
using Scalpel::ApplicationTerminationReason;
using Scalpel::ApplicationUi;
using Scalpel::DocumentWorkspace;
using Scalpel::FileDialogMode;
using Scalpel::FileDialogResult;
using Scalpel::FileDialogResultStatus;
using Scalpel::RecentFiles;

namespace {

struct Fixture {
	ApplicationEditor editor{320, 180};
	DocumentWorkspace workspace{editor};
	RecentFiles recent;
	ApplicationUi ui{editor, workspace, recent, ""};
	ActiveFileDialogs active;

	Fixture() {
		editor.LoadInitialBuffer("adapter\n");
		(void)ui.SynchronizeTabs();
	}
};

}

TEST_CASE("application adapter maps portal results to dialog identities") {
	Fixture fixture;
	fixture.workspace.RequestOpen();
	const std::vector<ApplicationShellEffect> effects =
		fixture.ui.TakeShellEffects();
	REQUIRE(effects.size() == 1);
	REQUIRE(effects[0].kind == ApplicationShellEffectKind::ShowOpen);
	const auto dialogId = effects[0].dialogId;

	bool quitAccepted = false;
	Scalpel::ApplySessionShellEffects(effects, fixture.ui, fixture.active,
		quitAccepted,
		[](const ApplicationShellEffect &) -> std::optional<uint64_t> {
			return 42;
		});
	CHECK_FALSE(quitAccepted);
	REQUIRE(fixture.active.size() == 1);
	CHECK(fixture.active.at(42) == dialogId);

	FileDialogResult accepted;
	accepted.id = 42;
	accepted.mode = FileDialogMode::Open;
	accepted.status = FileDialogResultStatus::Accepted;
	accepted.paths = {"/tmp/scalpel-adapter-open.txt"};
	// Path need not exist; open failure is a separate workspace outcome.
	// Use a cancelled result for the mapping assert, then a stale id.
	FileDialogResult cancelled = accepted;
	cancelled.status = FileDialogResultStatus::Cancelled;
	cancelled.paths.clear();
	Scalpel::ApplyFileDialogResults({cancelled}, fixture.ui, fixture.active);
	CHECK(fixture.active.empty());
	CHECK(fixture.workspace.TabCount() == 1);
	CHECK(fixture.editor.Text() == "adapter\n");
}

TEST_CASE("application adapter ignores stale portal request IDs") {
	Fixture fixture;
	fixture.workspace.RequestOpen();
	const std::vector<ApplicationShellEffect> effects =
		fixture.ui.TakeShellEffects();
	bool quitAccepted = false;
	Scalpel::ApplySessionShellEffects(effects, fixture.ui, fixture.active,
		quitAccepted,
		[](const ApplicationShellEffect &) -> std::optional<uint64_t> {
			return 7;
		});
	REQUIRE(fixture.active.count(7) == 1);

	FileDialogResult stale;
	stale.id = 99;
	stale.status = FileDialogResultStatus::Accepted;
	stale.paths = {"/tmp/must-not-open.txt"};
	Scalpel::ApplyFileDialogResults({stale}, fixture.ui, fixture.active);
	CHECK(fixture.active.count(7) == 1);
	CHECK(fixture.workspace.TabCount() == 1);
	CHECK(fixture.editor.Text() == "adapter\n");
}

TEST_CASE("application adapter reports dialog startup failure") {
	Fixture fixture;
	fixture.workspace.RequestOpen();
	const std::vector<ApplicationShellEffect> effects =
		fixture.ui.TakeShellEffects();
	REQUIRE(effects.size() == 1);
	const auto dialogId = effects[0].dialogId;

	bool quitAccepted = false;
	Scalpel::ApplySessionShellEffects(effects, fixture.ui, fixture.active,
		quitAccepted,
		[](const ApplicationShellEffect &) -> std::optional<uint64_t> {
			return std::nullopt;
		});
	CHECK(fixture.active.empty());
	CHECK_FALSE(quitAccepted);

	// A late transport result cannot revive an abandoned application intent.
	FileDialogResult late;
	late.id = 1;
	late.status = FileDialogResultStatus::Accepted;
	late.paths = {"/tmp/must-not-open.txt"};
	fixture.active[1] = dialogId;
	// Mapping still present only if the host recorded one; after failure the
	// workspace intent is gone even if a map entry is forced.
	Scalpel::ApplyFileDialogResults({late}, fixture.ui, fixture.active);
	CHECK(fixture.active.empty());
	CHECK(fixture.workspace.TabCount() == 1);
	CHECK(fixture.editor.Text() == "adapter\n");
}

TEST_CASE("application adapter accepts close and names termination reason") {
	Fixture fixture;
	bool quitAccepted = false;
	const ApplicationShellEffect accept{
		ApplicationShellEffectKind::AcceptClose, {}, {}};
	Scalpel::ApplySessionShellEffects({accept}, fixture.ui, fixture.active,
		quitAccepted,
		[](const ApplicationShellEffect &) -> std::optional<uint64_t> {
			return std::nullopt;
		});
	CHECK(quitAccepted);
	CHECK(Scalpel::SessionLoopTerminationReason(true) ==
		ApplicationTerminationReason::AcceptedClose);
	CHECK(Scalpel::SessionLoopTerminationReason(false) ==
		ApplicationTerminationReason::ForcedShutdown);
}

TEST_CASE("application adapter maps concurrent open and save dialogs") {
	Fixture fixture;
	fixture.workspace.RequestOpen();
	fixture.workspace.RequestSaveAs();
	const std::vector<ApplicationShellEffect> effects =
		fixture.ui.TakeShellEffects();
	REQUIRE(effects.size() == 2);

	bool quitAccepted = false;
	uint64_t nextId = 100;
	Scalpel::ApplySessionShellEffects(effects, fixture.ui, fixture.active,
		quitAccepted,
		[&nextId](const ApplicationShellEffect &) -> std::optional<uint64_t> {
			return nextId++;
		});
	CHECK(fixture.active.size() == 2);
	CHECK(fixture.active.at(100) == effects[0].dialogId);
	CHECK(fixture.active.at(101) == effects[1].dialogId);

	FileDialogResult saveCancel;
	saveCancel.id = 101;
	saveCancel.mode = FileDialogMode::Save;
	saveCancel.status = FileDialogResultStatus::Cancelled;
	Scalpel::ApplyFileDialogResults({saveCancel}, fixture.ui, fixture.active);
	CHECK(fixture.active.size() == 1);
	CHECK(fixture.active.count(100) == 1);
	CHECK(fixture.active.count(101) == 0);
}

TEST_CASE("application adapter leaves context-menu effects to the host") {
	Fixture fixture;
	bool quitAccepted = false;
	bool started = false;
	const ApplicationShellEffect showMenu{
		ApplicationShellEffectKind::ShowContextMenu, {}, {}, 10, 20, 3};
	Scalpel::ApplySessionShellEffects({showMenu}, fixture.ui, fixture.active,
		quitAccepted,
		[&started](const ApplicationShellEffect &) -> std::optional<uint64_t> {
			started = true;
			return 1;
		});
	CHECK_FALSE(started);
	CHECK_FALSE(quitAccepted);
	CHECK(fixture.active.empty());
}
