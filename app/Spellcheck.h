// Hunspell dictionary discovery, UTF-8 tokenization, and personal words.

#ifndef SPELLCHECK_H
#define SPELLCHECK_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Scalpel {

struct SpellcheckDictionaryInfo {
	std::string locale;
	std::string affixPath;
	std::string dictionaryPath;
};

struct SpellcheckDiscoveryIssue {
	std::string path;
	std::string message;
};

struct SpellcheckDiscovery {
	std::vector<SpellcheckDictionaryInfo> dictionaries;
	std::vector<SpellcheckDiscoveryIssue> issues;
};

/** Search the usual system Hunspell directories in priority order. */
[[nodiscard]] SpellcheckDiscovery DiscoverSpellcheckDictionaries();

/**
 * Search explicit directories in priority order. A locale is returned once,
 * from the first directory containing a readable regular .aff/.dic pair.
 * Incomplete and unreadable pairs are reported in issues.
 */
[[nodiscard]] SpellcheckDiscovery DiscoverSpellcheckDictionaries(
	const std::vector<std::string> &directories);

/**
 * Prefer locale without its encoding or modifier, then its language, then the
 * first discovered dictionary. C and POSIX do not prefer a dictionary.
 */
[[nodiscard]] std::optional<std::size_t> PreferredSpellcheckDictionary(
	const std::vector<SpellcheckDictionaryInfo> &dictionaries,
	std::string_view locale);

struct SpellcheckToken {
	std::size_t position = 0;
	std::size_t length = 0;
};

/**
 * Find the next spelling token at or after start, preserving byte offsets.
 * Unicode letters form words and combining marks attach to a preceding
 * letter. Internal apostrophes and dash punctuation stay in a word. Tokens
 * containing digits or connector punctuation are skipped, as are http(s) and
 * www URL runs. Each malformed UTF-8 byte is a boundary. Dictionary WORDCHARS
 * are accepted as word characters.
 */
[[nodiscard]] std::optional<SpellcheckToken> FindNextSpellcheckToken(
	std::string_view text, std::size_t start,
	std::string_view dictionaryWordCharacters = {});

/** Per-locale personal-word file under XDG_DATA_HOME or ~/.local/share. */
[[nodiscard]] std::string SpellcheckPersonalWordsPath(std::string_view locale,
	std::string_view xdgDataHome, std::string_view home);
[[nodiscard]] std::string SpellcheckPersonalWordsPath(std::string_view locale);

enum class SpellcheckLoadStatus {
	Success,
	MissingDictionary,
	UnreadableDictionary,
	UnsupportedEncoding,
	InvalidDictionary,
	PersonalWordsFailure,
};

class SpellcheckDictionary;

struct SpellcheckLoadResult {
	SpellcheckLoadStatus status = SpellcheckLoadStatus::InvalidDictionary;
	std::string message;
	std::unique_ptr<SpellcheckDictionary> dictionary;
};

enum class AddPersonalWordStatus {
	Added,
	AlreadyPresent,
	InvalidWord,
	LimitExceeded,
	WriteFailure,
};

/**
 * One mutable Hunspell instance. Calls must remain on one thread. Suggestions
 * are owned strings. Only UTF-8 dictionaries are accepted.
 */
class SpellcheckDictionary final {
public:
	~SpellcheckDictionary();
	SpellcheckDictionary(const SpellcheckDictionary &) = delete;
	SpellcheckDictionary &operator=(const SpellcheckDictionary &) = delete;
	SpellcheckDictionary(SpellcheckDictionary &&) noexcept;
	SpellcheckDictionary &operator=(SpellcheckDictionary &&) noexcept;

	[[nodiscard]] static SpellcheckLoadResult Load(
		const SpellcheckDictionaryInfo &info,
		std::string personalWordsPath = {});

	[[nodiscard]] bool Check(std::string_view word);
	[[nodiscard]] std::vector<std::string> Suggest(std::string_view word);
	[[nodiscard]] std::string_view WordCharacters() const noexcept;
	[[nodiscard]] const SpellcheckDictionaryInfo &Info() const noexcept;
	[[nodiscard]] AddPersonalWordStatus AddPersonalWord(std::string_view word);

private:
	class Impl;
	explicit SpellcheckDictionary(std::unique_ptr<Impl> implementation);
	std::unique_ptr<Impl> implementation;
};

}

#endif
