#include "WaylandLifecycleTest.h"

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
