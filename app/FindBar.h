// Fixed logical layout, hit-testing, UTF-8 field editing, pointer and keyboard
// transitions, and opaque painting for the in-window find bar. ApplicationUi
// owns FindBarModel and visibility; the component stays Wayland-free and does
// not search the document.

#ifndef FINDBAR_H
#define FINDBAR_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ApplicationInput.h"
#include "ApplicationTextInput.h"
#include "Geometry.h"
#include "Platform.h"
#include "UiStyle.h"

namespace Scalpel {

/** Fixed find-bar height in logical client pixels. */
[[nodiscard]] int FindBarHeight() noexcept;

/** Compact status after a search; cleared on a successful first-range match. */
enum class FindBarStatus {
	None,
	NoMatches,
	Wrapped,
};

enum class FindBarHit {
	None,
	Field,
	Previous,
	Next,
	Close,
	/** Empty band chrome outside controls. */
	Band,
};

struct FindBarHitResult {
	FindBarHit kind = FindBarHit::None;
};

/** Left-button press origin for matching button activation on release. */
enum class FindBarPressKind {
	Previous,
	Next,
	Close,
};

/**
 * Work the find bar cannot complete alone. Search and close are applied by
 * ApplicationUi; clipboard rows use the same request value type as the editor.
 */
enum class FindBarRequestKind {
	SearchForward,
	SearchBackward,
	Close,
	ClipboardCopy,
	ClipboardPaste,
};

struct FindBarRequest {
	FindBarRequestKind kind = FindBarRequestKind::Close;
	/** Local request id for ClipboardCopy / ClipboardPaste. */
	uint64_t clipboardId = 0;
	/** Payload for ClipboardCopy (selected or cut text). */
	std::string clipboardText;
};

/**
 * Process-lifetime query field state. Visibility is owned by ApplicationUi;
 * the model retains the query while the bar is closed and across tab switches.
 * Caret and selection are byte offsets into query, always on UTF-8 character
 * boundaries after every editing operation.
 */
struct FindBarModel {
	std::string query;
	std::size_t caret = 0;
	std::size_t anchor = 0;
	bool focused = false;
	FindBarStatus status = FindBarStatus::None;
	/** Uncommitted IME text; not part of query until committed. */
	std::optional<ApplicationTextInputPreedit> preedit;
	std::optional<FindBarPressKind> pressOrigin;
	/** Platform clipboard has a text offer; enables Ctrl+V request generation. */
	bool pasteAvailable = false;
	uint64_t nextClipboardRequest = 1;
	bool textInputStateDirty = false;
	ApplicationTextChangeCause textInputChangeCause =
		ApplicationTextChangeCause::Other;

	[[nodiscard]] std::size_t SelectionStart() const noexcept;
	[[nodiscard]] std::size_t SelectionEnd() const noexcept;
	[[nodiscard]] bool HasSelection() const noexcept;
	/** Clamp caret/anchor into [0, query.size()] on character boundaries. */
	void ClampCaretAndAnchor() noexcept;
	void SelectAll() noexcept;
	void CollapseSelectionToCaret() noexcept;
	/**
	 * Focus or blur the field. Focus gain selects the whole query and marks
	 * text-input state dirty; focus loss cancels preedit and clears presses.
	 */
	void SetFocused(bool focused) noexcept;
	void CancelPreedit() noexcept;
	void SetStatus(FindBarStatus status) noexcept;
	/**
	 * Replace the committed query, place the caret at the end, select all when
	 * focused, and cancel preedit. Rejects invalid UTF-8 (returns false).
	 */
	[[nodiscard]] bool SetQuery(std::string_view text);
	/** Delete the selection (or nothing when empty). Clears status. */
	void DeleteSelection() noexcept;
	/**
	 * Replace the selection with text (or insert at the caret). Rejects
	 * invalid UTF-8. Clears status and preedit.
	 */
	[[nodiscard]] bool InsertText(std::string_view text);
	/** Selected committed text, or empty when the selection is empty. */
	[[nodiscard]] std::string SelectedText() const;
};

struct FindBarLayout {
	Scintilla::Internal::PRectangle band;
	Scintilla::Internal::PRectangle field;
	/** Inner text area inside the field border and padding. */
	Scintilla::Internal::PRectangle fieldText;
	Scintilla::Internal::PRectangle status;
	Scintilla::Internal::PRectangle previousButton;
	Scintilla::Internal::PRectangle nextButton;
	Scintilla::Internal::PRectangle closeButton;
};

struct FindBarPointerResult {
	bool consumed = false;
	bool dirty = false;
	/** Field gained focus on this event (caller may capture search origin). */
	bool fieldFocused = false;
	std::vector<FindBarRequest> requests;
};

struct FindBarKeyboardResult {
	bool consumed = false;
	bool dirty = false;
	/** Committed query bytes changed; caller may run incremental search. */
	bool queryChanged = false;
	std::vector<FindBarRequest> requests;
};

struct FindBarTextInputResult {
	bool dirty = false;
	bool queryChanged = false;
};

/**
 * Lay out the full-width band for bandWidth logical pixels at bandTop.
 * Height is FindBarHeight(). Zero or negative width yields an empty layout.
 * Narrow frames shrink or empty controls without negative or overlapping
 * rectangles; buttons keep priority over the status label and field.
 */
[[nodiscard]] FindBarLayout LayoutFindBar(int bandWidth,
	int bandTop = 0) noexcept;

[[nodiscard]] FindBarHitResult HitTestFindBar(const FindBarLayout &layout,
	Scintilla::Internal::Point point) noexcept;

[[nodiscard]] std::string_view FindBarStatusLabel(FindBarStatus status) noexcept;

/**
 * Apply one pointer event. Primary-button press/release activates Previous,
 * Next, and Close only when both land on the same control. A press in the
 * field focuses it and selects the query. Motion and non-primary buttons over
 * the band are consumed without side effects other than clearing a press on
 * leave.
 */
[[nodiscard]] FindBarPointerResult HandleFindBarPointer(FindBarModel &model,
	const FindBarLayout &layout, const PointerInput &input) noexcept;

/**
 * Apply one keyboard event while the field is focused. Enter / Shift+Enter
 * request search; Escape requests close; Left/Right/Home/End, Backspace/Delete,
 * Ctrl+A/C/X/V, and direct UTF-8 text edit the field. When unfocused, nothing
 * is consumed.
 */
[[nodiscard]] FindBarKeyboardResult HandleFindBarKeyboard(FindBarModel &model,
	const KeyboardInput &input);

/**
 * Apply one text-input batch while the field is focused. Cancel drops preedit;
 * deletion uses byte lengths on character boundaries; commit and preedit follow
 * the same order as ApplicationEditor. Unfocused batches only honour cancel.
 */
[[nodiscard]] FindBarTextInputResult HandleFindBarTextInputBatch(
	FindBarModel &model, const ApplicationTextInputBatch &batch);

/**
 * Build the text-input client state for the focused field. surroundingText is
 * the committed query (preedit excluded). cursorRectangle uses the caret
 * position inside layout.fieldText, scrolled so the caret stays visible.
 */
[[nodiscard]] ApplicationTextInputState BuildFindBarTextInputState(
	const FindBarModel &model, const FindBarLayout &layout,
	Scintilla::Internal::Surface &surface,
	const Scintilla::Internal::Font *font);

/**
 * Apply a completed paste into the focused field. Rejects invalid UTF-8 and
 * ignores empty text. Returns true when the committed query changed.
 */
[[nodiscard]] bool ApplyFindBarPaste(FindBarModel &model, std::string_view text);

/**
 * Owns the find-bar font. Construct once beside the shell and reuse across
 * frames. Paints opaque band chrome; does not dim the client.
 */
class FindBarPainter final {
public:
	explicit FindBarPainter(const UiStyle &style = DefaultUiStyle());
	~FindBarPainter() = default;

	FindBarPainter(const FindBarPainter &) = delete;
	FindBarPainter &operator=(const FindBarPainter &) = delete;

	void Paint(Scintilla::Internal::Surface &surface,
		const FindBarLayout &layout,
		const FindBarModel &model) const;

	[[nodiscard]] const Scintilla::Internal::Font *LabelFont() const noexcept {
		return labelFont.get();
	}

	[[nodiscard]] const UiStyle &Style() const noexcept { return style; }

private:
	UiStyle style;
	std::shared_ptr<Scintilla::Internal::Font> labelFont;
};

}

#endif
