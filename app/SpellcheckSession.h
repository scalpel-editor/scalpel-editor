// Sequential, byte-safe spelling review of one captured document range.

#ifndef SPELLCHECKSESSION_H
#define SPELLCHECKSESSION_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "ApplicationEditor.h"
#include "Spellcheck.h"
#include "SpellcheckWorker.h"

namespace Scalpel {

enum class SpellcheckSessionState {
	Checking,
	Decision,
	Complete,
	Failed,
};

struct SpellcheckOccurrence {
	Scintilla::Position start = 0;
	Scintilla::Position length = 0;
	std::string word;
	std::vector<std::string> suggestions;
};

/**
 * Owns decision progression and validated edits for one spelling review. The
 * captured document remains locked for the session lifetime.
 */
class SpellcheckSession final {
public:
	SpellcheckSession(ApplicationEditor &editor,
		SpellcheckWorker &worker, SpellcheckDictionaryInfo dictionary,
		std::string personalWordsPath,
		Scintilla::Position scopeStart, Scintilla::Position scopeEnd);
	~SpellcheckSession();

	SpellcheckSession(const SpellcheckSession &) = delete;
	SpellcheckSession &operator=(const SpellcheckSession &) = delete;
	SpellcheckSession(SpellcheckSession &&) = delete;
	SpellcheckSession &operator=(SpellcheckSession &&) = delete;

	[[nodiscard]] SpellcheckSessionState State() const noexcept { return state; }
	[[nodiscard]] const std::optional<SpellcheckOccurrence> &Current() const noexcept {
		return current;
	}
	[[nodiscard]] std::string_view Error() const noexcept { return error; }
	[[nodiscard]] DocumentId Document() const noexcept { return document; }
	[[nodiscard]] Scintilla::Position ScopeStart() const noexcept { return scopeStart; }
	[[nodiscard]] Scintilla::Position ScopeEnd() const noexcept { return scopeEnd; }
	[[nodiscard]] Scintilla::Position ScanPosition() const noexcept {
		return scanPosition;
	}
	[[nodiscard]] int ResultDescriptor() const noexcept {
		return worker.ResultDescriptor();
	}
	/** Deliver completed worker requests on the application thread. */
	void ProcessResults();

	/** Replace this occurrence and advance. Empty UTF-8 text is allowed. */
	[[nodiscard]] bool Replace(std::string_view replacement);
	/** Replace exact, case-sensitive whole-token matches from here onward. */
	[[nodiscard]] bool ReplaceAll(std::string_view replacement);
	/** Skip only this occurrence. */
	[[nodiscard]] bool Ignore();
	/** Ignore this exact word for the remaining session and advance. */
	[[nodiscard]] bool IgnoreAll();
	/** Persist this exact word, retaining the decision when persistence fails. */
	[[nodiscard]] bool AddToDictionary();

private:
	[[nodiscard]] std::string CopyScope(Scintilla::Position from) const;
	[[nodiscard]] bool ValidateDocument();
	void ScanNext();
	void HandleResult(SpellcheckWorkerResult result);
	void AdvancePastCurrent();
	void Fail(std::string message);
	[[nodiscard]] bool Apply(std::vector<SpellcheckReplacement> replacements,
		Scintilla::Position nextScan, Scintilla::Position endDelta);

	ApplicationEditor &editor;
	SpellcheckWorker &worker;
	SpellcheckDictionaryInfo dictionary;
	std::string personalWordsPath;
	DocumentId document = 0;
	uint64_t expectedGeneration = 0;
	uint64_t workerGeneration = 0;
	uint64_t nextRequest = 1;
	uint64_t activeRequest = 0;
	Scintilla::Position scopeStart = 0;
	Scintilla::Position scopeEnd = 0;
	Scintilla::Position scanPosition = 0;
	SpellcheckSessionState state = SpellcheckSessionState::Failed;
	std::optional<SpellcheckOccurrence> current;
	std::optional<SpellcheckOccurrence> pendingReplaceAll;
	std::string pendingReplacement;
	std::unordered_set<std::string> ignored;
	std::string wordCharacters;
	std::string error;
	bool reviewHeld = false;
};

}

#endif
