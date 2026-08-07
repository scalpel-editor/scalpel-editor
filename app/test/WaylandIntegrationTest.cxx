#include <array>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "ApplicationEditor.h"
#include "WaylandClipboard.h"
#include "WaylandCursor.h"
#include "WaylandEventLoop.h"
#include "WaylandFrame.h"
#include "WaylandInput.h"
#include "WaylandLifecycle.h"
#include "WaylandPrimarySelection.h"
#include "WaylandScale.h"
#include "WaylandTextInput.h"
#include "WaylandTransfer.h"

using namespace std::chrono_literals;

namespace {

struct TestKeymap {
	std::string text;
};

TestKeymap MakeTestKeymap(const char *variant = nullptr) {
	xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	REQUIRE(context);
	const xkb_rule_names names{nullptr, nullptr, "us", variant, nullptr};
	xkb_keymap *keymap = xkb_keymap_new_from_names(
		context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	REQUIRE(keymap);
	char *text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
	REQUIRE(text);
	TestKeymap result;
	result.text = text;
	result.text.push_back('\0');
	std::free(text);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	return result;
}

struct Pipe {
	std::array<int, 2> descriptors{-1, -1};

	Pipe() {
		REQUIRE(pipe(descriptors.data()) == 0);
	}
	~Pipe() {
		for (const int descriptor : descriptors) {
			if (descriptor >= 0) {
				(void)close(descriptor);
			}
		}
	}

	int TakeRead() {
		return std::exchange(descriptors[0], -1);
	}
};

Scalpel::WaylandTextInputClientState TextInputClientState() {
	return {"base", 4, 4, {40, 20, 1, 16},
		Scalpel::WaylandTextChangeCause::Other};
}

}

TEST_CASE("Wayland shell integration removes a seat during compose and repeat") {
	const TestKeymap keymap = MakeTestKeymap("intl");
	Scalpel::WaylandInput::Clock::time_point now{};
	Scalpel::WaylandInput input("C.utf8", [&now] { return now; });
	REQUIRE(input.SetKeymap(keymap.text));
	REQUIRE(input.SetRepeatInfo(100, 10));
	input.RecordKeyboardFocus(true);

	Scalpel::WaylandLifecycle lifecycle(800, 600);
	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 40, 9);
	(void)lifecycle.UpdateSeatCapabilities(40, false, true);
	// Apostrophe on intl starts a compose sequence; key-repeat stays off
	// while composing (see CORE-032).
	input.RecordKey(1, KEY_APOSTROPHE, true);
	CHECK_FALSE(input.TimeUntilKeyRepeat().has_value());
	// A plain letter arms compositor key-repeat until the seat is removed.
	input.RecordKey(2, KEY_E, true);
	REQUIRE(input.TimeUntilKeyRepeat() == 10ms);

	const auto actions = lifecycle.RemoveGlobal(40);
	REQUIRE(actions.size() == 2);
	CHECK(actions[0].type ==
		Scalpel::WaylandLifecycleActionType::ReleaseKeyboard);
	for (const Scalpel::WaylandLifecycleAction &action : actions) {
		if (action.type ==
			Scalpel::WaylandLifecycleActionType::ReleaseKeyboard) {
			input.ResetKeyboardDevice();
		}
	}
	now += 10ms;
	CHECK_FALSE(input.RunKeyRepeat());
	const std::vector<Scalpel::InputEvent> removed = input.TakeInputs();
	// Device reset drops queued keys and reports a single focus loss.
	REQUIRE(removed.size() == 1);
	CHECK_FALSE(std::get<Scalpel::KeyboardFocusInput>(removed[0]).focused);

	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 41, 9);
	(void)lifecycle.UpdateSeatCapabilities(41, false, true);
	REQUIRE(input.SetKeymap(keymap.text));
	input.RecordKeyboardFocus(true);
	input.RecordKey(3, KEY_E, true);
	const std::vector<Scalpel::InputEvent> replacement = input.TakeInputs();
	REQUIRE(replacement.size() == 2);
	CHECK(std::get<Scalpel::KeyboardInput>(replacement[1]).text == "e");
}

TEST_CASE("Wayland shell integration rescales an outstanding frame") {
	Scalpel::WaylandFrameState frame;
	frame.Invalidate({10, 20, 30, 40});
	const auto first = frame.BeginFrame(800, 600, 0, false, true);
	REQUIRE(first);
	CHECK_FALSE(frame.PrepareFrame(first->submission, true).has_value());
	CHECK(frame.FeedbackOutstanding(first->submission));
	frame.SubmitFrame(first->submission);
	REQUIRE(frame.CallbackOutstanding());

	Scalpel::WaylandScaleState scale(800, 600);
	(void)scale.TakeConfiguration();
	scale.AddOutput(20);
	scale.SetOutputScale(20, 2);
	scale.EnterOutput(20);
	const auto changed = scale.TakeConfiguration();
	REQUIRE(changed);
	CHECK(changed->bufferWidth == 1600);
	CHECK(changed->bufferHeight == 1200);

	frame.CancelFrameCallback();
	frame.ResetDamageHistory();
	frame.Invalidate({0, 0, changed->logicalWidth, changed->logicalHeight});
	CHECK(frame.CanSubmit());
	const auto replacement = frame.BeginFrame(
		changed->bufferWidth, changed->bufferHeight, 2, true, true);
	REQUIRE(replacement);
	CHECK(replacement->repaintDamage ==
		std::vector<Scalpel::FrameRectangle>{{0, 0, 1600, 1200}});
}

TEST_CASE("Wayland shell integration removes an output under an entered cursor") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	Scalpel::WaylandScaleState scale(800, 600);
	(void)scale.TakeConfiguration();
	Scalpel::WaylandCursorState cursor;
	(void)cursor.Request(Scintilla::Internal::Window::Cursor::text);
	(void)cursor.SetThemeAvailable(true);
	REQUIRE(cursor.Enter(77));

	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Output, 20, 4);
	lifecycle.EnterOutput(20);
	scale.AddOutput(20);
	scale.SetOutputScale(20, 2);
	scale.EnterOutput(20);
	const auto scaled = scale.TakeConfiguration();
	REQUIRE(scaled);
	const auto scaledCursor = cursor.SetScale(scaled->cursorScale);
	REQUIRE(scaledCursor);
	CHECK(scaledCursor->serial == 77);
	CHECK(scaledCursor->scale == 2);

	const auto removed = lifecycle.RemoveGlobal(20);
	REQUIRE(removed.size() == 1);
	CHECK(removed[0].type ==
		Scalpel::WaylandLifecycleActionType::ReleaseOutput);
	scale.RemoveOutput(20);
	const auto fallback = scale.TakeConfiguration();
	REQUIRE(fallback);
	const auto fallbackCursor = cursor.SetScale(fallback->cursorScale);
	REQUIRE(fallbackCursor);
	CHECK(fallbackCursor->serial == 77);
	CHECK(fallbackCursor->scale == 1);
	CHECK(cursor.Entered());
}

TEST_CASE("Wayland shell integration services a transfer during flush recovery") {
	Pipe pipe;
	Scalpel::WaylandTransfer transfer =
		Scalpel::WaylandTransfer::Read(pipe.TakeRead());
	REQUIRE(write(pipe.descriptors[1], "clipboard", 9) == 9);
	(void)close(std::exchange(pipe.descriptors[1], -1));

	Scalpel::WaylandEventLoop eventLoop(true);
	eventLoop.AddEditorDeadline(0ms);
	eventLoop.AddDeadline(transfer.TimeUntilDeadline());
	eventLoop.AddSource(transfer.Descriptor(), POLLIN,
		[&transfer](short events) {
			CHECK((events & POLLIN) != 0);
			transfer.ProcessReady();
		});
	CHECK(eventLoop.TimeoutMilliseconds() > 0);

	std::vector<pollfd> descriptors = eventLoop.PollDescriptors();
	REQUIRE(descriptors.size() == 1);
	descriptors[0].revents = POLLIN;
	CHECK(eventLoop.DispatchReady(descriptors));
	const auto result = transfer.TakeResult();
	REQUIRE(result);
	CHECK(result->status == Scalpel::WaylandTransferStatus::Complete);
	CHECK(result->bytes == "clipboard");
}

TEST_CASE("Wayland shell integration cancels IME before direct input") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("base");
	editor.HandleKeyboardInput({Scintilla::Keys::End,
		Scintilla::KeyMod::Norm, {}, 1, true});

	Scalpel::WaylandTextInputState textInput;
	(void)textInput.Attach();
	(void)textInput.SetKeyboardFocus(true);
	(void)textInput.Enter();
	(void)textInput.UpdateClientState(TextInputClientState());
	textInput.RecordPreedit("x", 1, 1);
	textInput.Done(textInput.CommitSerial() - 1);
	const auto preeditBatches = textInput.TakeBatches();
	REQUIRE(preeditBatches.size() == 1);
	REQUIRE(preeditBatches[0].preedit);
	Scalpel::ApplicationTextInputBatch preedit;
	preedit.preedit = Scalpel::ApplicationTextInputPreedit{
		preeditBatches[0].preedit->text,
		preeditBatches[0].preedit->cursorBegin,
		preeditBatches[0].preedit->cursorEnd};
	editor.HandleTextInputBatch(preedit);
	CHECK(editor.Text() == "basex");

	textInput.Leave();
	const auto cancelled = textInput.TakeBatches();
	REQUIRE(cancelled.size() == 1);
	CHECK(cancelled[0].cancel);
	Scalpel::ApplicationTextInputBatch cancellation;
	cancellation.cancel = cancelled[0].cancel;
	editor.HandleTextInputBatch(cancellation);

	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput directInput("C.utf8");
	REQUIRE(directInput.SetKeymap(keymap.text));
	directInput.RecordKey(2, KEY_Y, true);
	const auto events = directInput.TakeInputs();
	REQUIRE(events.size() == 1);
	editor.HandleKeyboardInput(std::get<Scalpel::KeyboardInput>(events[0]));
	CHECK(editor.Text() == "basey");
}

TEST_CASE("Wayland shell integration closes without optional services") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	REQUIRE(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Compositor, 1, 6).size() == 1);
	REQUIRE(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::WmBase, 2, 6).size() == 1);
	CHECK_FALSE(lifecycle.ActiveSeat());
	CHECK(lifecycle.OutputCount() == 0);

	Scalpel::WaylandClipboardState clipboard;
	CHECK(clipboard.ChoosePaste().kind ==
		Scalpel::WaylandClipboardPasteChoice::Kind::Unavailable);
	CHECK_FALSE(clipboard.Publish("copy"));
	Scalpel::WaylandPrimarySelectionState primary;
	CHECK(primary.ChoosePaste().kind ==
		Scalpel::WaylandPrimaryPasteChoice::Kind::Unavailable);
	CHECK_FALSE(primary.Publish(std::optional<std::string>{"primary"}));
	Scalpel::WaylandTextInputState textInput;
	CHECK(textInput.Attach().empty());
	Scalpel::WaylandCursorState cursor;
	CHECK_FALSE(cursor.Request(
		Scintilla::Internal::Window::Cursor::text).has_value());
	Scalpel::WaylandPortalParentState parent;
	CHECK(parent.ParentHandle().empty());

	lifecycle.RequestClose();
	CHECK(lifecycle.CloseRequested());
}
