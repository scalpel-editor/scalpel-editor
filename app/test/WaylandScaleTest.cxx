#include "WaylandLifecycleTest.h"

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
