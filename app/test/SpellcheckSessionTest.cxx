#include "catch.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <unistd.h>
#include <poll.h>
#include <vector>

#include "ApplicationEditor.h"
#include "ApplicationInput.h"
#include "ApplicationTextInput.h"
#include "DocumentWorkspace.h"
#include "Spellcheck.h"
#include "SpellcheckSession.h"

namespace {

class TempDictionary {
public:
	TempDictionary() {
		char pattern[] = "/tmp/scalpel-spellcheck-session-XXXXXX";
		char *created = mkdtemp(pattern);
		REQUIRE(created != nullptr);
		path = created;
		Write("test.aff", "SET UTF-8\nTRY abcdefghijklmnopqrstuvwxyz\n");
		Write("test.dic", "5\nhello\nworld\nright\nknown\ncafé\n");
	}

	~TempDictionary() {
		for (const std::string &file : files) {
			(void)std::remove(file.c_str());
		}
		(void)rmdir(path.c_str());
	}

	Scalpel::SpellcheckDictionaryInfo Info() const {
		return {"test", path + "/test.aff", path + "/test.dic"};
	}

	std::string path;

private:
	void Write(std::string_view name, std::string_view bytes) {
		const std::string destination = path + "/" + std::string(name);
		std::ofstream output(destination, std::ios::binary);
		REQUIRE(output.good());
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		REQUIRE(output.good());
		files.push_back(destination);
	}

	std::vector<std::string> files;
};

Scalpel::KeyboardInput TextInput(std::string text) {
	Scalpel::KeyboardInput input;
	input.pressed = true;
	input.text = std::move(text);
	return input;
}

void Deliver(Scalpel::SpellcheckSession &session) {
	while (session.State() == Scalpel::SpellcheckSessionState::Checking) {
		pollfd descriptor{session.ResultDescriptor(), POLLIN, 0};
		int result = 0;
		do {
			result = poll(&descriptor, 1, 2000);
		} while (result < 0 && errno == EINTR);
		REQUIRE(result == 1);
		session.ProcessResults();
	}
}

std::vector<Scalpel::SpellcheckWorkerResult> Deliver(
	Scalpel::SpellcheckWorker &worker) {
	pollfd descriptor{worker.ResultDescriptor(), POLLIN, 0};
	int result = 0;
	do {
		result = poll(&descriptor, 1, 2000);
	} while (result < 0 && errno == EINTR);
	REQUIRE(result == 1);
	return worker.TakeResults();
}

}

TEST_CASE("spellcheck session replaces variable-length UTF-8 and advances in scope") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("wrng café bad tailbad");
	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {}, 0, 14);
	Deliver(session);
	REQUIRE(session.State() == Scalpel::SpellcheckSessionState::Decision);
	REQUIRE(session.Current().has_value());
	CHECK(session.Current()->word == "wrng");

	REQUIRE(session.Replace("👍"));
	Deliver(session);
	REQUIRE(session.State() == Scalpel::SpellcheckSessionState::Decision);
	CHECK(session.Current()->word == "bad");
	CHECK(session.ScopeEnd() == 14);
	REQUIRE(session.Replace("right"));
	Deliver(session);
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Complete);
	CHECK(editor.Text() == "👍 café right tailbad");
	CHECK(session.ScopeEnd() == 16);
}

TEST_CASE("spellcheck replace all uses exact whole tokens and one undo action") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("wrng wrngx wrng Wrng wrng");
	{
		Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
			0, editor.DocumentLength());
		Deliver(session);
		REQUIRE(session.Current().has_value());
		REQUIRE(session.ReplaceAll("right"));
		Deliver(session);
		CHECK(editor.Text() == "right wrngx right Wrng right");
		REQUIRE(session.Current().has_value());
		CHECK(session.Current()->word == "wrngx");
	}
	editor.RequestUndo();
	CHECK(editor.Text() == "wrng wrngx wrng Wrng wrng");
}

TEST_CASE("spellcheck ignores exact session words and completes no-match ranges") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad hello bad world");
	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
		0, editor.DocumentLength());
	Deliver(session);
	REQUIRE(session.Current().has_value());
	REQUIRE(session.IgnoreAll());
	Deliver(session);
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Complete);
	CHECK(editor.Text() == "bad hello bad world");
}

TEST_CASE("spellcheck completes a range containing no unknown words") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("hello café world");
	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
		0, editor.DocumentLength());
	Deliver(session);
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Complete);
	CHECK_FALSE(session.Current().has_value());
}

TEST_CASE("spellcheck replacements preserve malformed and out-of-scope bytes") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	const std::string original = std::string("bad") + '\xFF' + "wrng tailbad";
	editor.LoadInitialBuffer(original);
	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {}, 0, 8);
	Deliver(session);
	REQUIRE(session.Replace("right"));
	Deliver(session);
	REQUIRE(session.Replace("world"));
	Deliver(session);
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Complete);
	CHECK(editor.Text() == std::string("right") + '\xFF' + "world tailbad");
}

TEST_CASE("spellcheck review blocks ordinary input and tab switching") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	const Scalpel::DocumentId first = editor.ActiveDocument();
	const Scalpel::DocumentId second = editor.CreateDocument();
	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
		0, editor.DocumentLength());
	Deliver(session);

	editor.HandleKeyboardInput(TextInput("x"));
	editor.ActivateDocument(second);
	CHECK(editor.ActiveDocument() == first);
	CHECK(editor.Text() == "bad");
	CHECK_FALSE(editor.ReplaceRange(0, 3, "changed"));
}

TEST_CASE("spellcheck review keeps workspace tab identity synchronized") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	Scalpel::DocumentWorkspace workspace(editor);
	const Scalpel::DocumentId first = workspace.ActiveTab();
	editor.LoadInitialBuffer("bad");
	workspace.NewTab();
	const Scalpel::DocumentId second = workspace.ActiveTab();
	workspace.ActivateTab(first);
	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
		0, editor.DocumentLength());
	Deliver(session);

	workspace.ActivateTab(second);
	CHECK(workspace.ActiveTab() == first);
	CHECK(editor.ActiveDocument() == first);
}

TEST_CASE("spellcheck review preserves its selection against other commands") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad hello world");
	editor.RenderFrame();
	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
		0, editor.DocumentLength());
	Deliver(session);
	REQUIRE(session.Current().has_value());
	editor.SetClipboardPasteAvailable(true);
	CHECK_FALSE(editor.ClipboardPasteAvailable());
	CHECK(editor.FindTextForwardFromSelection("hello") ==
		Scalpel::ApplicationFindOutcome::NotFound);
	CHECK(editor.FindTextBackwardFromSelection("world") ==
		Scalpel::ApplicationFindOutcome::NotFound);
	CHECK_FALSE(editor.PrepareSelectionForContextMenu(
		Scintilla::Internal::Point(200, 8)));
	CHECK(editor.GetSelectionStart() == 0);
	CHECK(editor.GetSelectionEnd() == 3);
}

TEST_CASE("spellcheck review cancels an existing pointer gesture") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad hello world");
	editor.RenderFrame();
	SECTION("selection gesture") {
		editor.HandlePointerInput({Scalpel::PointerAction::Press,
			Scintilla::KeyMod::Norm, 20, 8, 0, 0, 10, 0});
	}
	SECTION("dragging selected text") {
		editor.RequestSelectAll();
		editor.HandlePointerInput({Scalpel::PointerAction::Press,
			Scintilla::KeyMod::Norm, 60, 8, 0, 0, 10, 0});
	}
	REQUIRE(editor.WindowState().mouseCaptured);
	{
		Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
			0, editor.DocumentLength());
		Deliver(session);
		CHECK_FALSE(editor.WindowState().mouseCaptured);
		editor.HandlePointerInput({Scalpel::PointerAction::Move,
			Scintilla::KeyMod::Norm, 200, 8, 0, 0, 11, -1});
		editor.HandlePointerInput({Scalpel::PointerAction::Release,
			Scintilla::KeyMod::Norm, 200, 8, 0, 0, 12, 0});
		CHECK(editor.GetSelectionStart() == 0);
		CHECK(editor.GetSelectionEnd() == 3);
	}
	editor.HandlePointerInput({Scalpel::PointerAction::Move,
		Scintilla::KeyMod::Norm, 150, 8, 0, 0, 13, -1});
	CHECK_FALSE(editor.WindowState().mouseCaptured);
	CHECK(editor.GetSelectionStart() == 0);
	CHECK(editor.GetSelectionEnd() == 3);
	CHECK(editor.UnsupportedRequests().empty());
}

TEST_CASE("spellcheck review rejects delayed paste and IME delivery") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	editor.SetClipboardPasteAvailable(true);
	editor.RequestClipboardPaste();
	const auto requests = editor.TakeClipboardRequests();
	REQUIRE(requests.size() == 1);

	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
		0, editor.DocumentLength());
	Deliver(session);
	editor.HandleClipboardResult(requests.front().id,
		Scalpel::ApplicationClipboardOperation::Paste,
		Scalpel::ApplicationClipboardStatus::Complete, "pasted");
	REQUIRE(editor.ClipboardResults().size() == 1);
	CHECK(editor.ClipboardResults().back().status ==
		Scalpel::ApplicationClipboardStatus::Superseded);

	Scalpel::ApplicationTextInputBatch input;
	input.commit = "typed";
	editor.HandleTextInputBatch(input);
	CHECK(editor.Text() == "bad");
}

TEST_CASE("spellcheck review rejects delayed primary-selection delivery") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	Scalpel::PointerInput press;
	press.action = Scalpel::PointerAction::Press;
	press.button = 2;
	press.x = 100;
	press.y = 40;
	editor.HandlePointerInput(press);
	const auto requests = editor.TakePrimarySelectionRequests();
	REQUIRE(requests.size() == 1);
	REQUIRE(requests.front().operation ==
		Scalpel::ApplicationPrimarySelectionOperation::Paste);

	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
		0, editor.DocumentLength());
	Deliver(session);
	editor.HandlePrimarySelectionResult(requests.front().id,
		Scalpel::ApplicationPrimarySelectionOperation::Paste,
		Scalpel::ApplicationPrimarySelectionStatus::Complete, "pasted");
	REQUIRE(editor.PrimarySelectionResults().size() == 1);
	CHECK(editor.PrimarySelectionResults().back().status ==
		Scalpel::ApplicationPrimarySelectionStatus::Superseded);
	CHECK(editor.Text() == "bad");
}

TEST_CASE("spellcheck start cancels tentative IME before validating scope") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	Scalpel::ApplicationTextInputBatch input;
	input.preedit = Scalpel::ApplicationTextInputPreedit{"x", 1, 1};
	editor.HandleTextInputBatch(input);
	REQUIRE(editor.HasTentativeTextInput());

	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {}, 0, 3);
	Deliver(session);
	CHECK_FALSE(editor.HasTentativeTextInput());
	CHECK(editor.Text() == "bad");
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Decision);
}

TEST_CASE("spellcheck detects unexpected document changes") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
		0, editor.DocumentLength());
	Deliver(session);
	REQUIRE(session.State() == Scalpel::SpellcheckSessionState::Decision);

	editor.LoadInitialBuffer("other");
	CHECK_FALSE(session.Ignore());
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Failed);
	CHECK(session.Error().find("changed") != std::string_view::npos);
}

TEST_CASE("spellcheck keeps a read-only replacement decision") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
		0, editor.DocumentLength());
	Deliver(session);
	editor.SetReadOnly(true);

	CHECK_FALSE(session.Replace("right"));
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Decision);
	CHECK(session.Current()->word == "bad");
	CHECK(session.Error() == "The document is read-only.");
}

TEST_CASE("spellcheck replace all recovers from a read-only rejection") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad bad");
	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
		0, editor.DocumentLength());
	Deliver(session);
	REQUIRE(session.ReplaceAll("right"));
	editor.SetReadOnly(true);

	// Deliver only this result: a rejected edit must not await more work.
	pollfd descriptor{session.ResultDescriptor(), POLLIN, 0};
	int result = 0;
	do {
		result = poll(&descriptor, 1, 2000);
	} while (result < 0 && errno == EINTR);
	REQUIRE(result == 1);
	session.ProcessResults();
	REQUIRE(session.State() == Scalpel::SpellcheckSessionState::Decision);
	REQUIRE(session.Current().has_value());
	CHECK(session.Current()->word == "bad");
	CHECK(session.Error() == "The document is read-only.");
	CHECK(editor.Text() == "bad bad");

	editor.SetReadOnly(false);
	REQUIRE(session.ReplaceAll("right"));
	Deliver(session);
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Complete);
	CHECK(editor.Text() == "right right");
}

TEST_CASE("spellcheck makes sequential replacements separate undo actions") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad wrng");
	{
		Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
			0, editor.DocumentLength());
		Deliver(session);
		REQUIRE(session.Replace("right"));
		Deliver(session);
		REQUIRE(session.Replace("world"));
		Deliver(session);
		CHECK(session.State() == Scalpel::SpellcheckSessionState::Complete);
	}
	CHECK(editor.Text() == "right world");
	editor.RequestUndo();
	CHECK(editor.Text() == "right wrng");
	editor.RequestUndo();
	CHECK(editor.Text() == "bad wrng");
	editor.RequestRedo();
	CHECK(editor.Text() == "right wrng");
	editor.RequestRedo();
	CHECK(editor.Text() == "right world");
}

TEST_CASE("spellcheck personal-word failure retains the current decision") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(),
		"/proc/scalpel-editor/session-words", 0, editor.DocumentLength());
	Deliver(session);

	REQUIRE(session.AddToDictionary());
	Deliver(session);
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Decision);
	CHECK(session.Current()->word == "bad");
	CHECK_FALSE(session.Error().empty());
}

TEST_CASE("spellcheck personal-word write advances after worker completion") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad wrng");
	const std::string personalPath = fixture.path + "/personal.txt";
	{
		Scalpel::SpellcheckSession session(editor, worker, fixture.Info(),
			personalPath, 0, editor.DocumentLength());
		Deliver(session);
		REQUIRE(session.Current()->word == "bad");
		REQUIRE(session.AddToDictionary());
		CHECK(session.State() == Scalpel::SpellcheckSessionState::Checking);
		Deliver(session);
		REQUIRE(session.State() == Scalpel::SpellcheckSessionState::Decision);
		CHECK(session.Current()->word == "wrng");
	}
	std::ifstream personal(personalPath, std::ios::binary);
	REQUIRE(personal.good());
	const std::string bytes((std::istreambuf_iterator<char>(personal)),
		std::istreambuf_iterator<char>());
	CHECK(bytes.find("bad\n") != std::string::npos);
	personal.close();
	CHECK(std::remove(personalPath.c_str()) == 0);
}

TEST_CASE("spellcheck worker reloads personal words between reviews") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("wrng bad");
	const std::string personalPath = fixture.path + "/personal.txt";
	{
		Scalpel::SpellcheckSession session(editor, worker, fixture.Info(),
			personalPath, 0, editor.DocumentLength());
		Deliver(session);
		REQUIRE(session.Current().has_value());
		CHECK(session.Current()->word == "wrng");
	}
	{
		std::ofstream output(personalPath, std::ios::binary);
		output << "scalpel-personal-words-v1\nbad\n";
		REQUIRE(output.good());
	}
	{
		Scalpel::SpellcheckSession session(editor, worker, fixture.Info(),
			personalPath, 0, editor.DocumentLength());
		Deliver(session);
		REQUIRE(session.Current().has_value());
		CHECK(session.Current()->word == "wrng");
		REQUIRE(session.AddToDictionary());
		Deliver(session);
		CHECK(session.State() == Scalpel::SpellcheckSessionState::Complete);
	}
	std::ifstream personal(personalPath, std::ios::binary);
	const std::string bytes((std::istreambuf_iterator<char>(personal)),
		std::istreambuf_iterator<char>());
	CHECK(bytes == "scalpel-personal-words-v1\nbad\nwrng\n");
	personal.close();
	CHECK(std::remove(personalPath.c_str()) == 0);
}

TEST_CASE("spellcheck worker reports dictionary load failure asynchronously") {
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	Scalpel::SpellcheckSession session(editor, worker,
		{"missing", "/missing/test.aff", "/missing/test.dic"}, {},
		0, editor.DocumentLength());

	CHECK(session.State() == Scalpel::SpellcheckSessionState::Checking);
	Deliver(session);
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Failed);
	CHECK(session.Error().find("missing") != std::string_view::npos);
}

TEST_CASE("spellcheck worker discovers dictionaries off the application thread") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	const uint64_t generation = worker.BeginSession();
	REQUIRE(worker.Discover(generation, 1, {fixture.path}));

	const std::vector<Scalpel::SpellcheckWorkerResult> results = Deliver(worker);
	REQUIRE(results.size() == 1);
	CHECK(results.front().generation == generation);
	CHECK(results.front().outcome == Scalpel::SpellcheckWorkerOutcome::Discovery);
	REQUIRE(results.front().discovery.dictionaries.size() == 1);
	CHECK(results.front().discovery.dictionaries.front().locale == "test");
}

TEST_CASE("spellcheck result validates document changes made during checking") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	Scalpel::SpellcheckSession session(editor, worker, fixture.Info(), {},
		0, editor.DocumentLength());
	REQUIRE(session.State() == Scalpel::SpellcheckSessionState::Checking);

	editor.LoadInitialBuffer("other");
	Deliver(session);
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Failed);
	CHECK(session.Error().find("changed") != std::string_view::npos);
}

TEST_CASE("spellcheck cancellation releases review and supersedes late work") {
	TempDictionary fixture;
	Scalpel::SpellcheckWorker worker;
	Scalpel::ApplicationEditor editor(320, 180);
	std::string longText;
	for (int index = 0; index < 200000; ++index) {
		longText += "hello ";
	}
	editor.LoadInitialBuffer(longText);
	{
		Scalpel::SpellcheckSession cancelled(editor, worker, fixture.Info(), {},
			0, editor.DocumentLength());
		CHECK(cancelled.State() == Scalpel::SpellcheckSessionState::Checking);
	}
	CHECK_FALSE(editor.SpellcheckReviewActive());

	editor.LoadInitialBuffer("bad");
	Scalpel::SpellcheckSession current(editor, worker, fixture.Info(), {},
		0, editor.DocumentLength());
	Deliver(current);
	REQUIRE(current.State() == Scalpel::SpellcheckSessionState::Decision);
	CHECK(current.Current()->word == "bad");
}

TEST_CASE("spellcheck worker shuts down with an active request") {
	TempDictionary fixture;
	auto worker = std::make_unique<Scalpel::SpellcheckWorker>();
	const uint64_t generation = worker->BeginSession();
	std::string text;
	for (int index = 0; index < 200000; ++index) {
		text += "hello ";
	}
	REQUIRE(worker->Scan(generation, 1, fixture.Info(), {},
		std::move(text), {}));

	worker.reset();
	SUCCEED("worker joined after accepting cancellation");
}
