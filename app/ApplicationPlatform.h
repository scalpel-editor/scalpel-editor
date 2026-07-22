// scalpel-editor application-owned state behind Scintilla's Window handle.

#ifndef APPLICATIONPLATFORM_H
#define APPLICATIONPLATFORM_H

#include <vector>

#include "Platform.h"

namespace Scalpel {

struct ApplicationWindow {
	Scintilla::Internal::PRectangle rectangle{};
	Scintilla::Internal::Window::Cursor cursor = Scintilla::Internal::Window::Cursor::invalid;
	bool visible = false;
	bool mouseCaptured = false;
	std::vector<Scintilla::Internal::PRectangle> invalidatedRectangles;
	int invalidateAllCount = 0;
};

}

#endif
