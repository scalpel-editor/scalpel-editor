#include "WaylandLifecycleTest.h"

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
