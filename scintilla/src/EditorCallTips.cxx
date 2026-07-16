// Scintilla source code edit control
/** @file EditorCallTips.cxx
 ** Call tip concern for ScintillaBase: show and cancel the tip window,
 ** highlight and colours, and click notification.
 **/
// Copyright 1998-2003 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cmath>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <algorithm>
#include <memory>

#include "ScintillaTypes.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "CharacterCategoryMap.h"

#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "CallTip.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "AutoComplete.h"
#include "ScintillaBase.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

// Shows a call tip aligned to document position pos. definition may contain
// multiple lines separated by '\n'. Characters '\001' and '\002' draw small
// up and down arrows for overload cycling. The caret position is remembered so
// backspace past that point cancels the tip. Showing a call tip cancels any
// active autocomplete list.
void ScintillaBase::CallTipShow(Sci::Position pos, const char *definition) {
	ac.Cancel();
	CallTipShow(LocationFromPosition(pos), definition);
}

// Places the tip window near pt using definition text. Uses STYLE_CALLTIP when
// CallTipUseStyle has been set; otherwise STYLE_DEFAULT for the font. Adjusts
// above or below the line so the tip stays inside the client area when it fits.
void ScintillaBase::CallTipShow(Point pt, const char *defn) {
	// If container knows about StyleCallTip then use it in place of the
	// StyleDefault for the face name, size and character set. Also use it
	// for the foreground and background colour.
	const int ctStyle = ct.UseStyleCallTip() ? StyleCallTip : StyleDefault;
	const Style &style = vs.styles[ctStyle];
	if (ct.UseStyleCallTip()) {
		ct.SetForeBack(style.fore, style.back);
	}
	if (wMargin.Created()) {
		pt = pt + GetVisibleOriginInMain();
	}
	AutoSurface surfaceMeasure(this);
	PRectangle rc = ct.CallTipStart(sel.MainCaret(), pt,
		vs.lineHeight,
		defn,
		surfaceMeasure,
		style.font);
	// If the call-tip window would be out of the client
	// space
	const PRectangle rcClient = GetClientRectangle();
	const int offset = vs.lineHeight + static_cast<int>(rc.Height());
	// adjust so it displays above the text.
	if (rc.bottom > rcClient.bottom && rc.Height() < rcClient.Height()) {
		rc.top -= offset;
		rc.bottom -= offset;
	}
	// adjust so it displays below the text.
	if (rc.top < rcClient.top && rc.Height() < rcClient.Height()) {
		rc.top += offset;
		rc.bottom += offset;
	}
	// Now display the window.
	CreateCallTipWindow(rc);
	ct.wCallTip.SetPositionRelative(rc, &wMain);
	ct.wCallTip.Show();
	ct.wCallTip.InvalidateAll();
}

// Report which call-tip region was clicked: 1 is the up arrow, 2 is the down
// arrow, and 0 is the body. The host may use arrows to choose another overload.
void ScintillaBase::CallTipClick() {
	NotificationData scn = {};
	scn.code = Notification::CallTipClick;
	scn.position = ct.clickPlace;
	NotifyParent(scn);
}

void ScintillaBase::CallTipCancel() {
	ct.CallTipCancel();
}

bool ScintillaBase::CallTipActive() const noexcept {
	return ct.inCallTipMode;
}

// Document position remembered when the tip was shown.
Sci::Position ScintillaBase::CallTipPosStart() const noexcept {
	return ct.posStartCallTip;
}

void ScintillaBase::CallTipSetPosStart(Sci::Position posStart) {
	ct.posStartCallTip = posStart;
}

// Highlights [highlightStart, highlightEnd) within the tip text, typically the
// current parameter. End must be greater than start.
void ScintillaBase::CallTipSetHlt(Sci::Position highlightStart, Sci::Position highlightEnd) {
	ct.SetHighlight(static_cast<size_t>(highlightStart), static_cast<size_t>(highlightEnd));
}

// Background colour for the tip and STYLE_CALLTIP.
void ScintillaBase::CallTipSetBack(ColourRGBA back) {
	ct.colourBG = back;
	vs.styles[StyleCallTip].back = ct.colourBG;
	InvalidateStyleRedraw();
}

// Unhighlighted tip text colour and STYLE_CALLTIP foreground.
void ScintillaBase::CallTipSetFore(ColourRGBA fore) {
	ct.colourUnSel = fore;
	vs.styles[StyleCallTip].fore = ct.colourUnSel;
	InvalidateStyleRedraw();
}

// Highlighted tip text colour.
void ScintillaBase::CallTipSetForeHlt(ColourRGBA fore) {
	ct.colourSel = fore;
	InvalidateStyleRedraw();
}

// Switches the tip to STYLE_CALLTIP and sets tab expansion width in pixels.
// Tab sizes below 1 leave Tab as an ordinary character.
void ScintillaBase::CallTipUseStyle(int tabSize) {
	ct.SetTabSize(tabSize);
	InvalidateStyleRedraw();
}

// When above is true the tip appears above the line; otherwise below (default).
void ScintillaBase::CallTipSetPosition(bool above) {
	ct.SetPosition(above);
	InvalidateStyleRedraw();
}
