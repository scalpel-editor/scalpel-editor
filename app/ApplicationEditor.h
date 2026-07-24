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
#include <vector>

#include "ApplicationClipboard.h"
#include "ApplicationPlatform.h"
#include "ApplicationPrimarySelection.h"
#include "ApplicationTextInput.h"
#include "ApplicationInput.h"
#include "EditorNotifications.h"
#include "ScintillaBase.h"

namespace Scintilla::Internal {
class DrawSurface;
class GlContext;
class Renderer;
}

namespace Scalpel {

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

struct ScrollState {
	int horizontalPosition = 0;
	Scintilla::Line verticalPosition = 0;
	Scintilla::Line maximum = 0;
	Scintilla::Line page = 0;
	int horizontalUpdates = 0;
	int verticalUpdates = 0;
	int changes = 0;
	int reconfigurations = 0;
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
	 * Replace the whole document for open or startup. Cancels text input,
	 * bumps the document generation used by asynchronous paste, clears undo,
	 * and marks the buffer clean.
	 */
	void LoadInitialBuffer(std::string_view text);
	[[nodiscard]] std::string Text() const;
	/** True when the document is not at its save point. */
	[[nodiscard]] bool Modified() const noexcept;
	/** Mark the current document bytes as saved without clearing undo. */
	void MarkSaved();
	// Document line count used for the gutter and status (Scintilla's line count).
	[[nodiscard]] Scintilla::Line LineCount() const noexcept;
	// Effective pixel width of the left fixed column (margins plus text-left gap).
	[[nodiscard]] int LineNumberMarginWidth() const noexcept;
	// Blank gap between the numbered margins and the text.
	[[nodiscard]] int TextLeftGap() const noexcept;
	// Font family name configured for a style (empty when unset).
	[[nodiscard]] std::string StyleFontName(int style);
	// Logical editor size and framebuffer pixel size change independently.
	void Resize(int width, int height);
	void SetFrameBufferSize(int width, int height);
	void SetKeyboardFocus(bool focused);
	void HandleKeyboardInput(const KeyboardInput &input);
	void HandlePointerInput(const PointerInput &input);
	void SetPointerCapture(bool captured);
	void RequestClipboardCopy();
	void RequestClipboardPaste();
	void SetClipboardPasteAvailable(bool available) noexcept;
	[[nodiscard]] bool ClipboardPasteAvailable();
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
	 * Optional post-paint chrome. Called after a successful Scintilla Paint on
	 * the same surface, with logical frame width and height, before swap.
	 * While set, PresentFrame and RenderFrame expand paint to the full client
	 * and PresentFrame uses a full buffer swap so alpha overlays do not darken
	 * preserved pixels outside partial damage.
	 */
	using OverlayPainter = std::function<void(Scintilla::Internal::Surface &surface,
		int width, int height)>;
	void SetOverlayPainter(OverlayPainter painter);
	/** Full-client invalidate so modal chrome appears or disappears cleanly. */
	void InvalidateClient();
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
	[[nodiscard]] const ScrollState &Scrollbars() const noexcept { return scrollbars; }
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
	void ConfigureLineNumberMargins();
	void ApplyLineNumberStyle();
	void UpdateLineNumberWidth();
	void RecordUnsupported(std::string request);
	void QueuePrimarySelectionClaim(std::optional<std::string> text);
	void RequestPrimarySelectionPaste(Scintilla::Position position);
	[[nodiscard]] ApplicationTextInputState BuildTextInputState();
	[[nodiscard]] bool DeleteTextInputSurrounding(
		const ApplicationTextInputDelete &deletion);
	void CancelTextInput();

	ScrollState scrollbars;
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
	double horizontalWheelRemainder = 0;
	double verticalWheelRemainder = 0;
	int bufferWidth = 0;
	int bufferHeight = 0;
};

}

#endif
