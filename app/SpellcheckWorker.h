// Single-threaded background execution for spelling dictionary work.

#ifndef SPELLCHECKWORKER_H
#define SPELLCHECKWORKER_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "Spellcheck.h"

namespace Scalpel {

enum class SpellcheckWorkerOperation {
	Discover,
	Scan,
	FindMatches,
	AddPersonalWord,
};

enum class SpellcheckWorkerOutcome {
	Discovery,
	Occurrence,
	Matches,
	Complete,
	PersonalWord,
	Failed,
};

struct SpellcheckWorkerResult {
	uint64_t generation = 0;
	uint64_t request = 0;
	SpellcheckWorkerOperation operation = SpellcheckWorkerOperation::Scan;
	SpellcheckWorkerOutcome outcome = SpellcheckWorkerOutcome::Failed;
	std::size_t position = 0;
	std::size_t length = 0;
	std::string word;
	std::vector<std::string> suggestions;
	std::string wordCharacters;
	AddPersonalWordStatus personalWordStatus = AddPersonalWordStatus::InvalidWord;
	std::string message;
	SpellcheckDiscovery discovery;
	std::vector<std::size_t> matches;
};

/**
 * Owns the sole Hunspell instance and all dictionary file access. Requests and
 * results contain copied bytes only. At most one request waits behind the
 * request currently executing.
 */
class SpellcheckWorker final {
public:
	SpellcheckWorker();
	~SpellcheckWorker() noexcept;

	SpellcheckWorker(const SpellcheckWorker &) = delete;
	SpellcheckWorker &operator=(const SpellcheckWorker &) = delete;
	SpellcheckWorker(SpellcheckWorker &&) = delete;
	SpellcheckWorker &operator=(SpellcheckWorker &&) = delete;

	/** Supersede any earlier session and return its generation tag. */
	[[nodiscard]] uint64_t BeginSession();
	void Cancel(uint64_t generation) noexcept;

	[[nodiscard]] bool Discover(uint64_t generation, uint64_t request,
		std::vector<std::string> directories = {});
	[[nodiscard]] bool Scan(uint64_t generation, uint64_t request,
		SpellcheckDictionaryInfo dictionary, std::string personalWordsPath,
		std::string text, std::unordered_set<std::string> ignored);
	[[nodiscard]] bool FindMatches(uint64_t generation, uint64_t request,
		std::string text, std::string word, std::string wordCharacters);
	[[nodiscard]] bool AddPersonalWord(uint64_t generation, uint64_t request,
		std::string word);

	/** Readable when TakeResults can return completed work. */
	[[nodiscard]] int ResultDescriptor() const noexcept { return resultRead; }
	[[nodiscard]] std::vector<SpellcheckWorkerResult> TakeResults();

private:
	struct Task {
		uint64_t generation = 0;
		uint64_t request = 0;
		SpellcheckWorkerOperation operation = SpellcheckWorkerOperation::Scan;
		SpellcheckDictionaryInfo dictionary;
		std::string personalWordsPath;
		std::vector<std::string> directories;
		std::string text;
		std::string word;
		std::string wordCharacters;
		std::unordered_set<std::string> ignored;
	};

	[[nodiscard]] bool Submit(Task task);
	void Run() noexcept;
	void Execute(Task task);
	void Publish(SpellcheckWorkerResult result);
	void DrainNotification() noexcept;
	[[nodiscard]] bool IsCurrent(uint64_t generation) const noexcept;

	int resultRead = -1;
	int resultWrite = -1;
	std::thread thread;
	std::mutex mutex;
	std::condition_variable condition;
	std::optional<Task> pending;
	std::deque<SpellcheckWorkerResult> results;
	bool stopping = false;
	std::atomic<uint64_t> activeGeneration{0};
	uint64_t nextGeneration = 1;
	std::unique_ptr<SpellcheckDictionary> dictionary;
	SpellcheckDictionaryInfo loadedDictionary;
	std::string loadedPersonalWordsPath;
};

}

#endif
