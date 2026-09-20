#include "SpellcheckSession.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "ApplicationEditor.h"
#include "UniConversion.h"

namespace Scalpel {
namespace {

constexpr Scintilla::Position kScanReadBytes = 64 * 1024;

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
	SpellcheckWorker &worker_, SpellcheckDictionaryInfo dictionary_,
	std::string personalWordsPath_,
	Scintilla::Position scopeStart_, Scintilla::Position scopeEnd_) :
	editor(editor_), worker(worker_), dictionary(std::move(dictionary_)),
	personalWordsPath(std::move(personalWordsPath_)),
	document(editor_.ActiveDocument()), scopeStart(scopeStart_),
	scopeEnd(scopeEnd_), scanPosition(scopeStart_) {
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
	workerGeneration = worker.BeginSession();
	ScanNext();
}

SpellcheckSession::~SpellcheckSession() {
	worker.Cancel(workerGeneration);
	if (reviewHeld) {
		editor.EndSpellcheckReview(document);
	}
}

std::string SpellcheckSession::CopyScope(Scintilla::Position from) const {
	std::string text;
	for (Scintilla::Position position = from; position < scopeEnd;) {
		const Scintilla::Position end = position +
			std::min(kScanReadBytes, scopeEnd - position);
		text += editor.TextRange(position, end);
		position = end;
	}
	return text;
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
	if (scanPosition >= scopeEnd) {
		state = SpellcheckSessionState::Complete;
		return;
	}
	std::string text = CopyScope(scanPosition);
	activeRequest = nextRequest++;
	if (!worker.Scan(workerGeneration, activeRequest, dictionary,
		personalWordsPath, std::move(text), ignored)) {
		Fail("The spelling worker is no longer available.");
	}
}

void SpellcheckSession::ProcessResults() {
	for (SpellcheckWorkerResult &result : worker.TakeResults()) {
		if (result.generation == workerGeneration &&
			result.request == activeRequest) {
			HandleResult(std::move(result));
		}
	}
}

void SpellcheckSession::HandleResult(SpellcheckWorkerResult result) {
	if (!ValidateDocument()) {
		worker.Cancel(workerGeneration);
		return;
	}
	if (result.outcome == SpellcheckWorkerOutcome::Failed) {
		Fail(result.message.empty() ? "The spelling worker failed." :
			std::move(result.message));
		return;
	}
	if (result.operation == SpellcheckWorkerOperation::AddPersonalWord) {
		if (result.outcome != SpellcheckWorkerOutcome::PersonalWord) {
			Fail("The personal dictionary request failed.");
			return;
		}
		if (result.personalWordStatus == AddPersonalWordStatus::Added ||
			result.personalWordStatus == AddPersonalWordStatus::AlreadyPresent) {
			AdvancePastCurrent();
			return;
		}
		state = SpellcheckSessionState::Decision;
		switch (result.personalWordStatus) {
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
		return;
	}
	if (result.operation == SpellcheckWorkerOperation::FindMatches) {
		if (result.outcome != SpellcheckWorkerOutcome::Matches ||
			!pendingReplaceAll) {
			Fail("The replace-all spelling request failed.");
			return;
		}
		const SpellcheckOccurrence occurrence = *pendingReplaceAll;
		std::vector<SpellcheckReplacement> replacements;
		replacements.reserve(result.matches.size());
		for (const std::size_t relative : result.matches) {
			if (relative > static_cast<std::size_t>(scopeEnd - occurrence.start) ||
				occurrence.length > scopeEnd - occurrence.start -
					static_cast<Scintilla::Position>(relative)) {
				Fail("The spelling worker returned an invalid replacement.");
				return;
			}
			replacements.push_back({occurrence.start +
				static_cast<Scintilla::Position>(relative), occurrence.word,
				pendingReplacement});
		}
		if (replacements.empty() || replacements.front().start !=
			occurrence.start) {
			Fail("The current spelling occurrence is no longer in the document.");
			return;
		}
		const Scintilla::Position replacementLength =
			static_cast<Scintilla::Position>(pendingReplacement.size());
		const Scintilla::Position oneDelta = replacementLength - occurrence.length;
		if (replacements.size() > static_cast<std::size_t>(
			std::numeric_limits<Scintilla::Position>::max())) {
			error = "There are too many spelling replacements.";
			state = SpellcheckSessionState::Decision;
			return;
		}
		const Scintilla::Position count =
			static_cast<Scintilla::Position>(replacements.size());
		if (oneDelta > 0 && count >
			(std::numeric_limits<Scintilla::Position>::max() - scopeEnd) / oneDelta) {
			error = "The spelling replacements are too large.";
			state = SpellcheckSessionState::Decision;
			return;
		}
		pendingReplaceAll.reset();
		const Scintilla::Position totalDelta = oneDelta * count;
		(void)Apply(std::move(replacements),
			occurrence.start + replacementLength, totalDelta);
		return;
	}
	wordCharacters = std::move(result.wordCharacters);
	if (result.outcome == SpellcheckWorkerOutcome::Complete) {
		scanPosition = scopeEnd;
		state = SpellcheckSessionState::Complete;
		return;
	}
	if (result.outcome != SpellcheckWorkerOutcome::Occurrence ||
		result.position > static_cast<std::size_t>(scopeEnd - scanPosition) ||
		result.length > static_cast<std::size_t>(scopeEnd - scanPosition) -
			result.position) {
		Fail("The spelling worker returned an invalid occurrence.");
		return;
	}
	const Scintilla::Position start = scanPosition +
		static_cast<Scintilla::Position>(result.position);
	const Scintilla::Position length =
		static_cast<Scintilla::Position>(result.length);
	scanPosition = start;
	current = SpellcheckOccurrence{start, length, std::move(result.word),
		std::move(result.suggestions)};
	state = SpellcheckSessionState::Decision;
	if (!editor.ShowSpellcheckRange(document, start, length)) {
		Fail("The spelling occurrence is no longer in the document.");
	}
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
		state = SpellcheckSessionState::Decision;
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
	if (!ValidateDocument()) {
		return false;
	}
	state = SpellcheckSessionState::Checking;
	error.clear();
	pendingReplaceAll = occurrence;
	pendingReplacement = replacement;
	activeRequest = nextRequest++;
	if (!worker.FindMatches(workerGeneration, activeRequest,
		CopyScope(occurrence.start), occurrence.word, wordCharacters)) {
		pendingReplaceAll.reset();
		state = SpellcheckSessionState::Decision;
		error = "The spelling worker is no longer available.";
		return false;
	}
	return true;
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

bool SpellcheckSession::AddToDictionary() {
	if (state != SpellcheckSessionState::Decision || !current ||
		!ValidateDocument()) {
		return false;
	}
	state = SpellcheckSessionState::Checking;
	error.clear();
	activeRequest = nextRequest++;
	if (!worker.AddPersonalWord(workerGeneration, activeRequest, current->word)) {
		state = SpellcheckSessionState::Decision;
		error = "The spelling worker is no longer available.";
		return false;
	}
	return true;
}

}
