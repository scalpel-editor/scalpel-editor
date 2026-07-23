#include "catch.hpp"

#include "WaylandLifecycle.h"

TEST_CASE("Wayland portal parent follows the current export") {
	Scalpel::WaylandPortalParentState parent;
	CHECK_FALSE(parent.ExportActive());
	CHECK(parent.ParentHandle().empty());

	parent.BeginExport(10);
	CHECK(parent.ExportActive());
	parent.DeliverHandle(10, "first");
	CHECK(parent.ParentHandle() == "wayland:first");

	parent.BeginExport(20);
	CHECK(parent.ParentHandle().empty());
	parent.DeliverHandle(10, "stale");
	CHECK(parent.ParentHandle().empty());
	parent.DeliverHandle(20, "second");
	CHECK(parent.ParentHandle() == "wayland:second");

	parent.EndExport(10);
	CHECK(parent.ParentHandle() == "wayland:second");
	parent.EndExport(20);
	CHECK_FALSE(parent.ExportActive());
	CHECK(parent.ParentHandle().empty());
	CHECK_THROWS(parent.BeginExport(0));
}
