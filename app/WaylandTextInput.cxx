// Wayland text-input-v3 state and protocol adapter.

#include "WaylandTextInput.h"

#include <stdexcept>
#include <utility>

#include "WaylandTransfer.h"
#include "text-input-client-protocol.h"

namespace Scalpel {

namespace {

constexpr std::size_t MaximumSurroundingBytes = 4000;

bool IsUtf8Boundary(const std::string &text, int32_t offset) noexcept {
	if (offset < 0 || static_cast<std::size_t>(offset) > text.size()) {
		return false;
	}
	return static_cast<std::size_t>(offset) == text.size() ||
		(static_cast<unsigned char>(text[static_cast<std::size_t>(offset)]) & 0xC0U) != 0x80U;
}

}

std::vector<WaylandTextInputRequest> WaylandTextInputState::Attach() {
	attached = true;
	entered = false;
	enabled = false;
	acceptsState = true;
	stateDirty = clientState.has_value();
	resetState = false;
	commitSerial = 0;
	ClearPending();
	return Reconcile();
}

void WaylandTextInputState::Detach() {
	if (attached || enabled || pendingPreedit || pendingCommit || pendingDeletion) {
		QueueCancellation();
	}
	attached = false;
	entered = false;
	enabled = false;
	acceptsState = true;
	commitSerial = 0;
	resetState = false;
	ClearPending();
}

std::vector<WaylandTextInputRequest> WaylandTextInputState::Enter() {
	if (!attached) {
		return {};
	}
	entered = true;
	stateDirty = clientState.has_value();
	return Reconcile();
}

void WaylandTextInputState::Leave() {
	if (!attached || !entered) {
		return;
	}
	entered = false;
	enabled = false;
	acceptsState = true;
	ClearPending();
	QueueCancellation();
}

std::vector<WaylandTextInputRequest> WaylandTextInputState::SetKeyboardFocus(bool focused) {
	if (keyboardFocused == focused) {
		return {};
	}
	keyboardFocused = focused;
	if (!focused) {
		ClearPending();
		QueueCancellation();
	}
	return Reconcile();
}

std::vector<WaylandTextInputRequest> WaylandTextInputState::UpdateClientState(
	WaylandTextInputClientState state_) {
	if ((state_.surroundingText &&
			(state_.surroundingText->size() > MaximumSurroundingBytes ||
				!IsValidWaylandText(*state_.surroundingText) ||
				!IsUtf8Boundary(*state_.surroundingText, state_.cursor) ||
				!IsUtf8Boundary(*state_.surroundingText, state_.anchor))) ||
		state_.cursorRectangle.width < 0 || state_.cursorRectangle.height < 0) {
		throw std::invalid_argument("invalid Wayland text input client state");
	}
	if (!clientState || *clientState != state_) {
		if (clientState &&
			clientState->surroundingText.has_value() !=
				state_.surroundingText.has_value()) {
			resetState = true;
		}
		clientState = std::move(state_);
		stateDirty = true;
	}
	return Reconcile();
}

void WaylandTextInputState::RecordPreedit(
	const char *text, int32_t cursorBegin, int32_t cursorEnd) {
	const std::string copied = text ? text : "";
	if (!IsValidWaylandText(copied) ||
		!((cursorBegin == -1 && cursorEnd == -1) ||
			(IsUtf8Boundary(copied, cursorBegin) &&
				IsUtf8Boundary(copied, cursorEnd)))) {
		pendingInvalid = true;
		return;
	}
	pendingPreedit = WaylandTextInputPreedit{copied, cursorBegin, cursorEnd};
}

void WaylandTextInputState::RecordCommit(const char *text) {
	const std::string copied = text ? text : "";
	if (!IsValidWaylandText(copied)) {
		pendingInvalid = true;
		return;
	}
	pendingCommit = copied;
}

void WaylandTextInputState::RecordDelete(
	uint32_t beforeLength, uint32_t afterLength) {
	pendingDeletion = WaylandTextInputDelete{beforeLength, afterLength};
}

void WaylandTextInputState::Done(uint32_t serial) {
	if (!attached || !entered) {
		ClearPending();
		return;
	}
	const bool matching = serial == commitSerial;
	WaylandTextInputBatch batch;
	batch.serial = serial;
	batch.refreshState = matching;
	if (pendingInvalid) {
		batch.cancel = true;
	} else {
		batch.preedit = std::move(pendingPreedit);
		batch.commit = std::move(pendingCommit);
		batch.deletion = pendingDeletion;
	}
	batches.push_back(std::move(batch));
	ClearPending();
	acceptsState = matching;
	if (matching) {
		stateDirty = clientState.has_value();
	}
}

std::vector<WaylandTextInputBatch> WaylandTextInputState::TakeBatches() {
	return std::exchange(batches, {});
}

std::vector<WaylandTextInputRequest> WaylandTextInputState::Reconcile() {
	std::vector<WaylandTextInputRequest> requests;
	const bool shouldEnable =
		attached && entered && keyboardFocused && clientState.has_value();
	if (!shouldEnable) {
		if (enabled && attached && entered) {
			requests.push_back({WaylandTextInputRequestType::Disable, std::nullopt});
			requests.push_back({WaylandTextInputRequestType::Commit, std::nullopt});
			commitSerial++;
		}
		enabled = false;
		return requests;
	}
	if (!enabled) {
		requests.push_back({WaylandTextInputRequestType::Enable, std::nullopt});
		requests.push_back({WaylandTextInputRequestType::State, clientState});
		requests.push_back({WaylandTextInputRequestType::Commit, std::nullopt});
		commitSerial++;
		enabled = true;
		stateDirty = false;
		resetState = false;
		return requests;
	}
	if (stateDirty && acceptsState) {
		if (resetState) {
			requests.push_back({WaylandTextInputRequestType::Enable, std::nullopt});
		}
		requests.push_back({WaylandTextInputRequestType::State, clientState});
		requests.push_back({WaylandTextInputRequestType::Commit, std::nullopt});
		commitSerial++;
		stateDirty = false;
		resetState = false;
	}
	return requests;
}

void WaylandTextInputState::QueueCancellation() {
	WaylandTextInputBatch batch;
	batch.serial = commitSerial;
	batch.cancel = true;
	batches.push_back(std::move(batch));
}

void WaylandTextInputState::ClearPending() noexcept {
	pendingPreedit.reset();
	pendingCommit.reset();
	pendingDeletion.reset();
	pendingInvalid = false;
}

const zwp_text_input_v3_listener WaylandTextInput::textInputListener = {
	WaylandTextInput::Enter,
	WaylandTextInput::Leave,
	WaylandTextInput::PreeditString,
	WaylandTextInput::CommitString,
	WaylandTextInput::DeleteSurroundingText,
	WaylandTextInput::Done,
};

WaylandTextInput::~WaylandTextInput() noexcept {
	DestroyTextInput();
}

bool WaylandTextInput::SetManager(zwp_text_input_manager_v3 *manager_) {
	if (manager == manager_) {
		return true;
	}
	DestroyTextInput();
	manager = manager_;
	return RecreateTextInput();
}

bool WaylandTextInput::SetSeat(wl_seat *seat_) {
	if (seat == seat_) {
		return true;
	}
	DestroyTextInput();
	seat = seat_;
	return RecreateTextInput();
}

void WaylandTextInput::SetKeyboardFocus(bool focused) {
	Apply(state.SetKeyboardFocus(focused));
}

void WaylandTextInput::UpdateClientState(WaylandTextInputClientState state_) {
	Apply(state.UpdateClientState(std::move(state_)));
}

void WaylandTextInput::DestroyTextInput() noexcept {
	state.Detach();
	if (textInput) {
		zwp_text_input_v3_destroy(textInput);
		textInput = nullptr;
	}
}

bool WaylandTextInput::RecreateTextInput() {
	if (!manager || !seat) {
		return true;
	}
	textInput = zwp_text_input_manager_v3_get_text_input(manager, seat);
	if (!textInput ||
		zwp_text_input_v3_add_listener(textInput, &textInputListener, this) != 0) {
		if (textInput) {
			zwp_text_input_v3_destroy(textInput);
			textInput = nullptr;
		}
		return false;
	}
	Apply(state.Attach());
	return true;
}

void WaylandTextInput::Apply(
	const std::vector<WaylandTextInputRequest> &requests) {
	if (!textInput) {
		return;
	}
	for (const WaylandTextInputRequest &request : requests) {
		switch (request.type) {
		case WaylandTextInputRequestType::Enable:
			zwp_text_input_v3_enable(textInput);
			zwp_text_input_v3_set_content_type(textInput,
				ZWP_TEXT_INPUT_V3_CONTENT_HINT_MULTILINE,
				ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
			break;
		case WaylandTextInputRequestType::Disable:
			zwp_text_input_v3_disable(textInput);
			break;
		case WaylandTextInputRequestType::State: {
			const WaylandTextInputClientState &client = *request.state;
			if (client.surroundingText) {
				zwp_text_input_v3_set_surrounding_text(textInput,
					client.surroundingText->c_str(), client.cursor, client.anchor);
			}
			zwp_text_input_v3_set_text_change_cause(textInput,
				client.changeCause == WaylandTextChangeCause::InputMethod ?
					ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD :
					ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER);
			zwp_text_input_v3_set_cursor_rectangle(textInput,
				client.cursorRectangle.x, client.cursorRectangle.y,
				client.cursorRectangle.width, client.cursorRectangle.height);
			break;
		}
		case WaylandTextInputRequestType::Commit:
			zwp_text_input_v3_commit(textInput);
			break;
		}
	}
}

void WaylandTextInput::Enter(
	void *data, zwp_text_input_v3 *, wl_surface *surface) {
	auto &input = *static_cast<WaylandTextInput *>(data);
	if (surface == input.mainSurface) {
		input.Apply(input.state.Enter());
	}
}

void WaylandTextInput::Leave(
	void *data, zwp_text_input_v3 *, wl_surface *surface) {
	auto &input = *static_cast<WaylandTextInput *>(data);
	if (surface == input.mainSurface) {
		input.state.Leave();
	}
}

void WaylandTextInput::PreeditString(void *data, zwp_text_input_v3 *,
	const char *text, int32_t cursorBegin, int32_t cursorEnd) {
	static_cast<WaylandTextInput *>(data)->state.RecordPreedit(
		text, cursorBegin, cursorEnd);
}

void WaylandTextInput::CommitString(
	void *data, zwp_text_input_v3 *, const char *text) {
	static_cast<WaylandTextInput *>(data)->state.RecordCommit(text);
}

void WaylandTextInput::DeleteSurroundingText(void *data, zwp_text_input_v3 *,
	uint32_t beforeLength, uint32_t afterLength) {
	static_cast<WaylandTextInput *>(data)->state.RecordDelete(
		beforeLength, afterLength);
}

void WaylandTextInput::Done(
	void *data, zwp_text_input_v3 *, uint32_t serial) {
	static_cast<WaylandTextInput *>(data)->state.Done(serial);
}

}
