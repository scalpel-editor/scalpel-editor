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
		std::unique_ptr<SpellcheckDictionary> dictionary,
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

	/** Replace this occurrence and advance. Empty UTF-8 text is allowed. */
	[[nodiscard]] bool Replace(std::string_view replacement);
	/** Replace exact, case-sensitive whole-token matches from here onward. */
	[[nodiscard]] bool ReplaceAll(std::string_view replacement);
	/** Skip only this occurrence. */
	[[nodiscard]] bool Ignore();
	/** Ignore this exact word for the remaining session and advance. */
	[[nodiscard]] bool IgnoreAll();
	/** Persist this exact word, retaining the decision when persistence fails. */
	[[nodiscard]] AddPersonalWordStatus AddToDictionary();

private:
	struct Token {
		Scintilla::Position start = 0;
		Scintilla::Position length = 0;
		std::string word;
	};

	[[nodiscard]] std::optional<Token> NextToken(Scintilla::Position from) const;
	[[nodiscard]] bool ValidateDocument();
	void ScanNext();
	void AdvancePastCurrent();
	void Fail(std::string message);
	[[nodiscard]] bool Apply(std::vector<SpellcheckReplacement> replacements,
		Scintilla::Position nextScan, Scintilla::Position endDelta);

	ApplicationEditor &editor;
	std::unique_ptr<SpellcheckDictionary> dictionary;
	DocumentId document = 0;
	uint64_t expectedGeneration = 0;
	Scintilla::Position scopeStart = 0;
	Scintilla::Position scopeEnd = 0;
	Scintilla::Position scanPosition = 0;
	SpellcheckSessionState state = SpellcheckSessionState::Failed;
	std::optional<SpellcheckOccurrence> current;
	std::unordered_set<std::string> ignored;
	std::string error;
	bool reviewHeld = false;
};

}

#endif
