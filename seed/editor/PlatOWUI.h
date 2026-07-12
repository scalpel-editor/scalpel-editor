#pragma once

#include <OnlyWayUi/Core/Vector2.h>
#include <functional>

namespace OnlyWayUi {

class RenderManager;

namespace Editor {

// Passed through Scintilla's opaque SurfaceID and WindowID values. This target and its render manager must outlive the initialized surface.
struct SurfaceTarget {
	RenderManager* render_manager = nullptr;
	Vector2f origin = {};
	Vector2f size = {};
	int pixels_per_inch = 96;
	std::function<void(float, float, float, float)> invalidate_rectangle;
};

} // namespace Editor
} // namespace OnlyWayUi
