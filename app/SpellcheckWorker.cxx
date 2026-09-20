#include "SpellcheckWorker.h"

#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace Scalpel {
namespace {

[[nodiscard]] bool SameDictionary(const SpellcheckDictionaryInfo &left,
	const SpellcheckDictionaryInfo &right) noexcept {
	return left.locale == right.locale && left.affixPath == right.affixPath &&
		left.dictionaryPath == right.dictionaryPath;
}

}

SpellcheckWorker::SpellcheckWorker() {
	int descriptors[2]{-1, -1};
	if (pipe2(descriptors, O_CLOEXEC | O_NONBLOCK) != 0) {
		throw std::system_error(errno, std::generic_category(),
			"Could not create spellcheck result pipe");
	}
	resultRead = descriptors[0];
	resultWrite = descriptors[1];
	try {
		thread = std::thread(&SpellcheckWorker::Run, this);
	} catch (...) {
		(void)close(resultRead);
		(void)close(resultWrite);
		resultRead = -1;
		resultWrite = -1;
		throw;
	}
}

SpellcheckWorker::~SpellcheckWorker() noexcept {
	{
		const std::lock_guard<std::mutex> lock(mutex);
		stopping = true;
		pending.reset();
		activeGeneration.store(0, std::memory_order_release);
	}
	condition.notify_one();
	if (thread.joinable()) {
		thread.join();
	}
	if (resultRead >= 0) {
		(void)close(resultRead);
	}
	if (resultWrite >= 0) {
		(void)close(resultWrite);
	}
}

uint64_t SpellcheckWorker::BeginSession() {
	const std::lock_guard<std::mutex> lock(mutex);
	uint64_t generation = nextGeneration++;
	if (generation == 0) {
		generation = nextGeneration++;
	}
	activeGeneration.store(generation, std::memory_order_release);
	pending.reset();
	results.clear();
	DrainNotification();
	return generation;
}

void SpellcheckWorker::Cancel(uint64_t generation) noexcept {
	uint64_t expected = generation;
	if (!activeGeneration.compare_exchange_strong(expected, 0,
		std::memory_order_acq_rel)) {
		return;
	}
	const std::lock_guard<std::mutex> lock(mutex);
	if (pending && pending->generation == generation) {
		pending.reset();
	}
	results.erase(std::remove_if(results.begin(), results.end(),
		[generation](const SpellcheckWorkerResult &result) {
			return result.generation == generation;
		}), results.end());
	DrainNotification();
}

bool SpellcheckWorker::Discover(uint64_t generation, uint64_t request,
	std::vector<std::string> directories) {
	Task task;
	task.generation = generation;
	task.request = request;
	task.operation = SpellcheckWorkerOperation::Discover;
	task.directories = std::move(directories);
	return Submit(std::move(task));
}

bool SpellcheckWorker::Scan(uint64_t generation, uint64_t request,
	SpellcheckDictionaryInfo dictionary_, std::string personalWordsPath,
	std::string text, std::unordered_set<std::string> ignored) {
	Task task;
	task.generation = generation;
	task.request = request;
	task.operation = SpellcheckWorkerOperation::Scan;
	task.dictionary = std::move(dictionary_);
	task.personalWordsPath = std::move(personalWordsPath);
	task.text = std::move(text);
	task.ignored = std::move(ignored);
	return Submit(std::move(task));
}

bool SpellcheckWorker::FindMatches(uint64_t generation, uint64_t request,
	std::string text, std::string word, std::string wordCharacters) {
	Task task;
	task.generation = generation;
	task.request = request;
	task.operation = SpellcheckWorkerOperation::FindMatches;
	task.text = std::move(text);
	task.word = std::move(word);
	task.wordCharacters = std::move(wordCharacters);
	return Submit(std::move(task));
}

bool SpellcheckWorker::AddPersonalWord(uint64_t generation, uint64_t request,
	std::string word) {
	Task task;
	task.generation = generation;
	task.request = request;
	task.operation = SpellcheckWorkerOperation::AddPersonalWord;
	task.text = std::move(word);
	return Submit(std::move(task));
}

bool SpellcheckWorker::Submit(Task task) {
	if (!IsCurrent(task.generation)) {
		return false;
	}
	{
		const std::lock_guard<std::mutex> lock(mutex);
		if (stopping || !IsCurrent(task.generation)) {
			return false;
		}
		pending = std::move(task);
	}
	condition.notify_one();
	return true;
}

void SpellcheckWorker::Run() noexcept {
	uint64_t dictionaryGeneration = 0;
	for (;;) {
		Task task;
		{
			std::unique_lock<std::mutex> lock(mutex);
			condition.wait(lock, [this] { return stopping || pending.has_value(); });
			if (stopping) {
				// Hunspell construction, use, and destruction stay on this thread.
				dictionary.reset();
				return;
			}
			task = std::move(*pending);
			pending.reset();
		}
		const uint64_t generation = task.generation;
		const uint64_t request = task.request;
		const SpellcheckWorkerOperation operation = task.operation;
		try {
			if (dictionaryGeneration != generation) {
				// Reload files for each review, retaining the dictionary only
				// between requests within that review.
				dictionary.reset();
				dictionaryGeneration = generation;
			}
			Execute(std::move(task));
		} catch (const std::exception &error) {
			Publish({generation, request, operation,
				SpellcheckWorkerOutcome::Failed, 0, 0, {}, {}, {},
				AddPersonalWordStatus::InvalidWord, error.what(), {}, {}});
		} catch (...) {
			Publish({generation, request, operation,
				SpellcheckWorkerOutcome::Failed, 0, 0, {}, {}, {},
				AddPersonalWordStatus::InvalidWord,
				"The spelling worker failed.", {}, {}});
		}
	}
}

void SpellcheckWorker::Execute(Task task) {
	if (!IsCurrent(task.generation)) {
		return;
	}
	if (task.operation == SpellcheckWorkerOperation::Discover) {
		SpellcheckDiscovery discovery = task.directories.empty() ?
			DiscoverSpellcheckDictionaries() :
			DiscoverSpellcheckDictionaries(task.directories);
		if (!IsCurrent(task.generation)) {
			return;
		}
		SpellcheckWorkerResult result;
		result.generation = task.generation;
		result.request = task.request;
		result.operation = task.operation;
		result.outcome = SpellcheckWorkerOutcome::Discovery;
		result.discovery = std::move(discovery);
		Publish(std::move(result));
		return;
	}
	if (task.operation == SpellcheckWorkerOperation::FindMatches) {
		std::vector<std::size_t> matches;
		std::size_t position = 0;
		while (position < task.text.size()) {
			if (!IsCurrent(task.generation)) {
				return;
			}
			const std::optional<SpellcheckToken> token = FindNextSpellcheckToken(
				task.text, position, task.wordCharacters);
			if (!token) {
				break;
			}
			if (task.text.compare(token->position, token->length, task.word) == 0) {
				matches.push_back(token->position);
			}
			position = token->position + token->length;
		}
		SpellcheckWorkerResult result;
		result.generation = task.generation;
		result.request = task.request;
		result.operation = task.operation;
		result.outcome = SpellcheckWorkerOutcome::Matches;
		result.matches = std::move(matches);
		Publish(std::move(result));
		return;
	}
	if (task.operation == SpellcheckWorkerOperation::AddPersonalWord) {
		if (!dictionary) {
			Publish({task.generation, task.request, task.operation,
				SpellcheckWorkerOutcome::Failed, 0, 0, {}, {}, {},
				AddPersonalWordStatus::InvalidWord,
				"No spelling dictionary is loaded.", {}, {}});
			return;
		}
		const AddPersonalWordStatus status = dictionary->AddPersonalWord(task.text);
		if (!IsCurrent(task.generation)) {
			return;
		}
		Publish({task.generation, task.request, task.operation,
			SpellcheckWorkerOutcome::PersonalWord, 0, 0, {}, {}, {}, status,
			{}, {}, {}});
		return;
	}

	if (!dictionary || !SameDictionary(loadedDictionary, task.dictionary) ||
		loadedPersonalWordsPath != task.personalWordsPath) {
		dictionary.reset();
		SpellcheckLoadResult loaded = SpellcheckDictionary::Load(
			task.dictionary, task.personalWordsPath);
		if (!IsCurrent(task.generation)) {
			return;
		}
		if (!loaded.dictionary) {
			Publish({task.generation, task.request, task.operation,
				SpellcheckWorkerOutcome::Failed, 0, 0, {}, {}, {},
				AddPersonalWordStatus::InvalidWord, std::move(loaded.message), {}, {}});
			return;
		}
		dictionary = std::move(loaded.dictionary);
		loadedDictionary = task.dictionary;
		loadedPersonalWordsPath = task.personalWordsPath;
	}

	const std::string wordCharacters(dictionary->WordCharacters());
	std::size_t position = 0;
	while (position < task.text.size()) {
		if (!IsCurrent(task.generation)) {
			return;
		}
		const std::optional<SpellcheckToken> token = FindNextSpellcheckToken(
			task.text, position, wordCharacters);
		if (!token) {
			break;
		}
		const std::string word = task.text.substr(token->position, token->length);
		position = token->position + token->length;
		if (task.ignored.count(word) != 0 || dictionary->Check(word)) {
			continue;
		}
		std::vector<std::string> suggestions = dictionary->Suggest(word);
		if (!IsCurrent(task.generation)) {
			return;
		}
		Publish({task.generation, task.request, task.operation,
			SpellcheckWorkerOutcome::Occurrence, token->position, token->length,
			word, std::move(suggestions), wordCharacters,
			AddPersonalWordStatus::InvalidWord, {}, {}, {}});
		return;
	}
	Publish({task.generation, task.request, task.operation,
		SpellcheckWorkerOutcome::Complete, 0, 0, {}, {}, wordCharacters,
		AddPersonalWordStatus::InvalidWord, {}, {}, {}});
}

void SpellcheckWorker::Publish(SpellcheckWorkerResult result) {
	if (!IsCurrent(result.generation)) {
		return;
	}
	{
		const std::lock_guard<std::mutex> lock(mutex);
		if (stopping || !IsCurrent(result.generation)) {
			return;
		}
		results.push_back(std::move(result));
	}
	const unsigned char notification = 1;
	ssize_t written = -1;
	do {
		written = write(resultWrite, &notification, sizeof(notification));
	} while (written < 0 && errno == EINTR);
	if (written < 0 && errno != EAGAIN) {
		// The queued result remains available to a caller already awake.
	}
}

std::vector<SpellcheckWorkerResult> SpellcheckWorker::TakeResults() {
	DrainNotification();
	std::vector<SpellcheckWorkerResult> taken;
	const std::lock_guard<std::mutex> lock(mutex);
	taken.reserve(results.size());
	while (!results.empty()) {
		taken.push_back(std::move(results.front()));
		results.pop_front();
	}
	return taken;
}

void SpellcheckWorker::DrainNotification() noexcept {
	unsigned char bytes[64];
	while (read(resultRead, bytes, sizeof(bytes)) > 0) {
	}
}

bool SpellcheckWorker::IsCurrent(uint64_t generation) const noexcept {
	return generation != 0 &&
		activeGeneration.load(std::memory_order_acquire) == generation;
}

}
