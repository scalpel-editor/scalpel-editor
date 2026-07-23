// Wayland text-input-v3 state and protocol adapter.

#ifndef WAYLANDTEXTINPUT_H
#define WAYLANDTEXTINPUT_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct wl_seat;
struct wl_surface;
struct zwp_text_input_manager_v3;
struct zwp_text_input_v3;
struct zwp_text_input_v3_listener;

namespace Scalpel {

struct WaylandTextInputRectangle {
	int32_t x = 0;
	int32_t y = 0;
	int32_t width = 0;
	int32_t height = 0;

	friend constexpr bool operator==(const WaylandTextInputRectangle &left,
		const WaylandTextInputRectangle &right) noexcept {
		return left.x == right.x && left.y == right.y &&
			left.width == right.width && left.height == right.height;
	}
};

enum class WaylandTextChangeCause {
	InputMethod,
	Other,
};

struct WaylandTextInputClientState {
	std::string surroundingText;
	int32_t cursor = 0;
	int32_t anchor = 0;
	WaylandTextInputRectangle cursorRectangle;
	WaylandTextChangeCause changeCause = WaylandTextChangeCause::Other;

	friend bool operator==(const WaylandTextInputClientState &left,
		const WaylandTextInputClientState &right) noexcept {
		return left.surroundingText == right.surroundingText &&
			left.cursor == right.cursor && left.anchor == right.anchor &&
			left.cursorRectangle == right.cursorRectangle &&
			left.changeCause == right.changeCause;
	}
	friend bool operator!=(const WaylandTextInputClientState &left,
		const WaylandTextInputClientState &right) noexcept {
		return !(left == right);
	}
};

struct WaylandTextInputPreedit {
	std::string text;
	int32_t cursorBegin = 0;
	int32_t cursorEnd = 0;
};

struct WaylandTextInputDelete {
	uint32_t beforeLength = 0;
	uint32_t afterLength = 0;
};

struct WaylandTextInputBatch {
	std::optional<WaylandTextInputPreedit> preedit;
	std::optional<std::string> commit;
	std::optional<WaylandTextInputDelete> deletion;
	uint32_t serial = 0;
	bool refreshState = false;
	bool cancel = false;
};

enum class WaylandTextInputRequestType {
	Enable,
	Disable,
	State,
	Commit,
};

struct WaylandTextInputRequest {
	WaylandTextInputRequestType type;
	std::optional<WaylandTextInputClientState> state;
};

/** Plain text-input state driven by copied protocol events. */
class WaylandTextInputState final {
public:
	[[nodiscard]] std::vector<WaylandTextInputRequest> Attach();
	void Detach();
	[[nodiscard]] std::vector<WaylandTextInputRequest> Enter();
	void Leave();
	[[nodiscard]] std::vector<WaylandTextInputRequest> SetKeyboardFocus(bool focused);
	[[nodiscard]] std::vector<WaylandTextInputRequest> UpdateClientState(
		WaylandTextInputClientState state);

	void RecordPreedit(const char *text, int32_t cursorBegin, int32_t cursorEnd);
	void RecordCommit(const char *text);
	void RecordDelete(uint32_t beforeLength, uint32_t afterLength);
	void Done(uint32_t serial);

	[[nodiscard]] std::vector<WaylandTextInputBatch> TakeBatches();
	[[nodiscard]] uint32_t CommitSerial() const noexcept { return commitSerial; }
	[[nodiscard]] bool Enabled() const noexcept { return enabled; }

private:
	[[nodiscard]] std::vector<WaylandTextInputRequest> Reconcile();
	void QueueCancellation();
	void ClearPending() noexcept;

	std::optional<WaylandTextInputClientState> clientState;
	std::optional<WaylandTextInputPreedit> pendingPreedit;
	std::optional<std::string> pendingCommit;
	std::optional<WaylandTextInputDelete> pendingDeletion;
	std::vector<WaylandTextInputBatch> batches;
	uint32_t commitSerial = 0;
	bool attached = false;
	bool entered = false;
	bool keyboardFocused = false;
	bool enabled = false;
	bool stateDirty = false;
	bool acceptsState = true;
	bool pendingInvalid = false;
};

/** Owns the active seat's text-input-v3 object and its thin listener. */
class WaylandTextInput final {
public:
	WaylandTextInput() = default;
	~WaylandTextInput() noexcept;

	WaylandTextInput(const WaylandTextInput &) = delete;
	WaylandTextInput(WaylandTextInput &&) = delete;
	WaylandTextInput &operator=(const WaylandTextInput &) = delete;
	WaylandTextInput &operator=(WaylandTextInput &&) = delete;

	[[nodiscard]] bool SetManager(zwp_text_input_manager_v3 *manager);
	[[nodiscard]] bool SetSeat(wl_seat *seat);
	void SetSurface(wl_surface *surface) noexcept { mainSurface = surface; }
	void SetKeyboardFocus(bool focused);
	void UpdateClientState(WaylandTextInputClientState state);
	[[nodiscard]] std::vector<WaylandTextInputBatch> TakeBatches() {
		return state.TakeBatches();
	}

private:
	void DestroyTextInput() noexcept;
	[[nodiscard]] bool RecreateTextInput();
	void Apply(const std::vector<WaylandTextInputRequest> &requests);

	static void Enter(void *data, zwp_text_input_v3 *textInput, wl_surface *surface);
	static void Leave(void *data, zwp_text_input_v3 *textInput, wl_surface *surface);
	static void PreeditString(void *data, zwp_text_input_v3 *textInput,
		const char *text, int32_t cursorBegin, int32_t cursorEnd);
	static void CommitString(void *data, zwp_text_input_v3 *textInput, const char *text);
	static void DeleteSurroundingText(void *data, zwp_text_input_v3 *textInput,
		uint32_t beforeLength, uint32_t afterLength);
	static void Done(void *data, zwp_text_input_v3 *textInput, uint32_t serial);

	static const struct zwp_text_input_v3_listener textInputListener;

	WaylandTextInputState state;
	zwp_text_input_manager_v3 *manager = nullptr;
	wl_seat *seat = nullptr;
	wl_surface *mainSurface = nullptr;
	zwp_text_input_v3 *textInput = nullptr;
};

}

#endif
