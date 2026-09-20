#include "catch.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <unistd.h>
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

	std::unique_ptr<Scalpel::SpellcheckDictionary> Load(
		std::string personalPath = {}) const {
		auto result = Scalpel::SpellcheckDictionary::Load(
			{"test", path + "/test.aff", path + "/test.dic"},
			std::move(personalPath));
		REQUIRE(result.dictionary != nullptr);
		return std::move(result.dictionary);
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

}

TEST_CASE("spellcheck session replaces variable-length UTF-8 and advances in scope") {
	TempDictionary fixture;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("wrng café bad tailbad");
	Scalpel::SpellcheckSession session(editor, fixture.Load(), 0, 14);
	REQUIRE(session.State() == Scalpel::SpellcheckSessionState::Decision);
	REQUIRE(session.Current().has_value());
	CHECK(session.Current()->word == "wrng");

	REQUIRE(session.Replace("👍"));
	REQUIRE(session.State() == Scalpel::SpellcheckSessionState::Decision);
	CHECK(session.Current()->word == "bad");
	CHECK(session.ScopeEnd() == 14);
	REQUIRE(session.Replace("right"));
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Complete);
	CHECK(editor.Text() == "👍 café right tailbad");
	CHECK(session.ScopeEnd() == 16);
}

TEST_CASE("spellcheck replace all uses exact whole tokens and one undo action") {
	TempDictionary fixture;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("wrng wrngx wrng Wrng wrng");
	{
		Scalpel::SpellcheckSession session(editor, fixture.Load(),
			0, editor.DocumentLength());
		REQUIRE(session.Current().has_value());
		REQUIRE(session.ReplaceAll("right"));
		CHECK(editor.Text() == "right wrngx right Wrng right");
		REQUIRE(session.Current().has_value());
		CHECK(session.Current()->word == "wrngx");
	}
	editor.RequestUndo();
	CHECK(editor.Text() == "wrng wrngx wrng Wrng wrng");
}

TEST_CASE("spellcheck ignores exact session words and completes no-match ranges") {
	TempDictionary fixture;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad hello bad world");
	Scalpel::SpellcheckSession session(editor, fixture.Load(),
		0, editor.DocumentLength());
	REQUIRE(session.Current().has_value());
	REQUIRE(session.IgnoreAll());
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Complete);
	CHECK(editor.Text() == "bad hello bad world");
}

TEST_CASE("spellcheck completes a range containing no unknown words") {
	TempDictionary fixture;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("hello café world");
	Scalpel::SpellcheckSession session(editor, fixture.Load(),
		0, editor.DocumentLength());
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Complete);
	CHECK_FALSE(session.Current().has_value());
}

TEST_CASE("spellcheck replacements preserve malformed and out-of-scope bytes") {
	TempDictionary fixture;
	Scalpel::ApplicationEditor editor(320, 180);
	const std::string original = std::string("bad") + '\xFF' + "wrng tailbad";
	editor.LoadInitialBuffer(original);
	Scalpel::SpellcheckSession session(editor, fixture.Load(), 0, 8);
	REQUIRE(session.Replace("right"));
	REQUIRE(session.Replace("world"));
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Complete);
	CHECK(editor.Text() == std::string("right") + '\xFF' + "world tailbad");
}

TEST_CASE("spellcheck review blocks ordinary input and tab switching") {
	TempDictionary fixture;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	const Scalpel::DocumentId first = editor.ActiveDocument();
	const Scalpel::DocumentId second = editor.CreateDocument();
	Scalpel::SpellcheckSession session(editor, fixture.Load(),
		0, editor.DocumentLength());

	editor.HandleKeyboardInput(TextInput("x"));
	editor.ActivateDocument(second);
	CHECK(editor.ActiveDocument() == first);
	CHECK(editor.Text() == "bad");
	CHECK_FALSE(editor.ReplaceRange(0, 3, "changed"));
}

TEST_CASE("spellcheck review keeps workspace tab identity synchronized") {
	TempDictionary fixture;
	Scalpel::ApplicationEditor editor(320, 180);
	Scalpel::DocumentWorkspace workspace(editor);
	const Scalpel::DocumentId first = workspace.ActiveTab();
	editor.LoadInitialBuffer("bad");
	workspace.NewTab();
	const Scalpel::DocumentId second = workspace.ActiveTab();
	workspace.ActivateTab(first);
	Scalpel::SpellcheckSession session(editor, fixture.Load(),
		0, editor.DocumentLength());

	workspace.ActivateTab(second);
	CHECK(workspace.ActiveTab() == first);
	CHECK(editor.ActiveDocument() == first);
}

TEST_CASE("spellcheck review rejects delayed paste and IME delivery") {
	TempDictionary fixture;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	editor.SetClipboardPasteAvailable(true);
	editor.RequestClipboardPaste();
	const auto requests = editor.TakeClipboardRequests();
	REQUIRE(requests.size() == 1);

	Scalpel::SpellcheckSession session(editor, fixture.Load(),
		0, editor.DocumentLength());
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

	Scalpel::SpellcheckSession session(editor, fixture.Load(),
		0, editor.DocumentLength());
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
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	Scalpel::ApplicationTextInputBatch input;
	input.preedit = Scalpel::ApplicationTextInputPreedit{"x", 1, 1};
	editor.HandleTextInputBatch(input);
	REQUIRE(editor.HasTentativeTextInput());

	Scalpel::SpellcheckSession session(editor, fixture.Load(), 0, 3);
	CHECK_FALSE(editor.HasTentativeTextInput());
	CHECK(editor.Text() == "bad");
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Decision);
}

TEST_CASE("spellcheck detects unexpected document changes") {
	TempDictionary fixture;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	Scalpel::SpellcheckSession session(editor, fixture.Load(),
		0, editor.DocumentLength());
	REQUIRE(session.State() == Scalpel::SpellcheckSessionState::Decision);

	editor.LoadInitialBuffer("other");
	CHECK_FALSE(session.Ignore());
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Failed);
	CHECK(session.Error().find("changed") != std::string_view::npos);
}

TEST_CASE("spellcheck keeps a read-only replacement decision") {
	TempDictionary fixture;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	Scalpel::SpellcheckSession session(editor, fixture.Load(),
		0, editor.DocumentLength());
	editor.SetReadOnly(true);

	CHECK_FALSE(session.Replace("right"));
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Decision);
	CHECK(session.Current()->word == "bad");
	CHECK(session.Error() == "The document is read-only.");
}

TEST_CASE("spellcheck makes sequential replacements separate undo actions") {
	TempDictionary fixture;
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad wrng");
	{
		Scalpel::SpellcheckSession session(editor, fixture.Load(),
			0, editor.DocumentLength());
		REQUIRE(session.Replace("right"));
		REQUIRE(session.Replace("world"));
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
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("bad");
	Scalpel::SpellcheckSession session(editor,
		fixture.Load("/proc/scalpel-editor/session-words"),
		0, editor.DocumentLength());

	CHECK(session.AddToDictionary() ==
		Scalpel::AddPersonalWordStatus::WriteFailure);
	CHECK(session.State() == Scalpel::SpellcheckSessionState::Decision);
	CHECK(session.Current()->word == "bad");
	CHECK_FALSE(session.Error().empty());
}
