// Production ScintillaBase host used by the standalone application.

#ifndef APPLICATIONEDITOR_H
#define APPLICATIONEDITOR_H

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ApplicationClipboard.h"
#include "ApplicationPlatform.h"
#include "ApplicationPrimarySelection.h"
#include "ApplicationTextInput.h"
#include "ApplicationInput.h"
#include "DocumentId.h"
#include "EditorNotifications.h"
#include "ScrollMetrics.h"
#include "ScintillaBase.h"

namespace Scintilla::Internal {
class Document;
class DrawSurface;
class GlContext;
class Renderer;
}

namespace Scalpel {

/**
 * Process-wide generic editor text face. Values map to Fontconfig family
 * strings (monospace, serif, sans-serif, system-ui); the host resolves them.
 */
enum class EditorFont {
	Monospace,
	Serif,
	Sans,
	System,
};

/** Canonical Fontconfig family string for a generic editor-font choice. */
[[nodiscard]] const char *EditorFontFamilyName(EditorFont font) noexcept;

struct TickerRequest {
	int reason = 0;
	int milliseconds = 0;
	int tolerance = 0;
	bool running = false;
};

struct FineTickerState {
	std::chrono::steady_clock::time_point nextFire{};
	std::chrono::milliseconds period{};
	bool running = false;
};

class ApplicationResources {
protected:
	ApplicationResources(int width, int height);
	ApplicationResources(std::unique_ptr<Scintilla::Internal::GlContext> context,
		int width, int height);
	~ApplicationResources();

	ApplicationWindow window;
	std::unique_ptr<Scintilla::Internal::GlContext> glContext;
	std::unique_ptr<Scintilla::Internal::Renderer> renderer;
	std::unique_ptr<Scintilla::Internal::DrawSurface> frame;
};

/**
 * The application-facing editor host used by both test and Wayland targets.
 *
 * It owns the editor window state and an injected or default headless EGL
 * renderer. ApplicationResources is the first base so it is destroyed after
 * ScintillaBase releases its cached drawing objects.
 *
 * Multiple Scintilla documents can be retained and switched without
 * constructing another host: one document is active for input and paint; the
 * rest keep their text, undo, save point, and a snapshot of selection and
 * scroll until activated again. DocumentWorkspace maps tabs and file paths onto
 * these IDs. ApplicationUi owns chrome and overlay selection state; the shell
 * paints the menu bar and tab strip as permanent top chrome and binds the
 * file-error card, unsaved-changes card, or open menu dropdown into the
 * post-paint overlay slot (file error first, then unsaved card, then menu).
 */
class ApplicationEditor final : private ApplicationResources, public Scintilla::Internal::ScintillaBase {
public:
	using Clock = std::chrono::steady_clock;
	using NowFunction = std::function<Clock::time_point()>;

	explicit ApplicationEditor(int width = 800, int height = 600,
		NowFunction now = Clock::now);
	ApplicationEditor(std::unique_ptr<Scintilla::Internal::GlContext> context,
		int width, int height, NowFunction now = Clock::now);
	~ApplicationEditor() override;

	ApplicationEditor(const ApplicationEditor &) = delete;
	ApplicationEditor(ApplicationEditor &&) = delete;
	ApplicationEditor &operator=(const ApplicationEditor &) = delete;
	ApplicationEditor &operator=(ApplicationEditor &&) = delete;

	/**
	 * Create an empty retained document without changing the active one.
	 * The caller activates it when the tab should become visible.
	 */
	[[nodiscard]] DocumentId CreateDocument();
	[[nodiscard]] DocumentId ActiveDocument() const noexcept;
	[[nodiscard]] bool HasDocument(DocumentId id) const noexcept;
	/**
	 * Make id active. Cancels tentative IME on the outgoing document,
	 * snapshots its selection and scroll, bumps the paste generation, switches
	 * the watched document, restores the incoming view, updates the line-number
	 * margin, and marks text-input state dirty. No-op when already active.
	 */
	void ActivateDocument(DocumentId id);
	/**
	 * Drop the table reference for id. The document must not be active;
	 * activate another first. Releases exactly the retained reference.
	 */
	void CloseDocument(DocumentId id);

	/**
	 * Replace the whole active document for open or startup. Cancels text
	 * input, bumps the document generation used by asynchronous paste, clears
	 * undo, and marks the buffer clean.
	 */
	void LoadInitialBuffer(std::string_view text);
	[[nodiscard]] std::string Text() const;
	[[nodiscard]] std::string Text(DocumentId id) const;
	/** True when the active document is not at its save point. */
	[[nodiscard]] bool Modified() const noexcept;
	[[nodiscard]] bool Modified(DocumentId id) const;
	/** Mark the active document bytes as saved without clearing undo. */
	void MarkSaved();
	void MarkSaved(DocumentId id);
	// Document line count used for the gutter and status (Scintilla's line count).
	[[nodiscard]] Scintilla::Line LineCount() const noexcept;
	// Effective pixel width of the left fixed column (margins plus text-left gap).
	[[nodiscard]] int LineNumberMarginWidth() const noexcept;
	// Blank gap between the numbered margins and the text.
	[[nodiscard]] int TextLeftGap() const noexcept;
	// Font family name configured for a style (empty when unset).
	[[nodiscard]] std::string StyleFontName(int style);
	/**
	 * Current generic editor text face. Startup default is System (system-ui),
	 * matching Platform::DefaultFont(). Styles belong to this view; the choice
	 * applies across all retained documents.
	 */
	[[nodiscard]] EditorFont CurrentEditorFont() const noexcept;
	/**
	 * Set the generic editor text face. No-op when unchanged. Updates
	 * STYLE_DEFAULT, copies it through plain-text styles, restores the
	 * monospace line-number gutter, and invalidates layout. Does not alter
	 * document bytes, save point, undo history, or selection.
	 */
	void SetEditorFont(EditorFont font);
	// Logical editor size and framebuffer pixel size change independently.
	void Resize(int width, int height);
	void SetFrameBufferSize(int width, int height);
	/**
	 * Reserve a fixed logical-height band at the top of the frame for permanent
	 * chrome (menu bar plus tab strip). Scintilla's client rectangle starts
	 * below the inset and ends left of the vertical bar and above the horizontal
	 * bar when those options are enabled. Pointer and caret coordinates stay in
	 * full-frame space. Zero clears the top reservation.
	 */
	void SetTopChromeInset(int logicalPixels);
	[[nodiscard]] int TopChromeInset() const noexcept { return topChromeInset; }
	/** Full frame bounds in logical pixels: (0, 0, width, height). */
	[[nodiscard]] Scintilla::Internal::PRectangle FrameRectangle() const noexcept;
	/** Top chrome band: (0, 0, width, topChromeInset). Empty when inset is 0. */
	[[nodiscard]] Scintilla::Internal::PRectangle TopChromeRectangle() const noexcept;
	/** Vertical scrollbar track (right edge below top chrome). Empty when hidden. */
	[[nodiscard]] Scintilla::Internal::PRectangle VerticalScrollBarRectangle() const noexcept;
	/** Horizontal scrollbar track (bottom edge left of vertical bar). Empty when hidden. */
	[[nodiscard]] Scintilla::Internal::PRectangle HorizontalScrollBarRectangle() const noexcept;
	/** Corner square when both bars are present. Empty otherwise. */
	[[nodiscard]] Scintilla::Internal::PRectangle JunctionRectangle() const noexcept;
	/** Editor text area excluding permanent chrome (top, side, and bottom bars). */
	[[nodiscard]] Scintilla::Internal::PRectangle EditorClientRectangle() const {
		return GetClientRectangle();
	}
	void SetKeyboardFocus(bool focused);
	void HandleKeyboardInput(const KeyboardInput &input);
	void HandlePointerInput(const PointerInput &input);
	void SetPointerCapture(bool captured);
	/**
	 * Named edit operations for the application action path (menus and matching
	 * shortcuts). They call the retained Scintilla commands; they do not
	 * reimplement editing.
	 */
	void RequestUndo();
	void RequestRedo();
	void RequestCut();
	void RequestSelectAll();
	void RequestClipboardCopy();
	void RequestClipboardPaste();
	void SetClipboardPasteAvailable(bool available) noexcept;
	[[nodiscard]] bool ClipboardPasteAvailable();
	/** True when the active document has a non-empty selection. */
	[[nodiscard]] bool HasSelection() const noexcept;
	/** True when Select All would cover at least one byte. */
	[[nodiscard]] bool CanSelectAll() const noexcept;
	/** True when undo history is available and the document is not read-only. */
	[[nodiscard]] bool CanUndoEdit() const noexcept;
	/** True when redo history is available and the document is not read-only. */
	[[nodiscard]] bool CanRedoEdit() const noexcept;
	/** True when cut is allowed: writable document with a non-empty selection. */
	[[nodiscard]] bool CanCut() const noexcept;
	/** True when copy is allowed: non-empty selection. */
	[[nodiscard]] bool CanCopy() const noexcept;
	[[nodiscard]] std::vector<ApplicationClipboardRequest> TakeClipboardRequests();
	[[nodiscard]] std::vector<ApplicationClipboardResult> TakeClipboardResults();
	void HandleClipboardResult(uint64_t id, ApplicationClipboardOperation operation,
		ApplicationClipboardStatus status, std::string text = {});
	[[nodiscard]] std::vector<ApplicationPrimarySelectionRequest>
		TakePrimarySelectionRequests();
	[[nodiscard]] std::vector<ApplicationPrimarySelectionResult>
		TakePrimarySelectionResults();
	void HandlePrimarySelectionResult(uint64_t id,
		ApplicationPrimarySelectionOperation operation,
		ApplicationPrimarySelectionStatus status, std::string text = {});
	void HandleTextInputBatch(const ApplicationTextInputBatch &batch);
	/** Drop preedit / tentative IME state (for example when a modal prompt opens). */
	void CancelActiveTextInput();
	[[nodiscard]] std::optional<ApplicationTextInputState> TakeTextInputState();
	void RenderFrame();
	void RenderFrame(
		const std::vector<Scintilla::Internal::PRectangle> &damage);
	void PresentFrame();
	void PresentFrame(
		const std::vector<Scintilla::Internal::PRectangle> &damage,
		const std::vector<int> &eglDamage, bool fullSwap);
	/**
	 * Optional post-paint overlay. Called after a successful Scintilla Paint on
	 * the same surface, with logical frame width and height, before swap.
	 * While set, PresentFrame and RenderFrame expand paint to the full frame
	 * (editor client plus top chrome) and PresentFrame uses a full buffer swap
	 * so alpha overlays do not darken preserved pixels outside partial damage.
	 * Call InvalidateFrame when overlay pixels above the client can change.
	 * ApplicationUi binds exactly one of the open menu dropdown, unsaved-
	 * changes card, or file-error card here; modal cards outrank the menu.
	 */
	using OverlayPainter = std::function<void(Scintilla::Internal::Surface &surface,
		int width, int height)>;
	void SetOverlayPainter(OverlayPainter painter) noexcept;
	/**
	 * Opaque permanent chrome (menu bar, tab strip, scrollbars, junction)
	 * painted after Scintilla and before any modal overlay. Only runs when any
	 * permanent chrome rectangle intersects frame damage, or when an overlay
	 * forces a full-frame paint. Does not expand damage or force a full buffer
	 * swap on ordinary editor frames.
	 */
	using PermanentChromePainter = std::function<void(
		Scintilla::Internal::Surface &surface, int width, int height)>;
	void SetPermanentChromePainter(PermanentChromePainter painter) noexcept;
	/** Damage only the top chrome band. */
	void InvalidateTopChrome();
	/** Damage vertical bar, horizontal bar, and junction (when present). */
	void InvalidateScrollBars();
	/** Damage only the vertical scrollbar track. */
	void InvalidateVerticalScrollBar();
	/** Damage only the horizontal scrollbar track. */
	void InvalidateHorizontalScrollBar();
	/** Damage only the editor client (excludes permanent chrome). */
	void InvalidateClient();
	/** Damage the complete logical frame, including permanent chrome. */
	void InvalidateFrame();
	[[nodiscard]] std::vector<Scintilla::Internal::PRectangle> TakeFrameDamage();
	void RunPendingWork();
	[[nodiscard]] std::optional<std::chrono::milliseconds> TimeUntilNextWork() const;
	[[nodiscard]] bool NeedsRedraw() const noexcept;

	[[nodiscard]] std::vector<uint8_t> FramePixels() const;
	[[nodiscard]] int FrameWidth() const noexcept;
	[[nodiscard]] int FrameHeight() const noexcept;
	[[nodiscard]] int BufferWidth() const noexcept { return bufferWidth; }
	[[nodiscard]] int BufferHeight() const noexcept { return bufferHeight; }
	[[nodiscard]] int BufferAge() const noexcept;
	[[nodiscard]] bool BufferAgeSupported() const noexcept;
	[[nodiscard]] bool DamageSwapSupported() const noexcept;
	[[nodiscard]] Scintilla::Internal::PRectangle LastPaintRectangle() const noexcept {
		return rcPaint;
	}
	[[nodiscard]] const ApplicationWindow &WindowState() const noexcept { return window; }
	[[nodiscard]] const ScrollMetrics &Scrollbars() const noexcept { return scrollbars; }
	/**
	 * Scroll the view so the given display line is first visible. Clamps to the
	 * current vertical upper bound.
	 */
	void ScrollVerticalTo(Scintilla::Line line);
	/**
	 * Scroll horizontally to the given pixel offset. Clamps to the current
	 * horizontal upper bound (zero while wrapping).
	 */
	void ScrollHorizontalTo(int xPos);
	[[nodiscard]] const std::vector<Scintilla::Notification> &Notifications() const noexcept { return notifications; }
	[[nodiscard]] const std::vector<TickerRequest> &TickerRequests() const noexcept { return tickerRequests; }
	[[nodiscard]] const std::vector<std::string> &UnsupportedRequests() const noexcept { return unsupportedRequests; }
	[[nodiscard]] const std::vector<ApplicationClipboardResult> &ClipboardResults() const noexcept {
		return clipboardResults;
	}
	[[nodiscard]] const std::vector<ApplicationPrimarySelectionResult> &
		PrimarySelectionResults() const noexcept {
		return primarySelectionResults;
	}
	[[nodiscard]] int ChangeNotifications() const noexcept { return changeNotifications; }
	[[nodiscard]] bool IdleRequested() const noexcept { return idleRequested; }
	[[nodiscard]] int ImeIndicatorAt(Scintilla::Position position) const;

protected:
	/**
	 * Editor text area only: below top chrome, left of the vertical bar, and
	 * above the horizontal bar. At least one logical client pixel is retained
	 * on each axis when the frame permits it.
	 */
	[[nodiscard]] Scintilla::Internal::PRectangle GetClientRectangle() const override;

	void SetHorizontalScrollPos() override;
	void SetVerticalScrollPos() override;
	bool ModifyScrollBars(Scintilla::Line maximum, Scintilla::Line page) override;
	void ReconfigureScrollBars() override;

	void Copy() override;
	void Paste() override;
	void ClaimSelection() override;
	void CopyToClipboard(const Scintilla::Internal::SelectionText &selectedText) override;
	void StartDrag() override;

	void NotifyChange() override;
	void NotifyParent(Scintilla::NotificationData notification) override;
	void NotifyCaretMove() override;
	void UpdateSystemCaret() override;
	void SetMouseCapture(bool captured) override;
	bool HaveMouseCapture() override;

	void CreateCallTipWindow(Scintilla::Internal::PRectangle rectangle) override;
	void AddToPopUp(const char *label, int command = 0, bool enabled = true) override;

	bool FineTickerRunning(TickReason reason) override;
	void FineTickerStart(TickReason reason, int milliseconds, int tolerance) override;
	void FineTickerCancel(TickReason reason) override;
	bool SetIdle(bool enabled) override;
	void QueueIdleWork(Scintilla::Internal::WorkItems items, Scintilla::Position upTo = 0) override;

private:
	struct RetainedDocument {
		Scintilla::Internal::Document *document = nullptr;
		std::string selection;
		Scintilla::Line firstVisibleLine = 0;
		Scintilla::Line firstVisibleDocumentLine = 0;
		Scintilla::Line firstVisibleSubLine = 0;
		int xOffset = 0;
	};

	void ConfigureLineNumberMargins();
	void ApplyLineNumberStyle();
	void UpdateLineNumberWidth();
	void RetainInitialDocument();
	void ReleaseRetainedDocuments();
	void SnapshotActiveView();
	void RestoreActiveView();
	[[nodiscard]] RetainedDocument *FindRetained(DocumentId id);
	[[nodiscard]] const RetainedDocument *FindRetained(DocumentId id) const;
	void RecordUnsupported(std::string request);
	void QueuePrimarySelectionClaim(std::optional<std::string> text);
	void RequestPrimarySelectionPaste(Scintilla::Position position);
	[[nodiscard]] ApplicationTextInputState BuildTextInputState();
	[[nodiscard]] bool DeleteTextInputSurrounding(
		const ApplicationTextInputDelete &deletion);
	void CancelTextInput();
	void ExecuteApplicationEdit(
		Scintilla::Internal::EditorCommand command);
	void RefreshScrollMetrics();
	[[nodiscard]] Scintilla::Line HorizontalUpperBound() const;
	[[nodiscard]] bool PermanentChromePresent() const noexcept;
	[[nodiscard]] bool DamageIntersectsPermanentChrome(
		const std::vector<Scintilla::Internal::PRectangle> &damage) const noexcept;

	std::unordered_map<DocumentId, RetainedDocument> retainedDocuments;
	DocumentId activeDocumentId = 0;
	DocumentId nextDocumentId = 1;
	EditorFont editorFont = EditorFont::System;
	ScrollMetrics scrollbars;
	static constexpr std::size_t tickerReasonCount = static_cast<std::size_t>(TickReason::platform) + 1;
	std::array<FineTickerState, tickerReasonCount> tickers{};
	std::vector<TickerRequest> tickerRequests;
	std::vector<Scintilla::Notification> notifications;
	std::vector<std::string> unsupportedRequests;
	std::vector<ApplicationClipboardRequest> clipboardRequests;
	std::vector<ApplicationClipboardResult> clipboardResults;
	std::vector<ApplicationPrimarySelectionRequest> primarySelectionRequests;
	std::vector<ApplicationPrimarySelectionResult> primarySelectionResults;
	struct PendingPaste {
		uint64_t id = 0;
		uint64_t documentGeneration = 0;
	};
	std::optional<PendingPaste> pendingPaste;
	struct PendingPrimaryPaste {
		uint64_t id = 0;
		uint64_t documentGeneration = 0;
		Scintilla::Position position = 0;
	};
	std::optional<PendingPrimaryPaste> pendingPrimaryPaste;
	struct PendingPrimaryClaim {
		std::optional<std::string> text;
	};
	std::optional<PendingPrimaryClaim> pendingPrimaryClaim;
	struct TextInputPreeditRange {
		Scintilla::Position start = 0;
		Scintilla::Position length = 0;
	};
	std::optional<TextInputPreeditRange> textInputPreeditRange;
	uint64_t nextClipboardRequest = 1;
	uint64_t nextPrimarySelectionRequest = 1;
	uint64_t latestPrimarySelectionClaimRequest = 0;
	uint64_t documentGeneration = 0;
	bool primarySelectionClaimed = false;
	bool suppressPrimarySelectionClaim = false;
	bool clipboardPasteAvailable = false;
	int changeNotifications = 0;
	bool idleRequested = false;
	bool queuedIdleWork = false;
	bool textInputStateDirty = true;
	ApplicationTextChangeCause textInputChangeCause =
		ApplicationTextChangeCause::Other;
	NowFunction now;
	OverlayPainter overlayPainter;
	PermanentChromePainter permanentChromePainter;
	int topChromeInset = 0;
	double horizontalWheelRemainder = 0;
	double verticalWheelRemainder = 0;
	int bufferWidth = 0;
	int bufferHeight = 0;
};

}

#endif
