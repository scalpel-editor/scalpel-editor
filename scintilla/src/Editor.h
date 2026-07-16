// Scintilla source code edit control
/** @file Editor.h
 ** Defines the main editor class.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef EDITOR_H
#define EDITOR_H

#include "EditorCommands.h"

namespace Scintilla::Internal {

/**
 */
class Timer {
public:
	bool ticking;
	int ticksToWait;
	static constexpr int tickSize = 100;
	TickerID tickerID;

	Timer() noexcept;
};

/**
 */
class Idler {
public:
	bool state;
	IdlerID idlerID;

	Idler() noexcept;
};

/**
 * When platform has a way to generate an event before painting,
 * accumulate needed styling range and other work items in
 * WorkNeeded to avoid unnecessary work inside paint handler
 */

enum class WorkItems {
	none = 0,
	style = 1,
	updateUI = 2
};

class WorkNeeded {
public:
	enum WorkItems items;
	Sci::Position upTo;

	WorkNeeded() noexcept : items(WorkItems::none), upTo(0) {}
	void Reset() noexcept {
		items = WorkItems::none;
		upTo = 0;
	}
	void Need(WorkItems items_, Sci::Position pos) noexcept {
		if (Scintilla::FlagSet(items_, WorkItems::style) && (upTo < pos))
			upTo = pos;
		items = static_cast<WorkItems>(static_cast<int>(items) | static_cast<int>(items_));
	}
};

/**
 * Hold a piece of text selected for copying or dragging, along with selection format information.
 * Text is always UTF-8 (same as the document).
 */
class SelectionText {
	std::string s;
public:
	bool rectangular;
	bool lineCopy;
	SelectionText() noexcept : rectangular(false), lineCopy(false) {}
	void Clear() noexcept {
		s.clear();
		rectangular = false;
		lineCopy = false;
	}
	void Copy(const std::string &s_, bool rectangular_, bool lineCopy_) {
		s = s_;
		rectangular = rectangular_;
		lineCopy = lineCopy_;
		FixSelectionForClipboard();
	}
	void Copy(const SelectionText &other) {
		Copy(other.s, other.rectangular, other.lineCopy);
	}
	const char *Data() const noexcept {
		return s.c_str();
	}
	size_t Length() const noexcept {
		return s.length();
	}
	std::string_view AsView() const noexcept {
		return std::string_view(s);
	}
	size_t LengthWithTerminator() const noexcept {
		return s.length() + 1;
	}
	std::string_view AsViewWithTerminator() const noexcept {
		return std::string_view(s.c_str(), s.length()+1);
	}
	bool Empty() const noexcept {
		return s.empty();
	}
private:
	void FixSelectionForClipboard() {
		// To avoid truncating the contents of the clipboard when pasted where the
		// clipboard contains NUL characters, replace NUL characters by spaces.
		std::replace(s.begin(), s.end(), '\0', ' ');
	}
};

struct WrapPending {
	// The range of lines that need to be wrapped
	enum { lineLarge = 0x7ffffff };
	Sci::Line start;	// When there are wraps pending, will be in document range
	Sci::Line end;	// May be lineLarge to indicate all of document after start
	WrapPending() noexcept {
		start = lineLarge;
		end = lineLarge;
	}
	void Reset() noexcept {
		start = lineLarge;
		end = lineLarge;
	}
	void Wrapped(Sci::Line line) noexcept {
		if (start == line)
			start++;
	}
	bool NeedsWrap() const noexcept {
		return start < end;
	}
	bool AddRange(Sci::Line lineStart, Sci::Line lineEnd) noexcept {
		const bool neededWrap = NeedsWrap();
		bool changed = false;
		if (start > lineStart) {
			start = lineStart;
			changed = true;
		}
		if ((end < lineEnd) || !neededWrap) {
			end = lineEnd;
			changed = true;
		}
		return changed;
	}
};

struct CaretPolicySlop {
	Scintilla::CaretPolicy policy;	// Combination from CaretPolicy::Slop, CaretPolicy::Strict, CaretPolicy::Jumps, CaretPolicy::Even
	int slop;	// Pixels for X, lines for Y
	CaretPolicySlop(Scintilla::CaretPolicy policy_, intptr_t slop_) noexcept :
		policy(policy_), slop(static_cast<int>(slop_)) {}
	CaretPolicySlop(uintptr_t policy_=0, intptr_t slop_=0) noexcept :
		policy(static_cast<Scintilla::CaretPolicy>(policy_)), slop(static_cast<int>(slop_)) {}
};

struct CaretPolicies {
	CaretPolicySlop x;
	CaretPolicySlop y;
};

struct VisiblePolicySlop {
	Scintilla::VisiblePolicy policy;	// Combination from VisiblePolicy::Slop, VisiblePolicy::Strict
	int slop;	// Pixels for X, lines for Y
	VisiblePolicySlop(uintptr_t policy_ = 0, intptr_t slop_ = 0) noexcept :
		policy(static_cast<Scintilla::VisiblePolicy>(policy_)), slop(static_cast<int>(slop_)) {}
};

enum class XYScrollOptions {
	none = 0x0,
	useMargin = 0x1,
	vertical = 0x2,
	horizontal = 0x4,
	all = useMargin | vertical | horizontal
};

constexpr XYScrollOptions operator|(XYScrollOptions a, XYScrollOptions b) noexcept {
	return static_cast<XYScrollOptions>(static_cast<int>(a) | static_cast<int>(b));
}

/**
 */
class Editor : public EditModel, public DocWatcher {
protected:	// ScintillaBase subclass needs access to much of Editor

	/** On GTK+, Scintilla is a container widget holding two scroll bars
	 * whereas on Windows there is just one window with both scroll bars turned on. */
	Window wMain;	///< The Scintilla parent window
	Window wMargin;	///< May be separate when using a scroll view for wMain

	// Optimization that avoids superfluous invalidations
	bool redrawPendingText = false;
	bool redrawPendingMargin = false;

	/** Style resources may be expensive to allocate so are cached between uses.
	 * When a style attribute is changed, this cache is flushed. */
	bool stylesValid;
	ViewStyle vs;
	Scintilla::Technology technology;
	Point sizeRGBAImage;
	float scaleRGBAImage;

	MarginView marginView;
	EditView view;

	Scintilla::CursorShape cursorMode;

	bool mouseDownCaptures;
	bool mouseWheelCaptures;

	int xCaretMargin;	///< Ensure this many pixels visible on both sides of caret
	bool horizontalScrollBarVisible;
	int scrollWidth;
	bool verticalScrollBarVisible;
	bool endAtLastLine;
	Scintilla::CaretSticky caretSticky;
	Scintilla::MarginOption marginOptions;
	bool mouseSelectionRectangularSwitch;
	bool multipleSelection;
	bool additionalSelectionTyping;
	Scintilla::MultiPaste multiPasteMode;

	Scintilla::VirtualSpace virtualSpaceOptions;

	KeyMap kmap;

	Timer timer;
	Timer autoScrollTimer;
	static constexpr int autoScrollDelay = 200;

	Idler idler;

	Point lastClick;
	unsigned int lastClickTime;
	Point doubleClickCloseThreshold;
	int dwellDelay;
	int ticksToDwell;
	bool dwelling;
	enum class TextUnit { character, word, subLine, wholeLine } selectionUnit;
	Point ptMouseLast;
	bool dragDropEnabled;
	enum class DragDrop { none, initial, dragging } inDragDrop;
	bool dropWentOutside;
	SelectionPosition posDrop;
	Sci::Position hotSpotClickPos;
	int lastXChosen;
	Sci::Position lineAnchorPos;
	Sci::Position originalAnchorPos;
	Sci::Position wordSelectAnchorStartPos;
	Sci::Position wordSelectAnchorEndPos;
	Sci::Position wordSelectInitialCaretPos;
	SelectionSegment targetRange;
	Scintilla::FindOption searchFlags;
	Sci::Line topLine;
	Sci::Position posTopLine;

	Scintilla::Update needUpdateUI;

	enum class PaintState { notPainting, painting, abandoned } paintState;
	bool paintAbandonedByStyling;
	PRectangle rcPaint;
	bool paintingAllText;
	bool willRedrawAll;
	WorkNeeded workNeeded;
	Scintilla::IdleStyling idleStyling;
	bool needIdleStyling;

	Scintilla::ModificationFlags modEventMask;
	bool commandEvents;
	Scintilla::Status errorStatus;

	SelectionText drag;

	CaretPolicies caretPolicies;

	VisiblePolicySlop visiblePolicy;

	Sci::Position searchAnchor;

	bool recordingMacro;

	Scintilla::AutomaticFold foldAutomatic;

	// Wrapping support
	WrapPending wrapPending;
	ActionDuration durationWrapOneByte;
	bool insideWrapScroll;
	struct LineDocSub {
		Scintilla::Line lineDoc = 0;
		Scintilla::Line subLine = 0;
	};
	std::optional<LineDocSub> scrollToAfterWrap;

	bool convertPastes;

	Editor();
	// Deleted so Editor objects can not be copied.
	Editor(const Editor &) = delete;
	Editor(Editor &&) = delete;
	Editor &operator=(const Editor &) = delete;
	Editor &operator=(Editor &&) = delete;
	// ~Editor() in public section
	virtual void Initialise() = 0;
	virtual void Finalise();

	void InvalidateStyleData() noexcept;
	void InvalidateStyleRedraw();
	void RefreshStyleData();
	void SetRepresentations();
	void DropGraphics() noexcept;

	bool HasMarginWindow() const noexcept;
	// The top left visible point in main window coordinates. Will be 0,0 except for
	// scroll views where it will be equivalent to the current scroll position.
	Point GetVisibleOriginInMain() const override;
	PointDocument DocumentPointFromView(Point ptView) const;  // Convert a point from view space to document
	Sci::Line TopLineOfMain() const noexcept final;   // Return the line at Main's y coordinate 0
	virtual Point ClientSize() const;
	virtual PRectangle GetClientRectangle() const;
	virtual PRectangle GetClientDrawingRectangle();
	PRectangle GetTextRectangle() const;

	Sci::Line LinesOnScreen() const override;
	Sci::Line LinesToScroll() const;
	Sci::Line MaxScrollPos() const;
	SelectionPosition ClampPositionIntoDocument(SelectionPosition sp) const;
	Point LocationFromPosition(SelectionPosition pos, PointEnd pe=PointEnd::start);
	Point LocationFromPosition(Sci::Position pos, PointEnd pe=PointEnd::start);
	int XFromPosition(SelectionPosition sp);
	SelectionPosition SPositionFromLocation(Point pt, bool canReturnInvalid=false, bool charPosition=false, bool virtualSpace=true);
	Sci::Position PositionFromLocation(Point pt, bool canReturnInvalid = false, bool charPosition = false);
	SelectionPosition SPositionFromLineX(Sci::Line lineDoc, int x);
	Sci::Position PositionFromLineX(Sci::Line lineDoc, int x);
	Sci::Line LineFromLocation(Point pt) const noexcept;
	void SetTopLine(Sci::Line topLineNew);

	virtual bool AbandonPaint();
	virtual void RedrawRect(PRectangle rc);
	virtual void DiscardOverdraw();
	virtual void Redraw();
	void RedrawSelMargin(Sci::Line line=-1, bool allAfter=false);
	PRectangle RectangleFromRange(Range r, int overlap);
	void InvalidateRange(Sci::Position start, Sci::Position end);

	bool UserVirtualSpace() const noexcept {
		return (FlagSet(virtualSpaceOptions, Scintilla::VirtualSpace::UserAccessible));
	}
	Sci::Position CurrentPosition() const noexcept;
	bool SelectionEmpty() const noexcept;
	SelectionPosition SelectionStart() noexcept;
	SelectionPosition SelectionEnd() noexcept;
	void SetRectangularRange();
	void ThinRectangularRange();
	void InvalidateSelection(SelectionRange newMain, bool invalidateWholeSelection=false);
	void InvalidateWholeSelection();
	SelectionRange LineSelectionRange(SelectionPosition currentPos_, SelectionPosition anchor_) const noexcept;
	// Selection model: definitions in EditorSelection.cxx.
	void SetSelection(SelectionPosition currentPos_, SelectionPosition anchor_);
	void SetSelection(Sci::Position currentPos_, Sci::Position anchor_);
	void SetSelection(SelectionPosition currentPos_);
	void SetEmptySelection(SelectionPosition currentPos_);
	void SetEmptySelection(Sci::Position currentPos_);
	void SetStreamSelection(Sci::Position caret, Sci::Position anchor);
	void AddSelection(Sci::Position caret, Sci::Position anchor);
	void SetMainSelection(size_t selection);
	size_t GetMainSelection() const noexcept;
	void HideSelection(bool hide);
	bool GetSelectionHidden() const noexcept;
	void TargetFromSelection();
	bool SelectionIsRectangle() const noexcept;
	Scintilla::SelectionMode GetSelectionMode() const noexcept;
	void SetMoveExtendsSelection(bool moveExtends);
	bool GetMoveExtendsSelection() const noexcept;
	void SetMouseSelectionRectangularSwitch(bool enable);
	bool GetMouseSelectionRectangularSwitch() const noexcept;
	void SetMultipleSelection(bool enable);
	bool GetMultipleSelection() const noexcept;
	void SetAdditionalSelectionTyping(bool enable);
	bool GetAdditionalSelectionTyping() const noexcept;
	size_t GetSelections() const noexcept;
	void ClearSelections();
	void SetSelectionLayer(Scintilla::Layer layer);
	Scintilla::Layer GetSelectionLayer() const noexcept;
	void SetUndoSelectionHistory(Scintilla::UndoSelectionHistoryOption option);
	Scintilla::UndoSelectionHistoryOption GetUndoSelectionHistory() const noexcept;
	void SetSelectionSerialized(std::string_view serialized);
	std::string GetSelectionSerialized() const;
	void SetRectangularSelectionCaret(Sci::Position pos);
	Sci::Position GetRectangularSelectionCaret() noexcept;
	void SetRectangularSelectionAnchor(Sci::Position pos);
	Sci::Position GetRectangularSelectionAnchor() noexcept;
	void SetRectangularSelectionCaretVirtualSpace(Sci::Position space);
	Sci::Position GetRectangularSelectionCaretVirtualSpace() noexcept;
	void SetRectangularSelectionAnchorVirtualSpace(Sci::Position space);
	Sci::Position GetRectangularSelectionAnchorVirtualSpace() noexcept;
	Sci::Position GetSelectionNCaret(size_t selection) const noexcept;
	Sci::Position GetSelectionNAnchor(size_t selection) const noexcept;
	Sci::Position GetSelectionNCaretVirtualSpace(size_t selection) const noexcept;
	Sci::Position GetSelectionNAnchorVirtualSpace(size_t selection) const noexcept;
	Sci::Position GetSelectionNStart(size_t selection) const noexcept;
	Sci::Position GetSelectionNStartVirtualSpace(size_t selection) const noexcept;
	Sci::Position GetSelectionNEnd(size_t selection) const noexcept;
	Sci::Position GetSelectionNEndVirtualSpace(size_t selection) const noexcept;
	enum class AddNumber { one, each };
	void MultipleSelectAdd(AddNumber addNumber);
	bool RangeContainsProtected(Sci::Position start, Sci::Position end) const noexcept;
	bool RangeContainsProtected(const SelectionRange &range) const noexcept;
	bool SelectionContainsProtected() const noexcept;
	Sci::Position MovePositionOutsideChar(Sci::Position pos, Sci::Position moveDir, bool checkLineEnd=true) const;
	SelectionPosition MovePositionOutsideChar(SelectionPosition pos, Sci::Position moveDir, bool checkLineEnd=true) const;
	void MovedCaret(SelectionPosition newPos, SelectionPosition previousPos,
		bool ensureVisible, CaretPolicies policies);
	void MovePositionTo(SelectionPosition newPos, Selection::SelTypes selt=Selection::SelTypes::none, bool ensureVisible=true);
	void MovePositionTo(Sci::Position newPos, Selection::SelTypes selt=Selection::SelTypes::none, bool ensureVisible=true);
	SelectionPosition MovePositionSoVisible(SelectionPosition pos, int moveDir);
	SelectionPosition MovePositionSoVisible(Sci::Position pos, int moveDir);
	Point PointMainCaret();
	void SetLastXChosen();
	void RememberSelectionForUndo(int index);
	void RememberSelectionOntoStack(int index);
	void RememberCurrentSelectionForRedoOntoStack();

	void ScrollTo(Sci::Line line, bool moveThumb=true);
	virtual void ScrollText(Sci::Line linesToMove);
	void HorizontalScrollTo(int xPos);
	// Caret: definitions in EditorCaret.cxx.
	void VerticalCentreCaret();
	void ScrollCaret();
	void ChooseCaretX();
	int GetCaretPeriod() const noexcept;
	void SetCaretPeriod(int periodMilliseconds);
	void SetCaretSticky(Scintilla::CaretSticky sticky);
	Scintilla::CaretSticky GetCaretSticky() const noexcept;
	void ToggleCaretSticky();
	bool GetCaretLineVisible() const noexcept;
	void SetCaretLineVisible(bool show);
	bool GetCaretLineVisibleAlways() const noexcept;
	void SetCaretLineVisibleAlways(bool alwaysShow);
	bool GetCaretLineHighlightSubLine() const noexcept;
	void SetCaretLineHighlightSubLine(bool subLine);
	int GetCaretLineFrame() const noexcept;
	void SetCaretLineFrame(int width);
	int GetCaretLineBack() const noexcept;
	void SetCaretLineBack(int rgb);
	Scintilla::Layer GetCaretLineLayer() const noexcept;
	void SetCaretLineLayer(Scintilla::Layer layer);
	int GetCaretLineBackAlpha() const noexcept;
	void SetCaretLineBackAlpha(int alpha);
	void SetXCaretPolicy(Scintilla::uptr_t policy, Scintilla::sptr_t slop);
	void SetYCaretPolicy(Scintilla::uptr_t policy, Scintilla::sptr_t slop);
	void SetCaretFore(int rgb);
	int GetCaretFore() const noexcept;
	void SetCaretStyle(Scintilla::CaretStyle style);
	Scintilla::CaretStyle GetCaretStyle() const noexcept;
	void SetCaretWidth(int pixelWidth);
	int GetCaretWidth() const noexcept;
	void SetAdditionalCaretsBlink(bool blink);
	bool GetAdditionalCaretsBlink() const noexcept;
	void SetAdditionalCaretsVisible(bool visible);
	bool GetAdditionalCaretsVisible() const noexcept;
	void SetAdditionalCaretFore(int rgb);
	int GetAdditionalCaretFore() const noexcept;
	void MoveSelectedLines(int lineDelta);
	void MoveSelectedLinesUp();
	void MoveSelectedLinesDown();
	void MoveCaretInsideView(bool ensureVisible=true);
	Sci::Line DisplayFromPosition(Sci::Position pos);

	struct XYScrollPosition {
		int xOffset;
		Sci::Line topLine;
		XYScrollPosition(int xOffset_, Sci::Line topLine_) noexcept : xOffset(xOffset_), topLine(topLine_) {}
		bool operator==(const XYScrollPosition &other) const noexcept {
			return (xOffset == other.xOffset) && (topLine == other.topLine);
		}
	};
	XYScrollPosition XYScrollToMakeVisible(const SelectionRange &range,
		const XYScrollOptions options, CaretPolicies policies);
	void SetXYScroll(XYScrollPosition newXY);
	void EnsureCaretVisible(bool useMargin=true, bool vert=true, bool horiz=true);
	void ScrollRange(SelectionRange range);
	void ShowCaretAtCurrentPosition();
	void DropCaret();
	void CaretSetPeriod(int period);
	void InvalidateCaret();
	virtual void NotifyCaretMove();
	virtual void UpdateSystemCaret();

	bool Wrapping() const noexcept;
	void NeedWrapping(Sci::Line docLineStart=0, Sci::Line docLineEnd=WrapPending::lineLarge);
	bool WrapOneLine(Surface *surface, Sci::Line lineToWrap);
	bool WrapBlock(Surface *surface, Sci::Line lineToWrap, Sci::Line lineToWrapEnd);
	enum class WrapScope {wsAll, wsVisible, wsIdle};
	bool WrapLines(WrapScope ws);
	void SetWrapVisualFlags(Scintilla::WrapVisualFlag wrapVisualFlags);
	Scintilla::WrapVisualFlag GetWrapVisualFlags() const noexcept;
	void SetWrapVisualFlagsLocation(Scintilla::WrapVisualLocation wrapVisualFlagsLocation);
	Scintilla::WrapVisualLocation GetWrapVisualFlagsLocation() const noexcept;
	void SetWrapStartIndent(int wrapStartIndent);
	int GetWrapStartIndent() const noexcept;
	void SetWrapIndentMode(Scintilla::WrapIndentMode wrapIndentMode);
	Scintilla::WrapIndentMode GetWrapIndentMode() const noexcept;

	// Lines, indentation, and line queries: EditorLines.cxx.
	// ConvertEOLs / SetEOLMode / GetEOLMode are public application methods.
	void LinesJoin();
	void LinesSplit(int pixelWidth);
	void SetTabDrawMode(Scintilla::TabDrawMode tabDrawMode);
	Scintilla::TabDrawMode GetTabDrawMode() const noexcept;
	void SetTabWidth(int tabWidth);
	int GetTabWidth() const noexcept;
	void SetTabMinimumWidth(int pixels);
	int GetTabMinimumWidth() const noexcept;
	void ClearTabStops(Sci::Line line);
	void AddTabStop(Sci::Line line, int x);
	int GetNextTabStop(Sci::Line line, int x) const;
	void SetSelEOLFilled(bool filled);
	bool GetSelEOLFilled() const noexcept;
	void SetWordChars(std::string_view characters);
	int GetWordChars(unsigned char *buffer) const;
	void SetIndent(int indentSize);
	int GetIndent() const noexcept;
	void SetUseTabs(bool useTabs);
	bool GetUseTabs() const noexcept;
	void SetLineIndentation(Sci::Line line, Sci::Position indentSize);
	int GetLineIndentation(Sci::Line line) const;
	Sci::Position GetLineIndentPosition(Sci::Line line) const noexcept;
	Sci::Position GetColumn(Sci::Position pos) const noexcept;
	Sci::Position CountCharacters(Sci::Position startPos, Sci::Position endPos) const noexcept;
	Sci::Position CountCodeUnits(Sci::Position startPos, Sci::Position endPos) const noexcept;
	void SetIndentationGuides(Scintilla::IndentView indentView);
	Scintilla::IndentView GetIndentationGuides() const noexcept;
	Sci::Position GetLineEndPosition(Sci::Line line) const noexcept;
	Sci::Position GetLine(Sci::Line line, char *buffer) const;
	Sci::Line GetLineCount() const noexcept;
	void AllocateLines(Sci::Line lines);
	Sci::Line LineFromPosition(Sci::Position pos) const noexcept;
	Sci::Position PositionFromLine(Sci::Line line);
	bool GetLineVisible(Sci::Line line) const noexcept;
	void SetTabIndents(bool tabIndents);
	bool GetTabIndents() const noexcept;
	void SetBackSpaceUnIndents(bool bsUnIndents);
	bool GetBackSpaceUnIndents() const noexcept;
	Sci::Position LineLength(Sci::Line line) const noexcept;
	void SetViewEOL(bool visible);
	bool GetViewEOL() const noexcept;
	void SetEdgeColumn(int column);
	int GetEdgeColumn() const noexcept;
	int GetMultiEdgeColumn(size_t which) const noexcept;
	Sci::Position GetLineSelStartPosition(Sci::Line line) const noexcept;
	Sci::Position GetLineSelEndPosition(Sci::Line line) const noexcept;
	void SetWhitespaceChars(std::string_view characters);
	int GetWhitespaceChars(unsigned char *buffer) const;
	void SetPunctuationChars(std::string_view characters);
	int GetPunctuationChars(unsigned char *buffer) const;
	Sci::Position FindColumn(Sci::Line line, Sci::Position column) const noexcept;
	void SetLineEndTypesAllowed(Scintilla::LineEndType lineEndBitSet);
	Scintilla::LineEndType GetLineEndTypesAllowed() const noexcept;
	Scintilla::LineEndType GetLineEndTypesActive() const noexcept;
	Scintilla::LineCharacterIndexType GetLineCharacterIndex() const noexcept;
	void AllocateLineCharacterIndex(Scintilla::LineCharacterIndexType lineCharacterIndex);
	void ReleaseLineCharacterIndex(Scintilla::LineCharacterIndexType lineCharacterIndex);

	void PaintSelMargin(Surface *surfaceWindow, const PRectangle &rc);
	void RefreshPixMaps(Surface *surfaceWindow);
	void Paint(Surface *surfaceWindow, PRectangle rcArea);

	virtual void SetVerticalScrollPos();
	virtual void SetHorizontalScrollPos() = 0;
	virtual bool ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) = 0;
	virtual void ReconfigureScrollBars();
	void ChangeScrollBars();
	virtual void SetScrollBars();
	void ChangeSize();

	void FilterSelections();
	Sci::Position RealizeVirtualSpace(Sci::Position position, Sci::Position virtualSpace);
	SelectionPosition RealizeVirtualSpace(const SelectionPosition &position);
	void AddChar(char ch);
	virtual void InsertCharacter(std::string_view sv, Scintilla::CharacterSource charSource);
	void ClearSelectionRange(SelectionRange &range);
	void ClearBeforeTentativeStart();
	void InsertPaste(std::string_view text);
	[[deprecated]] void InsertPaste(const char *text, Sci::Position len);
	enum class PasteShape { stream=0, rectangular = 1, line = 2 };
	void InsertPasteShape(std::string_view text, PasteShape shape);
	[[deprecated]] void InsertPasteShape(const char *text, Sci::Position len, PasteShape shape);
	void ClearSelection(bool retainMultipleSelections = false);
	// Document text: definitions and descriptions in EditorDocument.cxx.
	void ClearAll();
	void AddText(std::string_view text);
	void InsertText(Sci::Position pos, std::string_view text);
	void AppendText(std::string_view text);
	void DeleteRange(Sci::Position start, Sci::Position lengthDelete);
	Sci::Position GetLength() const noexcept;
	char GetCharAt(Sci::Position pos) const noexcept;
	void Allocate(Sci::Position bytes);
	const char *GetRangePointer(Sci::Position start, Sci::Position rangeLength);
	Sci::Position GetGapPosition() const noexcept;

	// Styling and drawing: definitions in EditorStyling.cxx.
	void ClearDocumentStyle();
	int GetStyleAt(Sci::Position pos) const noexcept;
	int GetStyleIndexAt(Sci::Position pos) const noexcept;
	Sci::Position GetEndStyled() const noexcept;
	void StartStyling(Sci::Position start);
	void SetStyling(Sci::Position length, int style);
	void SetStylingEx(Sci::Position length, const char *styles);
	void SetIdleStyling(Scintilla::IdleStyling idleStyling_);
	Scintilla::IdleStyling GetIdleStyling() const noexcept;
	void StyleClearAll();
	void StyleResetDefault();
	void StyleSetFore(int style, int rgb);
	int StyleGetFore(int style);
	void StyleSetBack(int style, int rgb);
	int StyleGetBack(int style);
	void StyleSetBold(int style, bool bold);
	bool StyleGetBold(int style);
	void StyleSetWeight(int style, Scintilla::FontWeight weight);
	Scintilla::FontWeight StyleGetWeight(int style);
	void StyleSetStretch(int style, Scintilla::FontStretch stretch);
	Scintilla::FontStretch StyleGetStretch(int style);
	void StyleSetItalic(int style, bool italic);
	bool StyleGetItalic(int style);
	void StyleSetEOLFilled(int style, bool eolFilled);
	bool StyleGetEOLFilled(int style);
	void StyleSetSize(int style, int sizePoints);
	int StyleGetSize(int style);
	void StyleSetSizeFractional(int style, int sizeHundredthPoints);
	int StyleGetSizeFractional(int style);
	void StyleSetFont(int style, const char *fontName);
	int StyleGetFont(int style, char *buffer);
	void StyleSetUnderline(int style, bool underline);
	bool StyleGetUnderline(int style);
	void StyleSetCase(int style, Scintilla::CaseVisible caseVisible);
	Scintilla::CaseVisible StyleGetCase(int style);
	void StyleSetCharacterSet(int style, Scintilla::CharacterSet characterSet);
	Scintilla::CharacterSet StyleGetCharacterSet(int style);
	void StyleSetVisible(int style, bool visible);
	bool StyleGetVisible(int style);
	void StyleSetChangeable(int style, bool changeable);
	bool StyleGetChangeable(int style);
	void StyleSetHotSpot(int style, bool hotspot);
	bool StyleGetHotSpot(int style);
	void StyleSetCheckMonospaced(int style, bool checkMonospaced);
	bool StyleGetCheckMonospaced(int style);
	void StyleSetInvisibleRepresentation(int style, const char *utf8);
	int StyleGetInvisibleRepresentation(int style, char *buffer);
	void SetElementColour(Scintilla::Element element, int colourAlpha);
	int GetElementColour(Scintilla::Element element) const;
	void ResetElementColour(Scintilla::Element element);
	bool GetElementIsSet(Scintilla::Element element) const;
	bool GetElementAllowsTranslucent(Scintilla::Element element) const;
	int GetElementBaseColour(Scintilla::Element element) const;
	void SetFontLocale(const char *localeName);
	int GetFontLocale(char *buffer) const;
	void SetLayoutCache(Scintilla::LineCache cacheMode);
	Scintilla::LineCache GetLayoutCache() const noexcept;
	void SetPositionCache(int size);
	int GetPositionCache() const noexcept;
	void SetLayoutThreads(unsigned int threads);
	unsigned int GetLayoutThreads() const noexcept;
	void SetPhasesDraw(int phases);
	int GetPhasesDraw() const noexcept;
	void SetExtraAscent(int extraAscent);
	int GetExtraAscent() const noexcept;
	void SetExtraDescent(int extraDescent);
	int GetExtraDescent() const noexcept;
	void RGBAImageSetWidth(int width);
	void RGBAImageSetHeight(int height);
	void RGBAImageSetScale(int scalePercent);
	virtual void SetBidirectional(Scintilla::Bidirectional bidirectional_);
	Scintilla::Bidirectional GetBidirectional() const noexcept;
	void SetViewWS(Scintilla::WhiteSpace viewWS);
	Scintilla::WhiteSpace GetViewWS() const noexcept;
	void SetWhitespaceFore(bool useSetting, int rgb);
	void SetWhitespaceBack(bool useSetting, int rgb);
	void SetWhitespaceSize(int size);
	int GetWhitespaceSize() const noexcept;
	void SetSelFore(bool useSetting, int rgb);
	void SetSelBack(bool useSetting, int rgb);
	void SetSelAlpha(int alpha);
	int GetSelAlpha() const;
	void SetAdditionalSelFore(int rgb);
	void SetAdditionalSelBack(int rgb);
	void SetAdditionalSelAlpha(int alpha);
	int GetAdditionalSelAlpha() const;
	void SetHighlightGuide(int column);
	int GetHighlightGuide() const noexcept;
	void SetEdgeMode(Scintilla::EdgeVisualStyle edgeMode);
	Scintilla::EdgeVisualStyle GetEdgeMode() const noexcept;
	void SetEdgeColour(int rgb);
	int GetEdgeColour() const noexcept;
	void MultiEdgeAddLine(int column, int rgb);
	void MultiEdgeClearAll();
	void ReleaseAllExtendedStyles();
	int AllocateExtendedStyles(int numberStyles);
	long TextWidth(Scintilla::uptr_t style, const char *text);
	// Clipboard: definitions and descriptions in EditorClipboard.cxx.
	// Clipboard cut/copy helpers and options: EditorClipboard.cxx.
	// Copy and Paste remain pure virtual host hooks.
	virtual void Cut();
	void PasteRectangular(SelectionPosition pos, std::string_view text);
	[[deprecated]] void PasteRectangular(SelectionPosition pos, const char *ptr, Sci::Position len);
	virtual void Copy() = 0;
	void CopyAllowLine();
	void CutAllowLine();
	virtual void Paste() = 0;
	void SetMultiPaste(Scintilla::MultiPaste multiPaste);
	Scintilla::MultiPaste GetMultiPaste() const noexcept;
	void SetPasteConvertEndings(bool convert);
	bool GetPasteConvertEndings() const noexcept;
	void SetCopySeparator(std::string_view separator);
	std::string GetCopySeparator() const;
	void Clear();
	virtual void SelectAll();
	void RestoreSelection(Sci::Position newPos, UndoRedo history);
	// Undo and history: definitions and descriptions in EditorHistory.cxx.
	virtual void Undo();
	virtual void Redo();
	void EmptyUndoBuffer();
	void SetUndoCollection(bool collectUndo);
	bool GetUndoCollection() const noexcept;
	int GetUndoSequence() const noexcept;
	int GetUndoActions() const noexcept;
	void SetUndoSavePoint(int action);
	int GetUndoSavePoint() const noexcept;
	void SetUndoDetach(int action);
	int GetUndoDetach() const noexcept;
	void SetUndoTentative(int action);
	int GetUndoTentative() const noexcept;
	void SetUndoCurrent(int action);
	int GetUndoCurrent() const noexcept;
	int GetUndoActionType(int action) const noexcept;
	Sci::Position GetUndoActionPosition(int action) const noexcept;
	std::string_view GetUndoActionText(int action) const noexcept;
	void PushUndoActionType(int type, Sci::Position position);
	void ChangeLastUndoActionText(std::string_view text);
	void AddUndoAction(Sci::Position token, bool mayCoalesce);
	void SetChangeHistory(Scintilla::ChangeHistoryOption option);
	Scintilla::ChangeHistoryOption GetChangeHistory() const noexcept;
	void DelCharBack(bool allowLineStartDeletion);
	virtual void ClaimSelection() = 0;

	virtual void NotifyChange() = 0;
	// Fixed-host policy and status; definitions in EditorHost.cxx.
	void SetModEventMask(Scintilla::ModificationFlags eventMask) noexcept;
	Scintilla::ModificationFlags GetModEventMask() const noexcept;
	void SetCommandEvents(bool enabled) noexcept;
	bool GetCommandEvents() const noexcept;
	void SetStatus(Scintilla::Status status) noexcept;
	Scintilla::Status GetStatus() const noexcept;
	virtual void NotifyFocus(bool focus);
	virtual void SetCtrlID(int identifier);
	virtual int GetCtrlID() { return ctrlID; }
	virtual void NotifyParent(Scintilla::NotificationData scn) = 0;
	virtual void NotifyStyleToNeeded(Sci::Position endStyleNeeded);
	void NotifyChar(int ch, Scintilla::CharacterSource charSource);
	void NotifySavePoint(bool isSavePoint);
	void NotifyModifyAttempt();
	virtual void NotifyDoubleClick(Point pt, Scintilla::KeyMod modifiers);
	void NotifyHotSpotClicked(Sci::Position position, Scintilla::KeyMod modifiers);
	void NotifyHotSpotDoubleClicked(Sci::Position position, Scintilla::KeyMod modifiers);
	void NotifyHotSpotReleaseClick(Sci::Position position, Scintilla::KeyMod modifiers);
	bool NotifyUpdateUI();
	void NotifyPainted();
	void NotifyIndicatorClick(bool click, Sci::Position position, Scintilla::KeyMod modifiers);
	bool NotifyMarginClick(Point pt, Scintilla::KeyMod modifiers);
	bool NotifyMarginRightClick(Point pt, Scintilla::KeyMod modifiers);
	void NotifyNeedShown(Sci::Position pos, Sci::Position len);
	void NotifyDwelling(Point pt, bool state);
	void NotifyZoom();

	void NotifyModifyAttempt(Document *document, void *userData) override;
	void NotifySavePoint(Document *document, void *userData, bool atSavePoint) override;
	void CheckModificationForWrap(DocModification mh);
	void NotifyModified(Document *document, DocModification mh, void *userData) override;
	void NotifyDeleted(Document *document, void *userData) noexcept override;
	void NotifyStyleNeeded(Document *doc, void *userData, Sci::Position endStyleNeeded) override;
	void NotifyErrorOccurred(Document *doc, void *userData, Scintilla::Status status) override;
	void NotifyGroupCompleted(Document *, void *) noexcept override;
	void NotifyMacroRecord(Scintilla::Message iMessage, Scintilla::uptr_t wParam, Scintilla::sptr_t lParam);

	void ContainerNeedsUpdate(Scintilla::Update flags) noexcept;
	void PageMove(int direction, Selection::SelTypes selt=Selection::SelTypes::none, bool stuttered = false);
	enum class CaseMapping { same, upper, lower };
	virtual std::string CaseMapString(const std::string &s, CaseMapping caseMapping);
	void ChangeCaseOfSelection(CaseMapping caseMapping);
	void LineDelete();
	void LineTranspose();
	void LineReverse();
	void Duplicate(bool forLine);
	virtual void CancelModes();
	void NewLine();
	SelectionPosition PositionUpOrDown(SelectionPosition spStart, int direction, int lastX);
	void CursorUpOrDown(int direction, Selection::SelTypes selt);
	void ParaUpOrDown(int direction, Selection::SelTypes selt);
	Range RangeDisplayLine(Sci::Line lineVisible);
	Sci::Position StartEndDisplayLine(Sci::Position pos, bool start);
	Sci::Position HomeWrapPosition(Sci::Position position);
	Sci::Position VCHomeDisplayPosition(Sci::Position position);
	Sci::Position VCHomeWrapPosition(Sci::Position position);
	Sci::Position LineEndWrapPosition(Sci::Position position);
	SelectionPosition PositionMove(EditorCommand command, SelectionPosition spCaretNow);
	SelectionRange SelectionMove(EditorCommand command, size_t r);
	int HorizontalMove(EditorCommand command);
	int DelWordOrLine(EditorCommand command);
	virtual int ExecuteCommand(EditorCommand command);
	virtual int KeyDefault(Scintilla::Keys /* key */, Scintilla::KeyMod /*modifiers*/);
	int KeyDownWithModifiers(Scintilla::Keys key, Scintilla::KeyMod modifiers, bool *consumed);

	void Indent(bool forwards, bool lineIndent);

	// Search and replace helpers: EditorSearch.cxx.
	// Application target/search methods are public below.
	void SearchAnchor() noexcept;
	Sci::Position SearchText(EditorCommand command, Scintilla::uptr_t wParam, Scintilla::sptr_t lParam);
	Sci::Position SearchInTarget(const char *text, Sci::Position length);
	void SetTargetStartVirtualSpace(Sci::Position space);
	Sci::Position GetTargetStartVirtualSpace() const noexcept;
	void SetTargetEndVirtualSpace(Sci::Position space);
	Sci::Position GetTargetEndVirtualSpace() const noexcept;
	void ReplaceSel(std::string_view text);
	void ReplaceRectangular(std::string_view text);
	// Scrolling options and hit testing: EditorScrolling.cxx.
	void SetHScrollBar(bool visible);
	bool GetHScrollBar() const noexcept;
	void SetVScrollBar(bool visible);
	bool GetVScrollBar() const noexcept;
	void SetScrollWidth(int width);
	int GetScrollWidth() const noexcept;
	void SetScrollWidthTracking(bool tracking);
	bool GetScrollWidthTracking() const noexcept;
	void SetEndAtLastLine(bool endAtLast);
	bool GetEndAtLastLine() const noexcept;
	int TextHeightPixels();
	void SetVisiblePolicy(Scintilla::uptr_t policy, Scintilla::sptr_t slop);
	int PointXFromPosition(Sci::Position pos);
	int PointYFromPosition(Sci::Position pos);
	// Margins: definitions in EditorMargins.cxx.
	bool ValidMargin(Scintilla::uptr_t margin) const noexcept;
	void SetMargins(size_t margins);
	size_t GetMargins() const noexcept;
	void SetMarginTypeN(size_t margin, Scintilla::MarginType marginType);
	Scintilla::MarginType GetMarginTypeN(size_t margin) const noexcept;
	void SetMarginWidthN(size_t margin, int pixelWidth);
	int GetMarginWidthN(size_t margin) const noexcept;
	void SetMarginMaskN(size_t margin, int mask);
	int GetMarginMaskN(size_t margin) const noexcept;
	void SetMarginSensitiveN(size_t margin, bool sensitive);
	bool GetMarginSensitiveN(size_t margin) const noexcept;
	void SetMarginCursorN(size_t margin, Scintilla::CursorShape cursor);
	Scintilla::CursorShape GetMarginCursorN(size_t margin) const noexcept;
	void SetMarginBackN(size_t margin, int rgb);
	int GetMarginBackN(size_t margin) const noexcept;
	void SetMarginLeft(int pixelWidth);
	int GetMarginLeft() const noexcept;
	void SetMarginRight(int pixelWidth);
	int GetMarginRight() const noexcept;
	void SetFoldMarginColour(bool useSetting, int rgb);
	void SetFoldMarginHiColour(bool useSetting, int rgb);
	void MarginSetText(Sci::Line line, const char *text);
	std::string MarginGetText(Sci::Line line) const;
	void MarginSetStyle(Sci::Line line, int style);
	int MarginGetStyle(Sci::Line line) const noexcept;
	void MarginSetStyles(Sci::Line line, const unsigned char *styles);
	Sci::Position MarginGetStyles(Sci::Line line, char *buffer) const;
	void MarginTextClearAll();
	void MarginSetStyleOffset(int style);
	int MarginGetStyleOffset() const noexcept;
	void SetMarginOptions(Scintilla::MarginOption options);
	Scintilla::MarginOption GetMarginOptions() const noexcept;

	// Markers: definitions in EditorMarkers.cxx.
	Sci::Line MarkerLineFromHandle(int markerHandle) const noexcept;
	void MarkerDeleteHandle(int markerHandle);
	int MarkerHandleFromLine(Sci::Line line, int which) const noexcept;
	int MarkerNumberFromLine(Sci::Line line, int which) const noexcept;
	// markerNumber is size_t so message wParam is checked before any 32-bit narrowing.
	void MarkerDefine(size_t markerNumber, Scintilla::MarkerSymbol markerSymbol);
	Scintilla::MarkerSymbol MarkerSymbolDefined(size_t markerNumber) const noexcept;
	void MarkerSetFore(size_t markerNumber, int rgb);
	void MarkerSetBack(size_t markerNumber, int rgb);
	void MarkerSetBackSelected(size_t markerNumber, int rgb);
	void MarkerSetForeTranslucent(size_t markerNumber, int colourAlpha);
	void MarkerSetBackTranslucent(size_t markerNumber, int colourAlpha);
	void MarkerSetBackSelectedTranslucent(size_t markerNumber, int colourAlpha);
	void MarkerSetStrokeWidth(size_t markerNumber, int hundredths);
	void MarkerEnableHighlight(bool enabled);
	void MarkerSetAlpha(size_t markerNumber, Scintilla::Alpha alpha);
	void MarkerSetLayer(size_t markerNumber, Scintilla::Layer layer);
	Scintilla::Layer MarkerGetLayer(size_t markerNumber) const noexcept;
	int MarkerAdd(Sci::Line line, int markerNumber);
	void MarkerAddSet(Sci::Line line, int markerSet);
	void MarkerDelete(Sci::Line line, int markerNumber);
	void MarkerDeleteAll(int markerNumber);
	int MarkerGet(Sci::Line line) const;
	Sci::Line MarkerNext(Sci::Line lineStart, int markerMask) const noexcept;
	Sci::Line MarkerPrevious(Sci::Line lineStart, int markerMask) const;
	void MarkerDefinePixmap(size_t markerNumber, const char *pixmap);
	void MarkerDefineRGBAImage(size_t markerNumber, const unsigned char *pixels);

	// Decorations: definitions in EditorDecorations.cxx.
	// indicator is size_t so message wParam is checked before any 32-bit narrowing.
	void IndicSetStyle(size_t indicator, Scintilla::IndicatorStyle style);
	Scintilla::IndicatorStyle IndicGetStyle(size_t indicator) const noexcept;
	void IndicSetFore(size_t indicator, int rgb);
	int IndicGetFore(size_t indicator) const noexcept;
	void IndicSetHoverStyle(size_t indicator, Scintilla::IndicatorStyle style);
	Scintilla::IndicatorStyle IndicGetHoverStyle(size_t indicator) const noexcept;
	void IndicSetHoverFore(size_t indicator, int rgb);
	int IndicGetHoverFore(size_t indicator) const noexcept;
	void IndicSetFlags(size_t indicator, Scintilla::IndicFlag flags);
	Scintilla::IndicFlag IndicGetFlags(size_t indicator) const noexcept;
	void IndicSetUnder(size_t indicator, bool under);
	bool IndicGetUnder(size_t indicator) const noexcept;
	void IndicSetAlpha(size_t indicator, int alpha);
	int IndicGetAlpha(size_t indicator) const noexcept;
	void IndicSetOutlineAlpha(size_t indicator, int alpha);
	int IndicGetOutlineAlpha(size_t indicator) const noexcept;
	void IndicSetStrokeWidth(size_t indicator, int hundredths);
	int IndicGetStrokeWidth(size_t indicator) const noexcept;
	void SetIndicatorCurrent(int indicator);
	int GetIndicatorCurrent() const noexcept;
	void SetIndicatorValue(int value);
	int GetIndicatorValue() const noexcept;
	void IndicatorFillRange(Sci::Position start, Sci::Position lengthFill);
	void IndicatorClearRange(Sci::Position start, Sci::Position lengthClear);
	int IndicatorAllOnFor(Sci::Position pos) const;
	int IndicatorValueAt(int indicator, Sci::Position pos) const;
	Sci::Position IndicatorStart(int indicator, Sci::Position pos) const;
	Sci::Position IndicatorEnd(int indicator, Sci::Position pos) const;
	void BraceHighlight(Sci::Position pos0, Sci::Position pos1);
	void BraceHighlightIndicator(bool useSetting, size_t indicator);
	void BraceBadLight(Sci::Position pos);
	void BraceBadLightIndicator(bool useSetting, size_t indicator);
	Sci::Position BraceMatch(Sci::Position pos, Sci::Position maxReStyle) const noexcept;
	Sci::Position BraceMatchNext(Sci::Position pos, Sci::Position startPos) const noexcept;
	void SetControlCharSymbol(int symbol);
	int GetControlCharSymbol() const noexcept;
	void SetRepresentation(std::string_view charBytes, std::string_view value);
	int GetRepresentation(std::string_view charBytes, char *buffer) const;
	void ClearRepresentation(std::string_view charBytes);
	void ClearAllRepresentations();
	void SetRepresentationAppearance(std::string_view charBytes, Scintilla::RepresentationAppearance appearance);
	Scintilla::RepresentationAppearance GetRepresentationAppearance(std::string_view charBytes) const;
	void SetRepresentationColour(std::string_view charBytes, int colourAlpha);
	int GetRepresentationColour(std::string_view charBytes) const;
	void SetHotspotActiveFore(bool useSetting, int rgb);
	int GetHotspotActiveFore() const;
	void SetHotspotActiveBack(bool useSetting, int rgb);
	int GetHotspotActiveBack() const;
	void SetHotspotActiveUnderline(bool underline);
	bool GetHotspotActiveUnderline() const noexcept;
	void SetHotspotSingleLine(bool singleLine);
	bool GetHotspotSingleLine() const noexcept;
	void AnnotationSetText(Sci::Line line, const char *text);
	std::string AnnotationGetText(Sci::Line line) const;
	void AnnotationSetStyle(Sci::Line line, int style);
	int AnnotationGetStyle(Sci::Line line) const noexcept;
	void AnnotationSetStyles(Sci::Line line, const unsigned char *styles);
	Sci::Position AnnotationGetStyles(Sci::Line line, char *buffer) const;
	int AnnotationGetLines(Sci::Line line) const noexcept;
	void AnnotationClearAll();
	Scintilla::AnnotationVisible AnnotationGetVisible() const noexcept;
	void AnnotationSetStyleOffset(int style);
	int AnnotationGetStyleOffset() const noexcept;
	void EOLAnnotationSetText(Sci::Line line, const char *text);
	std::string EOLAnnotationGetText(Sci::Line line) const;
	void EOLAnnotationSetStyle(Sci::Line line, int style);
	int EOLAnnotationGetStyle(Sci::Line line) const noexcept;
	void EOLAnnotationClearAll();
	Scintilla::EOLAnnotationVisible EOLAnnotationGetVisible() const noexcept;
	void EOLAnnotationSetStyleOffset(int style);
	int EOLAnnotationGetStyleOffset() const noexcept;

	virtual void CopyToClipboard(const SelectionText &selectedText) = 0;
	std::string RangeText(Sci::Position start, Sci::Position end) const;
	bool CopyLineRange(SelectionText *ss, bool allowProtected=true);
	void CopySelectionRange(SelectionText *ss, bool allowLineCopy=false);
	void CopyRangeToClipboard(Sci::Position start, Sci::Position end);
	void CopyText(std::string_view text);
	void SetDragPosition(SelectionPosition newPos);
	virtual void DisplayCursor(Window::Cursor c);
	virtual bool DragThreshold(Point ptStart, Point ptNow);
	virtual void StartDrag();
	void DropAt(SelectionPosition position, std::string_view value, bool moving, bool rectangular);
	[[deprecated]] void DropAt(SelectionPosition position, const char *value, size_t lengthValue, bool moving, bool rectangular);
	[[deprecated]] void DropAt(SelectionPosition position, const char *value, bool moving, bool rectangular);
	/** PositionInSelection returns true if position in selection. */
	bool PositionInSelection(Sci::Position pos);
	bool PointInSelection(Point pt);
	ptrdiff_t SelectionFromPoint(Point pt);
	bool PointInSelMargin(Point pt) const;
	Window::Cursor GetMarginCursor(Point pt) const noexcept;
	void DropSelection(size_t part);
	void TrimAndSetSelection(Sci::Position currentPos_, Sci::Position anchor_);
	void LineSelection(Sci::Position lineCurrentPos_, Sci::Position lineAnchorPos_, bool wholeLine);
	void WordSelection(Sci::Position pos);
	void DwellEnd(bool mouseMoved);
	void MouseLeave();
	virtual void ButtonDownWithModifiers(Point pt, unsigned int curTime, Scintilla::KeyMod modifiers);
	virtual void RightButtonDownWithModifiers(Point pt, unsigned int curTime, Scintilla::KeyMod modifiers);
	void ButtonMoveWithModifiers(Point pt, unsigned int curTime, Scintilla::KeyMod modifiers);
	void ButtonUpWithModifiers(Point pt, unsigned int curTime, Scintilla::KeyMod modifiers);

	bool Idle();
	enum class TickReason { caret, scroll, widen, dwell, platform };
	virtual void TickFor(TickReason reason);
	virtual bool FineTickerRunning(TickReason reason);
	virtual void FineTickerStart(TickReason reason, int millis, int tolerance);
	virtual void FineTickerCancel(TickReason reason);
	virtual bool SetIdle(bool) { return false; }
	void ChangeMouseCapture(bool on);
	virtual void SetMouseCapture(bool on) = 0;
	virtual bool HaveMouseCapture() = 0;
	virtual void UpdateBaseElements();

	Sci::Position PositionAfterArea(PRectangle rcArea) const;
	void StyleToPositionInView(Sci::Position pos);
	Sci::Position PositionAfterMaxStyling(Sci::Position posMax, bool scrolling) const;
	void StartIdleStyling(bool truncatedLastStyling);
	void StyleAreaBounded(PRectangle rcArea, bool scrolling);
	constexpr bool SynchronousStylingToVisible() const noexcept {
		return (idleStyling == Scintilla::IdleStyling::None) || (idleStyling == Scintilla::IdleStyling::AfterVisible);
	}
	void IdleStyle();
	virtual void IdleWork();
	virtual void QueueIdleWork(WorkItems items, Sci::Position upTo=0);

	int SupportsFeature(Scintilla::Supports feature);
	virtual bool PaintContains(PRectangle rc);
	bool PaintContainsMargin();
	void CheckForChangeOutsidePaint(Range r);
	void SetBraceHighlight(Sci::Position pos0, Sci::Position pos1, int matchStyle);

	void SetAnnotationHeights(Sci::Line start, Sci::Line end);
	virtual void SetDocPointer(Document *document);

	void SetAnnotationVisible(Scintilla::AnnotationVisible visible);
	void SetEOLAnnotationVisible(Scintilla::EOLAnnotationVisible visible);

	// Folding: definitions in EditorFolding.cxx.
	Sci::Line VisibleFromDocLine(Sci::Line docLine) const noexcept;
	Sci::Line DocLineFromVisible(Sci::Line displayLine) const noexcept;
	int SetFoldLevel(Sci::Line line, Scintilla::FoldLevel level);
	Scintilla::FoldLevel GetFoldLevel(Sci::Line line) const noexcept;
	Sci::Line GetLastChild(Sci::Line line, std::optional<Scintilla::FoldLevel> level = {}) const;
	Sci::Line GetFoldParent(Sci::Line line) const noexcept;
	void ShowLines(Sci::Line lineStart, Sci::Line lineEnd);
	void HideLines(Sci::Line lineStart, Sci::Line lineEnd);
	bool GetAllLinesVisible() const noexcept;
	void SetFoldExpanded(Sci::Line lineDoc, bool expanded);
	bool GetFoldExpanded(Sci::Line lineDoc) const noexcept;
	void SetAutomaticFold(Scintilla::AutomaticFold automatic);
	Scintilla::AutomaticFold GetAutomaticFold() const noexcept;
	void SetFoldFlags(Scintilla::FoldFlag flags);
	void ToggleFold(Sci::Line line);
	void ToggleFoldShowText(Sci::Line line, const char *text);
	void FoldDisplayTextSetStyle(Scintilla::FoldDisplayTextStyle style);
	Scintilla::FoldDisplayTextStyle FoldDisplayTextGetStyle() const noexcept;
	void SetDefaultFoldDisplayText(const char *text);
	void FoldLine(Sci::Line line, Scintilla::FoldAction action);
	void FoldChildren(Sci::Line line, Scintilla::FoldAction action);
	void FoldAll(Scintilla::FoldAction action);
	void ExpandChildren(Sci::Line line, Scintilla::FoldLevel level);
	Sci::Line ContractedFoldNext(Sci::Line lineStart) const noexcept;
	void EnsureVisible(Sci::Line line);
	void EnsureVisibleEnforcePolicy(Sci::Line line);
	Sci::Line ExpandLine(Sci::Line line);
	void FoldExpand(Sci::Line line, Scintilla::FoldAction action, Scintilla::FoldLevel level);
	void EnsureLineVisible(Sci::Line lineDoc, bool enforcePolicy);
	void FoldChanged(Sci::Line line, Scintilla::FoldLevel levelNow, Scintilla::FoldLevel levelPrev);
	void NeedShown(Sci::Position pos, Sci::Position len);

	Sci::Position GetTag(char *tagValue, int tagNumber);
	enum class ReplaceType {basic, patterns, minimal};
	Sci::Position ReplaceTarget(ReplaceType replaceType, std::string_view text);

	bool PositionIsHotspot(Sci::Position position) const noexcept;
	bool PointIsHotspot(Point pt);
	void SetHotSpotRange(const Point *pt);
	void SetHoverIndicatorPosition(Sci::Position position);
	void SetHoverIndicatorPoint(Point pt);

	virtual std::unique_ptr<Surface> CreateMeasurementSurface() const;
	virtual std::unique_ptr<Surface> CreateDrawingSurface(SurfaceID sid, std::optional<Scintilla::Technology> technologyOpt = {}) const;

	Sci::Line WrapCount(Sci::Line line);
	void AddStyledText(const char *buffer, Sci::Position appendLength);
	Sci::Position GetStyledText(char *buffer, Sci::Position cpMin, Sci::Position cpMax) const noexcept;
	Sci::Position GetTextRange(char *buffer, Sci::Position cpMin, Sci::Position cpMax) const;

	virtual Scintilla::sptr_t DefWndProc(Scintilla::Message iMessage, Scintilla::uptr_t wParam, Scintilla::sptr_t lParam) = 0;
	void SetSelectionNMessage(Scintilla::Message iMessage, Scintilla::uptr_t wParam, Scintilla::sptr_t lParam);
	void SetSelectionMode(uptr_t wParam, bool setMoveExtends);

	// Coercion functions for transforming WndProc parameters into pointers
	static void *PtrFromSPtr(Scintilla::sptr_t lParam) noexcept {
		return reinterpret_cast<void *>(lParam);
	}
	static const char *ConstCharPtrFromSPtr(Scintilla::sptr_t lParam) noexcept {
		return static_cast<const char *>(PtrFromSPtr(lParam));
	}
	static const unsigned char *ConstUCharPtrFromSPtr(Scintilla::sptr_t lParam) noexcept {
		return static_cast<const unsigned char *>(PtrFromSPtr(lParam));
	}
	static char *CharPtrFromSPtr(Scintilla::sptr_t lParam) noexcept {
		return static_cast<char *>(PtrFromSPtr(lParam));
	}
	static unsigned char *UCharPtrFromSPtr(Scintilla::sptr_t lParam) noexcept {
		return static_cast<unsigned char *>(PtrFromSPtr(lParam));
	}
	static std::string_view ViewFromParams(Scintilla::sptr_t lParam, Scintilla::uptr_t wParam) noexcept {
		if (SPtrFromUPtr(wParam) == -1) {
			return std::string_view(CharPtrFromSPtr(lParam));
		}
		return std::string_view(CharPtrFromSPtr(lParam), wParam);
	}
	static void *PtrFromUPtr(Scintilla::uptr_t wParam) noexcept {
		return reinterpret_cast<void *>(wParam);
	}
	static const char *ConstCharPtrFromUPtr(Scintilla::uptr_t wParam) noexcept {
		return static_cast<const char *>(PtrFromUPtr(wParam));
	}

	static constexpr Scintilla::sptr_t SPtrFromUPtr(Scintilla::uptr_t wParam) noexcept {
		return static_cast<Scintilla::sptr_t>(wParam);
	}
	static constexpr Sci::Position PositionFromUPtr(Scintilla::uptr_t wParam) noexcept {
		return SPtrFromUPtr(wParam);
	}
	static constexpr Sci::Line LineFromUPtr(Scintilla::uptr_t wParam) noexcept {
		return SPtrFromUPtr(wParam);
	}
	Point PointFromParameters(Scintilla::uptr_t wParam, Scintilla::sptr_t lParam) const noexcept {
		return Point(static_cast<XYPOSITION>(wParam) - vs.ExternalMarginWidth(), static_cast<XYPOSITION>(lParam));
	}

	static constexpr std::optional<FoldLevel> OptionalFoldLevel(Scintilla::sptr_t lParam) {
		if (lParam >= 0) {
			return static_cast<FoldLevel>(lParam);
		}
		return std::nullopt;
	}

	static Scintilla::sptr_t StringResult(Scintilla::sptr_t lParam, const char *val) noexcept;
	static Scintilla::sptr_t BytesResult(Scintilla::sptr_t lParam, const unsigned char *val, size_t len) noexcept;
	static Scintilla::sptr_t BytesResult(Scintilla::sptr_t lParam, std::string_view sv) noexcept;

	// Set a variable controlling appearance to a value and invalidates the display
	// if a change was made. Avoids extra text and the possibility of mistyping.
	template <typename T>
	bool SetAppearance(T &variable, T value) {
		// Using ! and == as more types have == defined than !=.
		const bool changed = !(variable == value);
		if (changed) {
			variable = value;
			InvalidateStyleRedraw();
		}
		return changed;
	}

	// Private input operations; definitions in EditorInput.cxx.
	void SetIMEInteraction(Scintilla::IMEInteraction imeInteraction_);
	Scintilla::IMEInteraction GetIMEInteraction() const noexcept;
	void SetMouseDwellTime(int milliseconds);
	int GetMouseDwellTime() const noexcept;
	void SetMouseDownCaptures(bool captures);
	bool GetMouseDownCaptures() const noexcept;
	void SetMouseWheelCaptures(bool captures);
	bool GetMouseWheelCaptures() const noexcept;
	void SetCursor(Scintilla::CursorShape cursor);
	Scintilla::CursorShape GetCursor() const noexcept;
	void SetDragDropEnabled(bool enabled);
	bool GetDragDropEnabled() const noexcept;
	void ChangeInsertion(std::string_view text);

	// Private overtype and key-map operations; definitions in EditorCommands.cxx.
	void SetOvertype(bool overtype);
	bool GetOvertype() const noexcept;
	void AssignCmdKey(Scintilla::Keys key, Scintilla::KeyMod modifiers, EditorCommand command);
	void ClearCmdKey(Scintilla::Keys key, Scintilla::KeyMod modifiers);
	void ClearAllCmdKeys();

public:
	~Editor() override;

	// Whole-document text, dirty state, and read-only: definitions and
	// descriptions live beside the implementations in EditorDocument.cxx.
	void SetText(std::string_view text);
	std::string GetText() const;
	Sci::Position GetTextLength() const noexcept;
	bool GetModify() const noexcept;
	void SetReadOnly(bool readOnly);
	bool GetReadOnly() const noexcept;

	// Undo availability, save point, and undo groups: definitions in EditorHistory.cxx.
	bool CanUndo() const noexcept;
	bool CanRedo() const noexcept;
	void SetSavePoint();
	void BeginUndoAction();
	void EndUndoAction();

	// Clipboard menu enablement; definitions in EditorClipboard.cxx.
	virtual bool CanPaste();

	// Line-ending policy for new lines and conversion; definitions in EditorLines.cxx.
	void SetEOLMode(Scintilla::EndOfLine eolMode);
	Scintilla::EndOfLine GetEOLMode() const noexcept;
	void ConvertEOLs(Scintilla::EndOfLine eolMode);

	// View scrolling; definitions in EditorScrolling.cxx.
	Sci::Line GetFirstVisibleLine() const noexcept;
	void SetFirstVisibleLine(Sci::Line line);
	void SetXOffset(int offset);
	int GetXOffset() const noexcept;
	void LineScroll(Sci::Position columns, Sci::Line lines);
	void ScrollVertical(Sci::Line docLine, Sci::Line displayLine);

	// Target search and replace; definitions in EditorSearch.cxx.
	void SetTargetStart(Sci::Position pos);
	Sci::Position GetTargetStart() const noexcept;
	void SetTargetEnd(Sci::Position pos);
	Sci::Position GetTargetEnd() const noexcept;
	void SetTargetRange(Sci::Position start, Sci::Position end);
	void TargetWholeDocument();
	std::string GetTargetText() const;
	Sci::Position SearchInTarget(std::string_view text);
	void SetSearchFlags(Scintilla::FindOption flags);
	Scintilla::FindOption GetSearchFlags() const noexcept;

	// Basic selection and navigation; definitions in EditorSelection.cxx.
	Sci::Position GetCurrentPos() noexcept;
	Sci::Position GetAnchor() noexcept;
	void SetCurrentPos(Sci::Position pos);
	void SetAnchor(Sci::Position pos);
	void SetSelectionStart(Sci::Position pos);
	Sci::Position GetSelectionStart() const noexcept;
	void SetSelectionEnd(Sci::Position pos);
	Sci::Position GetSelectionEnd() const noexcept;
	void SetSel(Sci::Position start, Sci::Position end);
	std::string GetSelText();
	bool GetSelectionEmpty() const noexcept;
	void GotoLine(Sci::Line lineNo);
	void GotoPos(Sci::Position pos);

	// Wrap mode is application-facing; its description lives beside the
	// definition in EditorWrapping.cxx.
	void SetWrapMode(Scintilla::Wrap wrapMode);
	Scintilla::Wrap GetWrapMode() const noexcept;

	// Zoom level in points; definitions in EditorStyling.cxx.
	void SetZoom(int zoomInPoints);
	int GetZoom() const noexcept;

	// Compositor focus state; definitions in EditorInput.cxx.
	void SetFocus(bool focusState);
	bool HasFocus() const noexcept;

	// UTF-8 character/word boundaries and related queries; EditorTextBoundaries.cxx.
	Sci::Position PositionBefore(Sci::Position pos) const;
	Sci::Position PositionAfter(Sci::Position pos) const;
	Sci::Position PositionRelative(Sci::Position pos, Sci::Position relativeCharacters) const;
	Sci::Position PositionRelativeCodeUnits(Sci::Position pos, Sci::Position relativeUTF16Units) const;
	void SetCharsDefault();
	void SetCharacterCategoryOptimization(int countCharacters);
	int GetCharacterCategoryOptimization() const noexcept;
	const char *GetCharacterPointer() const;
	Sci::Position WordStartPosition(Sci::Position pos, bool onlyWordCharacters) const;
	Sci::Position WordEndPosition(Sci::Position pos, bool onlyWordCharacters) const;
	bool IsRangeWord(Sci::Position start, Sci::Position end) const;
	Sci::Position GetCurLine(char *buffer, Sci::Position bufferLength) const;
	Sci::Line LineFromIndexPosition(Sci::Position pos, Scintilla::LineCharacterIndexType lineCharacterIndex) const;
	Sci::Position IndexPositionFromLine(Sci::Line line, Scintilla::LineCharacterIndexType lineCharacterIndex) const;

	// Virtual-space policy; definitions in EditorSelection.cxx.
	void SetVirtualSpaceOptions(Scintilla::VirtualSpace options);
	Scintilla::VirtualSpace GetVirtualSpaceOptions() const noexcept;

	// Line state for multi-line lexers; definitions in EditorLexing.cxx.
	int SetLineState(Sci::Line line, int state);
	int GetLineState(Sci::Line line) const;
	int GetMaxLineState() const noexcept;
	void ChangeLexerState(Sci::Position start, Sci::Position end);

	// Printing; definitions in EditorPrinting.cxx.
	void SetPrintMagnification(int magnification);
	int GetPrintMagnification() const noexcept;
	void SetPrintColourMode(Scintilla::PrintOption mode);
	Scintilla::PrintOption GetPrintColourMode() const noexcept;
	void SetPrintWrapMode(Scintilla::Wrap wrapMode);
	Scintilla::Wrap GetPrintWrapMode() const noexcept;
	Sci::Position FormatRange(bool draw, const Scintilla::RangeToFormatFull &fr);

	// Public so scintilla_send_message can use it.
	virtual Scintilla::sptr_t WndProc(Scintilla::Message iMessage, Scintilla::uptr_t wParam, Scintilla::sptr_t lParam);
	// Public so scintilla_set_id can use it.
	int ctrlID;
	friend class AutoSurface;
};

/**
 * A smart pointer class to ensure Surfaces are set up and deleted correctly.
 */
class AutoSurface {
private:
	std::unique_ptr<Surface> surf;
public:
	AutoSurface(const Editor *ed) :
		surf(ed->CreateMeasurementSurface())  {
	}
	AutoSurface(SurfaceID sid, const Editor *ed, std::optional<Scintilla::Technology> technology = {}) :
		surf(ed->CreateDrawingSurface(sid, technology)) {
	}
	// Deleted so AutoSurface objects can not be copied.
	AutoSurface(const AutoSurface &) = delete;
	AutoSurface(AutoSurface &&) = delete;
	void operator=(const AutoSurface &) = delete;
	void operator=(AutoSurface &&) = delete;
	~AutoSurface() {
	}
	Surface *operator->() const noexcept {
		return surf.get();
	}
	operator Surface *() const noexcept {
		return surf.get();
	}
};

}

#endif
