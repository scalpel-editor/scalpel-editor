#include "SpellcheckSession.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "ApplicationEditor.h"
#include "UniConversion.h"

namespace Scalpel {
namespace {

constexpr Scintilla::Position kScanReadBytes = 64 * 1024;

[[nodiscard]] bool IsAsciiSpace(char value) noexcept {
	return value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
		value == '\v' || value == '\f';
}

[[nodiscard]] std::string EditFailureMessage(SpellcheckEditStatus status) {
	switch (status) {
	case SpellcheckEditStatus::Applied:
		return {};
	case SpellcheckEditStatus::ReviewInactive:
		return "The spelling review no longer owns this document.";
	case SpellcheckEditStatus::DocumentChanged:
		return "The document changed during spelling review.";
	case SpellcheckEditStatus::ReadOnly:
		return "The document is read-only.";
	case SpellcheckEditStatus::InvalidRange:
		return "The spelling occurrence is no longer in the document.";
	case SpellcheckEditStatus::ProtectedRange:
		return "The spelling occurrence is protected.";
	}
	return "The spelling replacement failed.";
}

}

SpellcheckSession::SpellcheckSession(ApplicationEditor &editor_,
	std::unique_ptr<SpellcheckDictionary> dictionary_,
	Scintilla::Position scopeStart_, Scintilla::Position scopeEnd_) :
	editor(editor_), dictionary(std::move(dictionary_)),
	document(editor_.ActiveDocument()), scopeStart(scopeStart_),
	scopeEnd(scopeEnd_), scanPosition(scopeStart_) {
	if (!dictionary) {
		Fail("No spelling dictionary is loaded.");
		return;
	}
	if (!editor.BeginSpellcheckReview(document)) {
		Fail("Another document decision is already active.");
		return;
	}
	reviewHeld = true;
	if (scopeStart < 0 || scopeEnd < scopeStart ||
		scopeEnd > editor.DocumentLength()) {
		Fail("The spelling review range is invalid.");
		return;
	}
	expectedGeneration = editor.DocumentGeneration();
	ScanNext();
}

SpellcheckSession::~SpellcheckSession() {
	if (reviewHeld) {
		editor.EndSpellcheckReview(document);
	}
}

std::optional<SpellcheckSession::Token> SpellcheckSession::NextToken(
	Scintilla::Position from) const {
	if (from < scopeStart || from >= scopeEnd) {
		return std::nullopt;
	}
	std::string bytes;
	Scintilla::Position readPosition = from;
	while (readPosition < scopeEnd) {
		const Scintilla::Position readEnd = readPosition +
			std::min(kScanReadBytes, scopeEnd - readPosition);
		bytes += editor.TextRange(readPosition, readEnd);
		readPosition = readEnd;

		std::size_t safeLength = bytes.size();
		if (readPosition < scopeEnd) {
			const auto boundary = std::find_if(bytes.rbegin(), bytes.rend(),
				[](char value) { return IsAsciiSpace(value); });
			if (boundary == bytes.rend()) {
				continue;
			}
			safeLength = bytes.size() -
				static_cast<std::size_t>(std::distance(bytes.rbegin(), boundary));
		}
		const std::string_view safe(bytes.data(), safeLength);
		const std::optional<SpellcheckToken> token = FindNextSpellcheckToken(
			safe, 0, dictionary->WordCharacters());
		if (token) {
			return Token{
				from + static_cast<Scintilla::Position>(token->position),
				static_cast<Scintilla::Position>(token->length),
				std::string(safe.substr(token->position, token->length)),
			};
		}
		if (readPosition == scopeEnd) {
			return std::nullopt;
		}
		from += static_cast<Scintilla::Position>(safeLength);
		bytes.erase(0, safeLength);
	}
	return std::nullopt;
}

bool SpellcheckSession::ValidateDocument() {
	if (!reviewHeld || editor.ActiveDocument() != document ||
		editor.DocumentGeneration() != expectedGeneration) {
		Fail("The document changed during spelling review.");
		return false;
	}
	return true;
}

void SpellcheckSession::ScanNext() {
	if (!ValidateDocument()) {
		return;
	}
	state = SpellcheckSessionState::Checking;
	current.reset();
	error.clear();
	while (scanPosition < scopeEnd) {
		const std::optional<Token> token = NextToken(scanPosition);
		if (!token) {
			scanPosition = scopeEnd;
			state = SpellcheckSessionState::Complete;
			return;
		}
		scanPosition = token->start;
		if (ignored.count(token->word) != 0 || dictionary->Check(token->word)) {
			scanPosition = token->start + token->length;
			continue;
		}
		current = SpellcheckOccurrence{
			token->start, token->length, token->word,
			dictionary->Suggest(token->word),
		};
		state = SpellcheckSessionState::Decision;
		if (!editor.ShowSpellcheckRange(document, token->start, token->length)) {
			Fail("The spelling occurrence is no longer in the document.");
		}
		return;
	}
	state = SpellcheckSessionState::Complete;
}

void SpellcheckSession::AdvancePastCurrent() {
	if (!current) {
		return;
	}
	scanPosition = current->start + current->length;
	ScanNext();
}

void SpellcheckSession::Fail(std::string message) {
	state = SpellcheckSessionState::Failed;
	current.reset();
	error = std::move(message);
}

bool SpellcheckSession::Apply(std::vector<SpellcheckReplacement> replacements,
	Scintilla::Position nextScan, Scintilla::Position endDelta) {
	if (!ValidateDocument()) {
		return false;
	}
	const SpellcheckEditStatus status = editor.ReplaceSpellcheckRanges(
		document, expectedGeneration, replacements);
	if (status != SpellcheckEditStatus::Applied) {
		error = EditFailureMessage(status);
		if (status == SpellcheckEditStatus::DocumentChanged ||
			status == SpellcheckEditStatus::ReviewInactive ||
			status == SpellcheckEditStatus::InvalidRange) {
			Fail(error);
		}
		return false;
	}
	expectedGeneration = editor.DocumentGeneration();
	scopeEnd += endDelta;
	scanPosition = nextScan;
	ScanNext();
	return true;
}

bool SpellcheckSession::Replace(std::string_view replacement) {
	if (state != SpellcheckSessionState::Decision || !current) {
		return false;
	}
	if (!Scintilla::Internal::UTF8IsValid(replacement)) {
		error = "The replacement is not valid UTF-8.";
		return false;
	}
	if (replacement.size() > static_cast<std::size_t>(
			std::numeric_limits<Scintilla::Position>::max())) {
		error = "The replacement is too large.";
		return false;
	}
	const SpellcheckOccurrence occurrence = *current;
	const Scintilla::Position replacementLength =
		static_cast<Scintilla::Position>(replacement.size());
	return Apply({SpellcheckReplacement{
		occurrence.start, occurrence.word, std::string(replacement)}},
		occurrence.start + replacementLength,
		replacementLength - occurrence.length);
}

bool SpellcheckSession::ReplaceAll(std::string_view replacement) {
	if (state != SpellcheckSessionState::Decision || !current) {
		return false;
	}
	if (!Scintilla::Internal::UTF8IsValid(replacement)) {
		error = "The replacement is not valid UTF-8.";
		return false;
	}
	if (replacement.size() > static_cast<std::size_t>(
			std::numeric_limits<Scintilla::Position>::max())) {
		error = "The replacement is too large.";
		return false;
	}
	const SpellcheckOccurrence occurrence = *current;
	std::vector<SpellcheckReplacement> replacements;
	Scintilla::Position position = occurrence.start;
	while (position < scopeEnd) {
		const std::optional<Token> token = NextToken(position);
		if (!token) {
			break;
		}
		if (token->word == occurrence.word) {
			replacements.push_back(
				{token->start, token->word, std::string(replacement)});
		}
		position = token->start + token->length;
	}
	const Scintilla::Position replacementLength =
		static_cast<Scintilla::Position>(replacement.size());
	const Scintilla::Position oneDelta = replacementLength - occurrence.length;
	if (replacements.size() > static_cast<std::size_t>(
			std::numeric_limits<Scintilla::Position>::max())) {
		error = "There are too many spelling replacements.";
		return false;
	}
	const Scintilla::Position count =
		static_cast<Scintilla::Position>(replacements.size());
	if (oneDelta > 0 && count >
			(std::numeric_limits<Scintilla::Position>::max() - scopeEnd) / oneDelta) {
		error = "The spelling replacements are too large.";
		return false;
	}
	const Scintilla::Position totalDelta = oneDelta * count;
	return Apply(std::move(replacements),
		occurrence.start + replacementLength, totalDelta);
}

bool SpellcheckSession::Ignore() {
	if (state != SpellcheckSessionState::Decision || !current ||
		!ValidateDocument()) {
		return false;
	}
	AdvancePastCurrent();
	return true;
}

bool SpellcheckSession::IgnoreAll() {
	if (state != SpellcheckSessionState::Decision || !current ||
		!ValidateDocument()) {
		return false;
	}
	ignored.insert(current->word);
	AdvancePastCurrent();
	return true;
}

AddPersonalWordStatus SpellcheckSession::AddToDictionary() {
	if (state != SpellcheckSessionState::Decision || !current ||
		!ValidateDocument()) {
		return AddPersonalWordStatus::InvalidWord;
	}
	const AddPersonalWordStatus status = dictionary->AddPersonalWord(current->word);
	if (status == AddPersonalWordStatus::Added ||
		status == AddPersonalWordStatus::AlreadyPresent) {
		AdvancePastCurrent();
		return status;
	}
	switch (status) {
	case AddPersonalWordStatus::InvalidWord:
		error = "This word cannot be added to the dictionary.";
		break;
	case AddPersonalWordStatus::LimitExceeded:
		error = "The personal dictionary is full.";
		break;
	case AddPersonalWordStatus::WriteFailure:
		error = "The personal dictionary could not be saved.";
		break;
	case AddPersonalWordStatus::Added:
	case AddPersonalWordStatus::AlreadyPresent:
		break;
	}
	return status;
}

}
