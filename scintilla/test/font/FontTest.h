#pragma once

// Font selection, ownership, rasterization, measurement, and shaped-run tests.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>

#include "EditorStyleTypes.h"
#include "FontPlatform.h"
#include "Geometry.h"
#include "Platform.h"
#include "ShapedLayout.h"
#include "ShapedRun.h"

#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

inline const std::filesystem::path fontDirectory = SCALPEL_TEST_FONT_DIR;
inline const std::filesystem::path primaryPath =
	fontDirectory / "FallbackPrimary.ttf";
inline const std::filesystem::path snowmanPath =
	fontDirectory / "FallbackSnowman.ttf";

inline std::shared_ptr<FontFace> LoadPrimary(
	FontCache &cache, double size = 16.0) {
	return cache.LoadPath(primaryPath, FontParameters("fixture", size));
}

inline std::shared_ptr<FontFace> LoadSnowman(
	FontCache &cache, double size = 16.0) {
	return cache.LoadPath(snowmanPath, FontParameters("fixture", size));
}

inline bool MonotonicEnds(const std::vector<XYPOSITION> &ends) {
	for (size_t index = 1; index < ends.size(); index++) {
		if (ends[index] + 1e-9 < ends[index - 1]) {
			return false;
		}
	}
	return true;
}

}
