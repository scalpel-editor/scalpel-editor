#include <algorithm>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "WaylandLifecycle.h"
#include "WaylandCursor.h"
#include "WaylandFrame.h"
#include "WaylandScale.h"
#include "WaylandWindow.h"

namespace {

using Cursor = Scintilla::Internal::Window::Cursor;

std::string_view FirstAvailable(const Scalpel::WaylandCursorNames &names,
	std::initializer_list<std::string_view> available) {
	for (std::size_t index = 0; index < names.count; ++index) {
		if (std::find(available.begin(), available.end(), names[index]) != available.end()) {
			return names[index];
		}
	}
	return {};
}

}

TEST_CASE("Wayland cursor choices cover every editor cursor") {
	const std::array cursors{
		Cursor::invalid, Cursor::text, Cursor::arrow, Cursor::up, Cursor::wait,
		Cursor::horizontal, Cursor::vertical, Cursor::reverseArrow, Cursor::hand,
	};
	for (const Cursor cursor : cursors) {
		const Scalpel::WaylandCursorNames names = Scalpel::CursorNames(cursor);
		INFO(static_cast<int>(cursor));
		REQUIRE(names.count > 0);
		CHECK(names[names.count - 1] == "arrow");
	}
	CHECK(Scalpel::CursorNames(Cursor::text)[0] == "text");
	CHECK(Scalpel::CursorNames(Cursor::up)[0] == "sb_up_arrow");
	CHECK(Scalpel::CursorNames(Cursor::reverseArrow)[0] == "right_ptr");
	CHECK(Scalpel::CursorNames(Cursor::hand)[0] == "pointer");
	CHECK(FirstAvailable(Scalpel::CursorNames(Cursor::text), {"left_ptr"}) ==
		"left_ptr");
	CHECK(FirstAvailable(Scalpel::CursorNames(Cursor::hand), {"arrow"}) == "arrow");
}

TEST_CASE("Wayland cursor state retains requests until pointer entry") {
	Scalpel::WaylandCursorState cursor;

	CHECK_FALSE(cursor.Request(Cursor::text).has_value());
	CHECK_FALSE(cursor.Enter(42).has_value());
	const auto available = cursor.SetThemeAvailable(true);
	REQUIRE(available.has_value());
	CHECK(available->cursor == Cursor::text);
	CHECK(available->serial == 42);
	CHECK(available->scale == 1);

	const auto changed = cursor.Request(Cursor::hand);
	REQUIRE(changed.has_value());
	CHECK(changed->cursor == Cursor::hand);
	CHECK_FALSE(cursor.Request(Cursor::hand).has_value());
}

TEST_CASE("Wayland cursor state clears stale pointer serials") {
	Scalpel::WaylandCursorState cursor;
	(void)cursor.SetThemeAvailable(true);
	REQUIRE(cursor.Enter(7).has_value());

	cursor.Leave();
	CHECK_FALSE(cursor.Request(Cursor::arrow).has_value());
	const auto reentered = cursor.Enter(8);
	REQUIRE(reentered.has_value());
	CHECK(reentered->serial == 8);
	CHECK(reentered->cursor == Cursor::arrow);

	cursor.ResetPointer();
	CHECK_FALSE(cursor.Request(Cursor::wait).has_value());
	const auto replacement = cursor.Enter(9);
	REQUIRE(replacement.has_value());
	CHECK(replacement->serial == 9);
	CHECK(replacement->cursor == Cursor::wait);
}

TEST_CASE("Wayland cursor state rebuilds scaled themes and hotspots") {
	Scalpel::WaylandCursorState cursor;
	(void)cursor.SetThemeAvailable(true);
	(void)cursor.Enter(24);
	const auto scaled = cursor.SetScale(2);
	REQUIRE(scaled.has_value());
	CHECK(scaled->scale == 2);
	CHECK(Scalpel::CursorThemePixelSize(24, scaled->scale) == 48);
	CHECK(Scalpel::CursorImageGeometry(48, 48, 14, 18, scaled->scale) ==
		Scalpel::WaylandCursorImageGeometry{48, 48, 7, 9});
	CHECK_THROWS_WITH(cursor.SetScale(0), "Wayland cursor scale must be positive");
}

TEST_CASE("Wayland scale follows entered outputs and preferred integer scale") {
	Scalpel::WaylandScaleState scale(801, 601);
	REQUIRE(scale.TakeConfiguration() ==
		Scalpel::WaylandScaleConfiguration{801, 601, 801, 601, 120, 1, 1, false});
	CHECK_FALSE(scale.TakeConfiguration().has_value());

	scale.AddOutput(10);
	scale.AddOutput(20);
	scale.SetOutputScale(10, 2);
	scale.SetOutputScale(20, 3);
	scale.EnterOutput(10);
	REQUIRE(scale.TakeConfiguration()->surfaceBufferScale == 2);
	scale.EnterOutput(20);
	REQUIRE(scale.TakeConfiguration() ==
		Scalpel::WaylandScaleConfiguration{
			801, 601, 2403, 1803, 360, 3, 3, false});

	scale.RemoveOutput(20);
	REQUIRE(scale.TakeConfiguration()->surfaceBufferScale == 2);
	scale.SetPreferredBufferScale(4);
	REQUIRE(scale.TakeConfiguration() ==
		Scalpel::WaylandScaleConfiguration{
			801, 601, 3204, 2404, 480, 4, 4, false});
	scale.LeaveOutput(10);
	CHECK_FALSE(scale.TakeConfiguration().has_value());
}

TEST_CASE("Wayland scale uses fractional scale only with both protocols") {
	Scalpel::WaylandScaleState scale(801, 601);
	(void)scale.TakeConfiguration();
	scale.SetPreferredBufferScale(2);
	REQUIRE(scale.TakeConfiguration()->surfaceBufferScale == 2);
	scale.SetFractionalPreferredScale(150);
	CHECK(scale.Configuration().surfaceBufferScale == 2);

	scale.SetFractionalProtocols(true, false);
	CHECK_FALSE(scale.TakeConfiguration().has_value());
	scale.SetFractionalProtocols(true, true);
	REQUIRE(scale.TakeConfiguration() ==
		Scalpel::WaylandScaleConfiguration{
			801, 601, 1002, 752, 150, 1, 2, true});

	scale.SetFractionalProtocols(false, true);
	REQUIRE(scale.TakeConfiguration()->surfaceBufferScale == 2);
	scale.SetFractionalProtocols(true, true);
	(void)scale.TakeConfiguration();
	scale.ClearFractionalPreferredScale();
	REQUIRE(scale.TakeConfiguration()->surfaceBufferScale == 2);
	scale.SetFractionalPreferredScale(150);
	(void)scale.TakeConfiguration();
	scale.Resize(640, 480);
	REQUIRE(scale.TakeConfiguration() ==
		Scalpel::WaylandScaleConfiguration{
			640, 480, 800, 600, 150, 1, 2, true});
}

TEST_CASE("Wayland scale rejects invalid sizes and scales") {
	CHECK_THROWS(Scalpel::WaylandScaleState(0, 600));
	Scalpel::WaylandScaleState scale(800, 600);
	CHECK_THROWS(scale.SetOutputScale(1, 0));
	CHECK_THROWS(scale.SetPreferredBufferScale(0));
	CHECK_THROWS(scale.SetFractionalPreferredScale(0));
	CHECK_THROWS(scale.Resize(800, 0));
}

TEST_CASE("Wayland scale remains pending until its configuration is applied") {
	Scalpel::WaylandScaleState scale(800, 600);
	const auto initial = scale.PendingConfiguration();
	REQUIRE(initial.has_value());
	CHECK(scale.PendingConfiguration() == initial);

	scale.Resize(640, 480);
	scale.MarkConfigurationApplied(*initial);
	const auto resized = scale.PendingConfiguration();
	REQUIRE(resized.has_value());
	CHECK(resized->logicalWidth == 640);
	CHECK(resized->logicalHeight == 480);

	scale.MarkConfigurationApplied(*resized);
	CHECK_FALSE(scale.PendingConfiguration().has_value());
}

TEST_CASE("Wayland scale falls back when buffer scale is unavailable") {
	Scalpel::WaylandScaleState scale(800, 600);
	(void)scale.TakeConfiguration();
	scale.SetPreferredBufferScale(2);
	REQUIRE(scale.TakeConfiguration()->surfaceBufferScale == 2);

	scale.SetBufferScaleAvailable(false);
	REQUIRE(scale.TakeConfiguration() ==
		Scalpel::WaylandScaleConfiguration{
			800, 600, 800, 600, 120, 1, 1, false});
	scale.SetFractionalPreferredScale(150);
	scale.SetFractionalProtocols(true, true);
	REQUIRE(scale.TakeConfiguration() ==
		Scalpel::WaylandScaleConfiguration{
			800, 600, 1000, 750, 150, 1, 2, true});
}

TEST_CASE("Wayland frame pacing preserves invalidation across waits and paint") {
	Scalpel::WaylandFrameState frame;

	CHECK_FALSE(frame.CanSubmit());
	frame.Invalidate({10, 20, 30, 40});
	CHECK(frame.CanSubmit());
	const auto first = frame.BeginFrame(100, 80, 0, false, true);
	REQUIRE(first.has_value());
	CHECK(first->submissionDamage ==
		std::vector<Scalpel::FrameRectangle>{{10, 20, 30, 40}});
	CHECK(first->repaintDamage ==
		std::vector<Scalpel::FrameRectangle>{{0, 0, 100, 80}});
	CHECK(frame.Painting());

	frame.Invalidate({40, 20, 60, 40});
	CHECK_FALSE(frame.CanSubmit());
	CHECK_FALSE(frame.PrepareFrame(first->submission, false).has_value());
	frame.SubmitFrame(first->submission);
	CHECK(frame.CallbackOutstanding());
	CHECK(frame.Invalidated());
	CHECK_FALSE(frame.CanSubmit());

	frame.Invalidate({60, 20, 80, 40});
	frame.FrameCallbackDone();
	CHECK(frame.CanSubmit());
	const auto second = frame.BeginFrame(100, 80, 1, true, true);
	REQUIRE(second.has_value());
	CHECK(second->submissionDamage ==
		std::vector<Scalpel::FrameRectangle>{
			{40, 20, 60, 40}, {60, 20, 80, 40}});
	CHECK_FALSE(frame.PrepareFrame(second->submission, false).has_value());
	frame.SubmitFrame(second->submission);
	frame.FrameCallbackDone();
	CHECK_FALSE(frame.Invalidated());
	CHECK_FALSE(frame.CanSubmit());
	CHECK_FALSE(frame.BeginFrame(100, 80, 1, true, true).has_value());
}

TEST_CASE("Wayland frame paint cancellation restores captured damage") {
	Scalpel::WaylandFrameState frame;
	frame.Invalidate({4, 5, 20, 30});
	const auto plan = frame.BeginFrame(100, 80, 1, true, true);
	REQUIRE(plan.has_value());

	(void)frame.PrepareFrame(plan->submission, true);
	CHECK(frame.FeedbackOutstanding(plan->submission));
	frame.CancelFrameCallback();
	frame.CancelPaint();
	CHECK_FALSE(frame.Painting());
	CHECK_FALSE(frame.FeedbackOutstanding(plan->submission));
	CHECK(frame.Invalidated());
	CHECK(frame.CanSubmit());
	const auto replacement = frame.BeginFrame(100, 80, 1, true, true);
	REQUIRE(replacement.has_value());
	CHECK(replacement->submissionDamage ==
		std::vector<Scalpel::FrameRectangle>{{4, 5, 20, 30}});
	CHECK_FALSE(frame.PrepareFrame(replacement->submission, false).has_value());
	frame.SubmitFrame(replacement->submission);
	frame.CancelFrameCallback();
	CHECK_FALSE(frame.CallbackOutstanding());
}

TEST_CASE("Wayland frame accepts completion during buffer swap") {
	Scalpel::WaylandFrameState frame;
	frame.Invalidate({4, 5, 20, 30});
	const auto plan = frame.BeginFrame(100, 80, 1, true, true);
	REQUIRE(plan.has_value());

	CHECK_FALSE(frame.PrepareFrame(plan->submission, true).has_value());
	CHECK(frame.CallbackOutstanding());
	CHECK(frame.FeedbackOutstanding(plan->submission));
	frame.FrameCallbackDone();
	frame.Presented(plan->submission, 12, 345, 16'666'667, 99, 3);
	CHECK_FALSE(frame.CallbackOutstanding());
	CHECK_FALSE(frame.FeedbackOutstanding(plan->submission));

	frame.SubmitFrame(plan->submission);
	CHECK_FALSE(frame.CallbackOutstanding());
	const auto reports = frame.TakePresentationResults();
	REQUIRE(reports.size() == 1);
	CHECK(reports.front().submission == plan->submission);
}

TEST_CASE("Wayland frame damage clips and converts coordinate origins") {
	const std::vector<Scalpel::FrameRectangle> damage{
		{-5, 2, 20, 12},
		{90, 70, 110, 90},
		{20, 20, 20, 30},
		{120, 90, 140, 100},
	};
	const auto clipped = Scalpel::ClipFrameDamage(damage, 100, 80);
	CHECK(clipped == std::vector<Scalpel::FrameRectangle>{
		{0, 2, 20, 12}, {90, 70, 100, 80}});
	CHECK(Scalpel::WaylandBufferDamage(clipped) ==
		std::vector<Scalpel::DamageRectangle>{
			{0, 2, 20, 10}, {90, 70, 10, 10}});
	CHECK(Scalpel::EglBufferDamage(clipped, 80) ==
		std::vector<Scalpel::DamageRectangle>{
			{0, 68, 20, 10}, {90, 0, 10, 10}});
	CHECK(Scalpel::ScaleFrameDamage({{10, 5, 30, 20}}, 100, 80, 2) ==
		std::vector<Scalpel::FrameRectangle>{{20, 10, 60, 40}});
	CHECK(Scalpel::ScaleFrameDamageFractional(
		{{1, 1, 3, 3}}, 100, 80, 150, 120) ==
		std::vector<Scalpel::FrameRectangle>{{1, 1, 4, 4}});
	CHECK(Scalpel::ScaleFrameDamageToBuffer(
		{{800, 0, 801, 601}}, 801, 601, 1002, 752) ==
		std::vector<Scalpel::FrameRectangle>{{1000, 0, 1002, 752}});
	CHECK_THROWS(Scalpel::ClipFrameDamage(damage, 0, 80));
	CHECK_THROWS(Scalpel::ScaleFrameDamage(damage, 100, 80, 0));
	CHECK_THROWS(Scalpel::ScaleFrameDamageFractional(
		damage, 100, 80, 150, 0));
	CHECK_THROWS(Scalpel::EglBufferDamage(clipped, 0));
}

TEST_CASE("Wayland frame plans keep paint logical and damage scaled") {
	Scalpel::FramePlan plan;
	plan.submissionDamage = {{1, 1, 3, 3}};
	plan.repaintDamage = {{1, 1, 3, 3}, {7, 5, 9, 7}};
	const Scalpel::FramePlan scaled = Scalpel::ScaleFramePlan(
		std::move(plan), 100, 80, 125, 100);

	CHECK(scaled.submissionDamage ==
		std::vector<Scalpel::FrameRectangle>{{1, 1, 3, 3}});
	CHECK(scaled.repaintDamage ==
		std::vector<Scalpel::FrameRectangle>{
			{1, 1, 3, 3}, {7, 5, 9, 7}});
	CHECK(scaled.waylandDamage ==
		std::vector<Scalpel::DamageRectangle>{{1, 1, 3, 3}});
	CHECK(scaled.eglDamage ==
		std::vector<Scalpel::DamageRectangle>{{1, 91, 11, 8}});
}

TEST_CASE("Wayland frame damage bounds excessive rectangle counts") {
	std::vector<Scalpel::FrameRectangle> damage;
	for (int index = 0; index < 17; ++index) {
		damage.push_back({index, index, index + 1, index + 1});
	}
	CHECK(Scalpel::ClipFrameDamage(damage, 100, 80) ==
		std::vector<Scalpel::FrameRectangle>{{0, 0, 17, 17}});
}

TEST_CASE("Wayland frame buffer age extends repaint damage") {
	Scalpel::WaylandFrameState frame;
	frame.Invalidate({0, 0, 10, 10});
	const auto first = frame.BeginFrame(100, 80, 1, true, true);
	REQUIRE(first.has_value());
	CHECK(first->repaintDamage ==
		std::vector<Scalpel::FrameRectangle>{{0, 0, 10, 10}});
	(void)frame.PrepareFrame(first->submission, false);
	frame.SubmitFrame(first->submission);
	frame.FrameCallbackDone();

	frame.Invalidate({20, 20, 30, 30});
	const auto second = frame.BeginFrame(100, 80, 1, true, true);
	REQUIRE(second.has_value());
	(void)frame.PrepareFrame(second->submission, false);
	frame.SubmitFrame(second->submission);
	frame.FrameCallbackDone();

	frame.Invalidate({40, 40, 50, 50});
	const auto aged = frame.BeginFrame(100, 80, 2, true, true);
	REQUIRE(aged.has_value());
	CHECK(aged->repaintDamage ==
		std::vector<Scalpel::FrameRectangle>{
			{40, 40, 50, 50}, {20, 20, 30, 30}});
	CHECK(aged->eglDamage ==
		std::vector<Scalpel::DamageRectangle>{{20, 30, 30, 30}});
	(void)frame.PrepareFrame(aged->submission, false);
	frame.SubmitFrame(aged->submission);
	frame.FrameCallbackDone();

	frame.Invalidate({60, 60, 70, 70});
	const auto invalidAge = frame.BeginFrame(100, 80, 9, true, true);
	REQUIRE(invalidAge.has_value());
	CHECK(invalidAge->repaintDamage ==
		std::vector<Scalpel::FrameRectangle>{{0, 0, 100, 80}});
	CHECK(frame.DamageHistorySize() == 3);
}

TEST_CASE("Wayland frame buffer resize invalidates damage history") {
	Scalpel::WaylandFrameState frame;
	frame.Invalidate({0, 0, 10, 10});
	const auto original = frame.BeginFrame(100, 80, 1, true, true);
	REQUIRE(original.has_value());
	(void)frame.PrepareFrame(original->submission, false);
	frame.SubmitFrame(original->submission);
	frame.FrameCallbackDone();
	CHECK(frame.DamageHistorySize() == 1);

	frame.Invalidate({100, 80, 110, 90});
	const auto resized = frame.BeginFrame(120, 90, 1, true, true);
	REQUIRE(resized.has_value());
	CHECK(resized->submissionDamage ==
		std::vector<Scalpel::FrameRectangle>{{100, 80, 110, 90}});
	CHECK(resized->repaintDamage ==
		std::vector<Scalpel::FrameRectangle>{{0, 0, 120, 90}});
	CHECK(frame.DamageHistorySize() == 0);
}

TEST_CASE("Wayland frame scale changes reset damage history") {
	Scalpel::WaylandFrameState frame;
	frame.Invalidate({0, 0, 10, 10});
	const auto original = frame.BeginFrame(100, 80, 1, true, true);
	REQUIRE(original.has_value());
	(void)frame.PrepareFrame(original->submission, false);
	frame.SubmitFrame(original->submission);
	frame.FrameCallbackDone();
	REQUIRE(frame.DamageHistorySize() == 1);

	frame.ResetDamageHistory();
	CHECK(frame.DamageHistorySize() == 0);
	frame.Invalidate({20, 20, 30, 30});
	const auto scaled = frame.BeginFrame(100, 80, 2, true, true);
	REQUIRE(scaled.has_value());
	CHECK(scaled->repaintDamage ==
		std::vector<Scalpel::FrameRectangle>{{0, 0, 100, 80}});
}

TEST_CASE("Wayland frame extension fallbacks remain independent") {
	Scalpel::WaylandFrameState frame;
	frame.Invalidate({10, 10, 20, 20});
	const auto noDamageSwap = frame.BeginFrame(100, 80, 1, true, false);
	REQUIRE(noDamageSwap.has_value());
	CHECK(noDamageSwap->repaintDamage ==
		std::vector<Scalpel::FrameRectangle>{{10, 10, 20, 20}});
	CHECK(noDamageSwap->fullSwap);
	(void)frame.PrepareFrame(noDamageSwap->submission, false);
	frame.SubmitFrame(noDamageSwap->submission);
	frame.FrameCallbackDone();

	frame.Invalidate({30, 30, 40, 40});
	const auto noBufferAge = frame.BeginFrame(100, 80, 1, false, true);
	REQUIRE(noBufferAge.has_value());
	CHECK(noBufferAge->repaintDamage ==
		std::vector<Scalpel::FrameRectangle>{{0, 0, 100, 80}});
	CHECK_FALSE(noBufferAge->fullSwap);
}

TEST_CASE("Wayland frame uses full damage when invalidation clips away") {
	Scalpel::WaylandFrameState frame;
	frame.Invalidate({200, 200, 220, 220});
	const auto plan = frame.BeginFrame(100, 80, 1, true, true);
	REQUIRE(plan.has_value());
	CHECK(plan->submissionDamage ==
		std::vector<Scalpel::FrameRectangle>{{0, 0, 100, 80}});
	CHECK(plan->waylandDamage ==
		std::vector<Scalpel::DamageRectangle>{{0, 0, 100, 80}});
}

TEST_CASE("Wayland frame presentation reports match submission identifiers") {
	Scalpel::WaylandFrameState frame;
	auto submit = [&frame](Scalpel::FrameRectangle damage, bool feedback) {
		frame.Invalidate(damage);
		const auto plan = frame.BeginFrame(100, 80, 1, true, true);
		REQUIRE(plan.has_value());
		const uint64_t submission = plan->submission;
		CHECK_FALSE(frame.PrepareFrame(submission, feedback).has_value());
		frame.SubmitFrame(submission);
		frame.FrameCallbackDone();
		return submission;
	};

	const uint64_t first = submit({0, 0, 10, 10}, false);
	CHECK_FALSE(frame.FeedbackOutstanding(first));
	const uint64_t second = submit({10, 10, 20, 20}, true);
	const uint64_t third = submit({20, 20, 30, 30}, true);
	frame.Presented(third, 12, 345, 16'666'667, 99, 3);
	frame.Discarded(second);
	frame.Presented(999, 0, 0, 0, 0, 0);

	const auto reports = frame.TakePresentationResults();
	REQUIRE(reports.size() == 2);
	CHECK(reports[0].submission == third);
	CHECK(reports[0].kind ==
		Scalpel::PresentationResult::Kind::Presented);
	CHECK(reports[0].seconds == 12);
	CHECK(reports[0].nanoseconds == 345);
	CHECK(reports[0].refreshNanoseconds == 16'666'667);
	CHECK(reports[0].sequence == 99);
	CHECK(reports[0].flags == 3);
	CHECK(reports[1].submission == second);
	CHECK(reports[1].kind ==
		Scalpel::PresentationResult::Kind::Discarded);
	CHECK(frame.TakePresentationResults().empty());
}

TEST_CASE("Wayland frame bounds outstanding presentation feedback") {
	Scalpel::WaylandFrameState frame;
	uint64_t first = 0;
	std::optional<uint64_t> expired;
	for (int index = 0; index < 9; ++index) {
		frame.Invalidate({index, index, index + 1, index + 1});
		const auto plan = frame.BeginFrame(100, 80, 1, true, true);
		REQUIRE(plan.has_value());
		if (index == 0) {
			first = plan->submission;
		}
		expired = frame.PrepareFrame(plan->submission, true);
		frame.SubmitFrame(plan->submission);
		frame.FrameCallbackDone();
	}
	REQUIRE(expired.has_value());
	CHECK(*expired == first);
	CHECK_FALSE(frame.FeedbackOutstanding(first));
}

TEST_CASE("Wayland window reports the constructor argument it rejects") {
	CHECK_THROWS_WITH(Scalpel::WaylandWindow(nullptr, 800, 600),
		"WaylandWindow requires a title");
	CHECK_THROWS_WITH(Scalpel::WaylandWindow("scalpel-editor", 0, 600),
		"WaylandLifecycle requires a positive size");
}

TEST_CASE("Wayland lifecycle coalesces configured sizes") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	lifecycle.ProposeSize(900, 650);
	lifecycle.ProposeSize(1024, 768);
	CHECK(lifecycle.CommitConfigure() == Scalpel::WindowSize{1024, 768});
	CHECK(lifecycle.Width() == 1024);
	CHECK(lifecycle.Height() == 768);
	CHECK(lifecycle.TakeResize() == Scalpel::WindowSize{1024, 768});
	CHECK_FALSE(lifecycle.TakeResize().has_value());

	lifecycle.ProposeSize(1100, 700);
	lifecycle.ProposeSize(0, 0);
	CHECK_FALSE(lifecycle.CommitConfigure().has_value());
	lifecycle.ProposeSize(1024, 768);
	CHECK_FALSE(lifecycle.CommitConfigure().has_value());
}

TEST_CASE("Wayland size proposals preserve toplevel state") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	lifecycle.ProposeToplevel(900, 650, {1, 4});
	lifecycle.ProposeSize(1024, 768);
	REQUIRE(lifecycle.CommitConfigure() == Scalpel::WindowSize{1024, 768});
	CHECK(lifecycle.ToplevelState().maximized);
	CHECK(lifecycle.ToplevelState().activated);

	lifecycle.ProposeSize(1100, 700);
	REQUIRE(lifecycle.CommitConfigure() == Scalpel::WindowSize{1100, 700});
	CHECK(lifecycle.ToplevelState().maximized);
	CHECK(lifecycle.ToplevelState().activated);
}

TEST_CASE("Wayland lifecycle commits retained toplevel state") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	lifecycle.ProposeConfigureBounds(1600, 1000);
	CHECK_FALSE(lifecycle.ToplevelState().serverSideDecoration);
	lifecycle.ProposeWmCapabilities({1, 2, 3, 4, 999});
	lifecycle.ProposeDecoration(true);
	lifecycle.ProposeToplevel(1024, 768, {1, 2, 4, 999});
	CHECK_FALSE(lifecycle.ToplevelState().configureBounds.has_value());
	REQUIRE(lifecycle.CommitConfigure() == Scalpel::WindowSize{1024, 768});
	const Scalpel::WaylandToplevelState &configured = lifecycle.ToplevelState();
	CHECK(configured.configureBounds == Scalpel::WindowSize{1600, 1000});
	CHECK(configured.maximized);
	CHECK(configured.fullscreen);
	CHECK_FALSE(configured.resizing);
	CHECK(configured.activated);
	CHECK(configured.windowMenuAvailable);
	CHECK(configured.maximizeAvailable);
	CHECK(configured.fullscreenAvailable);
	CHECK(configured.minimizeAvailable);
	CHECK(configured.serverSideDecoration);

	lifecycle.ProposeConfigureBounds(0, 0);
	lifecycle.ProposeWmCapabilities({});
	lifecycle.ProposeDecoration(false);
	lifecycle.ProposeToplevel(0, 0, {3});
	CHECK_FALSE(lifecycle.CommitConfigure().has_value());
	const Scalpel::WaylandToplevelState &updated = lifecycle.ToplevelState();
	CHECK_FALSE(updated.configureBounds.has_value());
	CHECK_FALSE(updated.maximized);
	CHECK_FALSE(updated.fullscreen);
	CHECK(updated.resizing);
	CHECK_FALSE(updated.activated);
	CHECK_FALSE(updated.windowMenuAvailable);
	CHECK_FALSE(updated.maximizeAvailable);
	CHECK_FALSE(updated.fullscreenAvailable);
	CHECK_FALSE(updated.minimizeAvailable);
	CHECK_FALSE(updated.serverSideDecoration);
}

TEST_CASE("Wayland lifecycle retains a close request") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	CHECK_FALSE(lifecycle.CloseRequested());
	lifecycle.RequestClose();
	CHECK(lifecycle.CloseRequested());
}

TEST_CASE("Wayland registry binds each global only once") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	const auto compositor = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Compositor, 10, 6);
	REQUIRE(compositor.size() == 1);
	CHECK(compositor.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindCompositor, 10, 6});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Compositor, 10, 6).empty());
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Compositor, 11, 6).empty());

	const auto wmBase = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::WmBase, 20, 7);
	REQUIRE(wmBase.size() == 1);
	CHECK(wmBase.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindWmBase, 20, 7});
}

TEST_CASE("Wayland registry replaces a removed decoration manager") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::DecorationManager, 25, 1);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindDecorationManager, 25, 1});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::DecorationManager, 26, 1).empty());

	const auto replacement = lifecycle.RemoveGlobal(25);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleaseDecorationManager, 25});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindDecorationManager, 26, 1});
	lifecycle.ProposeDecoration(true);
	(void)lifecycle.CommitConfigure();
	const auto removed = lifecycle.RemoveGlobal(26);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleaseDecorationManager);
	CHECK(lifecycle.ToplevelState().serverSideDecoration);
}

TEST_CASE("Wayland registry replaces optional shared memory") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::SharedMemory, 27, 1);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindSharedMemory, 27, 1});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::SharedMemory, 28, 1).empty());

	const auto replacement = lifecycle.RemoveGlobal(27);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleaseSharedMemory, 27});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindSharedMemory, 28, 1});
	const auto removed = lifecycle.RemoveGlobal(28);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleaseSharedMemory);
}

TEST_CASE("Wayland registry replaces the optional data device manager") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::DataDeviceManager, 29, 3);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindDataDeviceManager, 29, 3});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::DataDeviceManager, 30, 2).empty());

	const auto replacement = lifecycle.RemoveGlobal(29);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleaseDataDeviceManager, 29});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindDataDeviceManager, 30, 2});
	const auto removed = lifecycle.RemoveGlobal(30);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleaseDataDeviceManager);
}

TEST_CASE("Wayland registry replaces the optional primary selection manager") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::PrimarySelectionManager, 31, 1);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindPrimarySelectionManager, 31, 1});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::PrimarySelectionManager, 32, 1).empty());

	const auto replacement = lifecycle.RemoveGlobal(31);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleasePrimarySelectionManager, 31});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindPrimarySelectionManager, 32, 1});
	const auto removed = lifecycle.RemoveGlobal(32);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleasePrimarySelectionManager);
}

TEST_CASE("Wayland registry replaces the optional text input manager") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::TextInputManager, 33, 1);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindTextInputManager, 33, 1});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::TextInputManager, 34, 1).empty());

	const auto replacement = lifecycle.RemoveGlobal(33);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleaseTextInputManager, 33});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindTextInputManager, 34, 1});
	const auto removed = lifecycle.RemoveGlobal(34);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleaseTextInputManager);
}

TEST_CASE("Wayland registry treats presentation timing as optional") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	CHECK(lifecycle.RemoveGlobal(35).empty());
	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Presentation, 35, 2);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindPresentation, 35, 2});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Presentation, 36, 1).empty());

	const auto replacement = lifecycle.RemoveGlobal(35);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleasePresentation, 35});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindPresentation, 36, 1});
	const auto removed = lifecycle.RemoveGlobal(36);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleasePresentation);

	const auto fresh = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Presentation, 37, 2);
	REQUIRE(fresh.size() == 1);
	CHECK(fresh.front().type ==
		Scalpel::WaylandLifecycleActionType::BindPresentation);
}

TEST_CASE("Wayland registry replaces optional scale managers") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	REQUIRE(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Viewporter, 40, 1).front() ==
		Scalpel::WaylandLifecycleAction{
			Scalpel::WaylandLifecycleActionType::BindViewporter, 40, 1});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Viewporter, 41, 1).empty());
	const auto viewporterReplacement = lifecycle.RemoveGlobal(40);
	REQUIRE(viewporterReplacement.size() == 2);
	CHECK(viewporterReplacement[0].type ==
		Scalpel::WaylandLifecycleActionType::ReleaseViewporter);
	CHECK(viewporterReplacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindViewporter, 41, 1});

	REQUIRE(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::FractionalScaleManager, 42, 1).front() ==
		Scalpel::WaylandLifecycleAction{
			Scalpel::WaylandLifecycleActionType::BindFractionalScaleManager,
			42, 1});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::FractionalScaleManager, 43, 1).empty());
	const auto fractionalReplacement = lifecycle.RemoveGlobal(42);
	REQUIRE(fractionalReplacement.size() == 2);
	CHECK(fractionalReplacement[0].type ==
		Scalpel::WaylandLifecycleActionType::ReleaseFractionalScaleManager);
	CHECK(fractionalReplacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindFractionalScaleManager,
		43, 1});
}

TEST_CASE("Wayland registry closes when an active required global disappears") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Compositor, 10, 4);
	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Compositor, 11, 4);

	CHECK(lifecycle.RemoveGlobal(999).empty());
	CHECK(lifecycle.RemoveGlobal(11).empty());
	CHECK_FALSE(lifecycle.CloseRequested());
	const auto actions = lifecycle.RemoveGlobal(10);
	REQUIRE(actions.size() == 1);
	CHECK(actions.front().type == Scalpel::WaylandLifecycleActionType::Close);
	CHECK(lifecycle.CloseRequested());
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Compositor, 12, 4).empty());
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Output, 30, 4).empty());
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Seat, 40, 9).empty());
	CHECK(lifecycle.OutputCount() == 0);
	CHECK(lifecycle.SeatCount() == 0);
}

TEST_CASE("Wayland lifecycle tracks hot-plugged output membership") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	CHECK(lifecycle.OutputCount() == 0);
	CHECK(lifecycle.EnteredOutputCount() == 0);

	const auto first = lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Output, 30, 4);
	const auto second = lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Output, 31, 2);
	REQUIRE(first.size() == 1);
	REQUIRE(second.size() == 1);
	CHECK(first.front().type == Scalpel::WaylandLifecycleActionType::BindOutput);
	CHECK(second.front().type == Scalpel::WaylandLifecycleActionType::BindOutput);
	CHECK(lifecycle.OutputCount() == 2);

	lifecycle.EnterOutput(30);
	lifecycle.EnterOutput(31);
	lifecycle.EnterOutput(999);
	CHECK(lifecycle.OutputEntered(30));
	CHECK(lifecycle.OutputEntered(31));
	CHECK(lifecycle.EnteredOutputCount() == 2);
	lifecycle.LeaveOutput(30);
	CHECK_FALSE(lifecycle.OutputEntered(30));
	CHECK(lifecycle.EnteredOutputCount() == 1);

	const auto removed = lifecycle.RemoveGlobal(31);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleaseOutput, 31});
	CHECK(lifecycle.OutputCount() == 1);
	CHECK(lifecycle.EnteredOutputCount() == 0);
}

TEST_CASE("Wayland lifecycle selects one hot-plugged seat") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	CHECK(lifecycle.SeatCount() == 0);
	CHECK_FALSE(lifecycle.ActiveSeat().has_value());

	const auto first = lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 40, 9);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindSeat, 40, 9});
	CHECK(lifecycle.ActiveSeat() == 40);
	CHECK(lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 41, 7).empty());
	CHECK(lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 41, 7).empty());
	CHECK(lifecycle.SeatCount() == 2);
	CHECK(lifecycle.ActiveSeat() == 40);

	CHECK(lifecycle.RemoveGlobal(41).empty());
	CHECK(lifecycle.SeatCount() == 1);
	CHECK(lifecycle.ActiveSeat() == 40);
}

TEST_CASE("Wayland lifecycle recreates devices after capability changes") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 40, 9);

	const auto added = lifecycle.UpdateSeatCapabilities(40, true, true);
	REQUIRE(added.size() == 2);
	CHECK(added[0].type == Scalpel::WaylandLifecycleActionType::CreatePointer);
	CHECK(added[1].type == Scalpel::WaylandLifecycleActionType::CreateKeyboard);
	CHECK(lifecycle.PointerActive());
	CHECK(lifecycle.KeyboardActive());
	CHECK(lifecycle.UpdateSeatCapabilities(40, true, true).empty());
	CHECK(lifecycle.UpdateSeatCapabilities(999, false, false).empty());

	const auto removed = lifecycle.UpdateSeatCapabilities(40, false, false);
	REQUIRE(removed.size() == 2);
	CHECK(removed[0].type == Scalpel::WaylandLifecycleActionType::ReleasePointer);
	CHECK(removed[1].type == Scalpel::WaylandLifecycleActionType::ReleaseKeyboard);
	CHECK_FALSE(lifecycle.PointerActive());
	CHECK_FALSE(lifecycle.KeyboardActive());

	const auto regained = lifecycle.UpdateSeatCapabilities(40, true, true);
	REQUIRE(regained.size() == 2);
	CHECK(regained[0].type == Scalpel::WaylandLifecycleActionType::CreatePointer);
	CHECK(regained[1].type == Scalpel::WaylandLifecycleActionType::CreateKeyboard);
}

TEST_CASE("Wayland lifecycle promotes a fresh seat after active removal") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 40, 9);
	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 41, 7);
	(void)lifecycle.UpdateSeatCapabilities(40, true, true);

	const auto actions = lifecycle.RemoveGlobal(40);
	REQUIRE(actions.size() == 4);
	CHECK(actions[0].type == Scalpel::WaylandLifecycleActionType::ReleasePointer);
	CHECK(actions[1].type == Scalpel::WaylandLifecycleActionType::ReleaseKeyboard);
	CHECK(actions[2].type == Scalpel::WaylandLifecycleActionType::ReleaseSeat);
	CHECK(actions[3] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindSeat, 41, 7});
	CHECK(lifecycle.ActiveSeat() == 41);
	CHECK_FALSE(lifecycle.PointerActive());
	CHECK_FALSE(lifecycle.KeyboardActive());

	const auto last = lifecycle.RemoveGlobal(41);
	REQUIRE(last.size() == 1);
	CHECK(last.front().type == Scalpel::WaylandLifecycleActionType::ReleaseSeat);
	CHECK_FALSE(lifecycle.ActiveSeat().has_value());
	CHECK(lifecycle.SeatCount() == 0);

	const auto replacement = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Seat, 40, 5);
	REQUIRE(replacement.size() == 1);
	CHECK(replacement.front().type == Scalpel::WaylandLifecycleActionType::BindSeat);
}
