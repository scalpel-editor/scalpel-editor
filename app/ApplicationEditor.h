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

#include "ApplicationPlatform.h"
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

	void LoadInitialBuffer(std::string_view text);
	[[nodiscard]] std::string Text() const;
	void Resize(int width, int height);
	void SetKeyboardFocus(bool focused);
	void HandleKeyboardInput(const KeyboardInput &input);
	void HandlePointerInput(const PointerInput &input);
	void SetPointerCapture(bool captured);
	void RequestClipboardCopy();
	[[nodiscard]] bool ClipboardPasteAvailable();
	void RenderFrame();
	void PresentFrame();
	void RunPendingWork();
	[[nodiscard]] std::optional<std::chrono::milliseconds> TimeUntilNextWork() const;
	[[nodiscard]] bool NeedsRedraw() const noexcept;

	[[nodiscard]] std::vector<uint8_t> FramePixels() const;
	[[nodiscard]] int FrameWidth() const noexcept;
	[[nodiscard]] int FrameHeight() const noexcept;
	[[nodiscard]] const ApplicationWindow &WindowState() const noexcept { return window; }
	[[nodiscard]] const ScrollState &Scrollbars() const noexcept { return scrollbars; }
	[[nodiscard]] const std::vector<Scintilla::Notification> &Notifications() const noexcept { return notifications; }
	[[nodiscard]] const std::vector<TickerRequest> &TickerRequests() const noexcept { return tickerRequests; }
	[[nodiscard]] const std::vector<std::string> &UnsupportedRequests() const noexcept { return unsupportedRequests; }
	[[nodiscard]] int ChangeNotifications() const noexcept { return changeNotifications; }
	[[nodiscard]] bool IdleRequested() const noexcept { return idleRequested; }

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
	void RecordUnsupported(std::string request);

	ScrollState scrollbars;
	static constexpr std::size_t tickerReasonCount = static_cast<std::size_t>(TickReason::platform) + 1;
	std::array<FineTickerState, tickerReasonCount> tickers{};
	std::vector<TickerRequest> tickerRequests;
	std::vector<Scintilla::Notification> notifications;
	std::vector<std::string> unsupportedRequests;
	int changeNotifications = 0;
	bool idleRequested = false;
	bool queuedIdleWork = false;
	NowFunction now;
	double horizontalWheelRemainder = 0;
	double verticalWheelRemainder = 0;
};

}

#endif
