#include "Spellcheck.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <hunspell.hxx>
#include <sys/stat.h>
#include <unistd.h>

#include "CharacterCategoryMap.h"
#include "UniConversion.h"

namespace Scalpel {
namespace {

using Scintilla::Internal::CategoriseCharacter;
using Scintilla::Internal::CharacterCategory;
using Scintilla::Internal::UTF8Classify;
using Scintilla::Internal::UTF8MaskInvalid;
using Scintilla::Internal::UTF8MaskWidth;
using Scintilla::Internal::UnicodeFromUTF8;

constexpr std::size_t kMaximumAffixBytes = 1024U * 1024U;
constexpr std::size_t kMaximumPersonalBytes = 1024U * 1024U;
constexpr std::size_t kMaximumPersonalWords = 10000U;
constexpr std::string_view kPersonalHeader = "scalpel-personal-words-v1\n";

[[nodiscard]] std::vector<std::string> SystemDictionaryDirectories() {
	const char *configured = std::getenv("XDG_DATA_DIRS");
	const std::string_view roots = configured && *configured ? configured :
		"/usr/local/share:/usr/share";
	std::vector<std::string> directories;
	for (std::size_t start = 0; start <= roots.size();) {
		const std::size_t separator = roots.find(':', start);
		const std::size_t end = separator == std::string_view::npos ?
			roots.size() : separator;
		// XDG data roots must be absolute, never relative to the launch directory.
		if (end > start && roots[start] == '/') {
			const std::string root(roots.substr(start, end - start));
			directories.push_back(root + "/hunspell");
			directories.push_back(root + "/myspell");
			directories.push_back(root + "/myspell/dicts");
		}
		if (separator == std::string_view::npos) {
			break;
		}
		start = separator + 1;
	}
	return directories;
}

struct DecodedCharacter {
	int codePoint = 0;
	std::size_t width = 1;
	bool valid = false;
};

[[nodiscard]] DecodedCharacter Decode(std::string_view text,
	std::size_t position) noexcept {
	if (position >= text.size()) {
		return {};
	}
	const int classified = UTF8Classify(text.substr(position));
	if (classified & UTF8MaskInvalid) {
		return {static_cast<unsigned char>(text[position]), 1, false};
	}
	return {UnicodeFromUTF8(text.substr(position)),
		static_cast<std::size_t>(classified & UTF8MaskWidth), true};
}

[[nodiscard]] bool IsLetter(CharacterCategory category) noexcept {
	return category == Scintilla::Internal::ccLu ||
		category == Scintilla::Internal::ccLl ||
		category == Scintilla::Internal::ccLt ||
		category == Scintilla::Internal::ccLm ||
		category == Scintilla::Internal::ccLo;
}

[[nodiscard]] bool IsMark(CharacterCategory category) noexcept {
	return category == Scintilla::Internal::ccMn ||
		category == Scintilla::Internal::ccMc ||
		category == Scintilla::Internal::ccMe;
}

[[nodiscard]] bool IsDigit(CharacterCategory category) noexcept {
	return category == Scintilla::Internal::ccNd ||
		category == Scintilla::Internal::ccNl ||
		category == Scintilla::Internal::ccNo;
}

[[nodiscard]] bool IsJoiner(int codePoint, CharacterCategory category) noexcept {
	return codePoint == '\'' || codePoint == 0x2019 ||
		category == Scintilla::Internal::ccPd;
}

[[nodiscard]] std::set<int> DecodeCharacterSet(std::string_view text) {
	std::set<int> characters;
	for (std::size_t position = 0; position < text.size();) {
		const DecodedCharacter decoded = Decode(text, position);
		const CharacterCategory category = CategoriseCharacter(decoded.codePoint);
		// WORDCHARS must not override identifier skipping or internal joiners.
		if (decoded.valid && !IsDigit(category) &&
			category != Scintilla::Internal::ccPc &&
			!IsJoiner(decoded.codePoint, category)) {
			characters.insert(decoded.codePoint);
		}
		position += decoded.width;
	}
	return characters;
}

[[nodiscard]] bool StartsUrl(std::string_view text, std::size_t position) {
	const std::string_view tail = text.substr(position);
	return tail.compare(0, 7, "http://") == 0 ||
		tail.compare(0, 8, "https://") == 0 ||
		tail.compare(0, 4, "www.") == 0;
}

[[nodiscard]] bool IsSpace(int codePoint) noexcept {
	const CharacterCategory category = CategoriseCharacter(codePoint);
	return category == Scintilla::Internal::ccZs ||
		category == Scintilla::Internal::ccZl ||
		category == Scintilla::Internal::ccZp ||
		(codePoint >= '\t' && codePoint <= '\r') || codePoint == 0x85;
}

[[nodiscard]] bool IsUrlBoundary(std::string_view text,
	std::size_t position) noexcept {
	if (position == 0) {
		return true;
	}
	const unsigned char previous =
		static_cast<unsigned char>(text[position - 1]);
	return previous < 0x80 &&
		!((previous >= 'a' && previous <= 'z') ||
			(previous >= 'A' && previous <= 'Z') ||
			(previous >= '0' && previous <= '9') || previous == '_');
}

[[nodiscard]] bool IsReadableRegularFile(const std::string &path,
	bool &missing) {
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::status(path, error);
	missing = error == std::errc::no_such_file_or_directory ||
		status.type() == std::filesystem::file_type::not_found;
	if (error || !std::filesystem::is_regular_file(status)) {
		return false;
	}
	return access(path.c_str(), R_OK) == 0;
}

[[nodiscard]] std::optional<std::string> ReadBoundedFile(
	const std::string &path, std::size_t maximumBytes) {
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		return std::nullopt;
	}
	std::string bytes;
	char buffer[4096];
	while (input) {
		input.read(buffer, sizeof(buffer));
		const std::streamsize count = input.gcount();
		if (count > 0) {
			if (bytes.size() + static_cast<std::size_t>(count) > maximumBytes) {
				return std::nullopt;
			}
			bytes.append(buffer, static_cast<std::size_t>(count));
		}
	}
	if (!input.eof()) {
		return std::nullopt;
	}
	return bytes;
}

[[nodiscard]] std::string DictionaryEncoding(std::string_view affix) {
	std::size_t start = 0;
	while (start < affix.size()) {
		const std::size_t end = affix.find('\n', start);
		std::string_view line = affix.substr(start,
			end == std::string_view::npos ? affix.size() - start : end - start);
		if (!line.empty() && line.back() == '\r') {
			line.remove_suffix(1);
		}
		if (line.compare(0, 4, "SET ") == 0) {
			line.remove_prefix(4);
			while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
				line.remove_prefix(1);
			}
			const std::size_t space = line.find_first_of(" \t");
			return std::string(line.substr(0, space));
		}
		if (end == std::string_view::npos) {
			break;
		}
		start = end + 1;
	}
	return {};
}

[[nodiscard]] bool ValidPersonalWord(std::string_view word) noexcept {
	return !word.empty() && word.size() < 300 &&
		word.find_first_of(std::string_view("\0\r\n", 3)) == std::string_view::npos &&
		Scintilla::Internal::UTF8IsValid(word);
}

[[nodiscard]] bool LoadPersonalWords(const std::string &path,
	std::vector<std::string> &words) {
	if (path.empty()) {
		return true;
	}
	std::error_code error;
	if (!std::filesystem::exists(path, error)) {
		return !error;
	}
	const std::optional<std::string> bytes =
		ReadBoundedFile(path, kMaximumPersonalBytes);
	if (!bytes || bytes->compare(0, kPersonalHeader.size(), kPersonalHeader) != 0) {
		return false;
	}
	std::size_t start = kPersonalHeader.size();
	while (start < bytes->size()) {
		const std::size_t end = bytes->find('\n', start);
		if (end == std::string::npos || end == start || words.size() >= kMaximumPersonalWords) {
			return false;
		}
		const std::string_view word(bytes->data() + start, end - start);
		if (!ValidPersonalWord(word) ||
			std::find(words.begin(), words.end(), word) != words.end()) {
			return false;
		}
		words.emplace_back(word);
		start = end + 1;
	}
	return true;
}

[[nodiscard]] bool WriteAll(int descriptor, std::string_view bytes) noexcept {
	while (!bytes.empty()) {
		const ssize_t count = write(descriptor, bytes.data(), bytes.size());
		if (count < 0) {
			if (errno == EINTR) {
				continue;
			}
			return false;
		}
		bytes.remove_prefix(static_cast<std::size_t>(count));
	}
	return true;
}

[[nodiscard]] bool SavePersonalWords(const std::string &path,
	const std::vector<std::string> &words) {
	if (path.empty()) {
		return false;
	}
	std::string bytes(kPersonalHeader);
	for (const std::string &word : words) {
		if (bytes.size() + word.size() + 1 > kMaximumPersonalBytes) {
			return false;
		}
		bytes.append(word);
		bytes.push_back('\n');
	}
	const std::filesystem::path destination(path);
	const std::filesystem::path parent = destination.parent_path();
	std::error_code error;
	if (!parent.empty()) {
		std::filesystem::create_directories(parent, error);
		if (error) {
			return false;
		}
	}
	const std::string directory = parent.empty() ? "." : parent.string();
	std::string pattern = directory + "/.personal-words-XXXXXX";
	std::vector<char> mutablePattern(pattern.begin(), pattern.end());
	mutablePattern.push_back('\0');
	const int descriptor = mkstemp(mutablePattern.data());
	if (descriptor < 0) {
		return false;
	}
	const std::string temporary(mutablePattern.data());
	const bool written = WriteAll(descriptor, bytes) && fsync(descriptor) == 0;
	const bool closed = close(descriptor) == 0;
	if (!written || !closed || rename(temporary.c_str(), path.c_str()) != 0) {
		(void)unlink(temporary.c_str());
		return false;
	}
	return true;
}

[[nodiscard]] std::string EnvironmentValue(const char *name) {
	const char *value = std::getenv(name);
	return value ? std::string(value) : std::string{};
}

[[nodiscard]] bool SafeLocale(std::string_view locale) noexcept {
	if (locale.empty()) {
		return false;
	}
	return std::all_of(locale.begin(), locale.end(), [](unsigned char value) {
		return (value >= 'a' && value <= 'z') ||
			(value >= 'A' && value <= 'Z') ||
			(value >= '0' && value <= '9') || value == '_' || value == '-';
	});
}

[[nodiscard]] std::string NormalizeLocale(std::string_view locale) {
	std::string normalized(locale);
	const std::size_t encoding = normalized.find('.');
	if (encoding != std::string::npos) {
		normalized.resize(encoding);
	}
	const std::size_t modifier = normalized.find('@');
	if (modifier != std::string::npos) {
		normalized.resize(modifier);
	}
	std::replace(normalized.begin(), normalized.end(), '-', '_');
	return normalized;
}

}

SpellcheckDiscovery DiscoverSpellcheckDictionaries() {
	return DiscoverSpellcheckDictionaries(SystemDictionaryDirectories());
}

SpellcheckDiscovery DiscoverSpellcheckDictionaries(
	const std::vector<std::string> &directories) {
	SpellcheckDiscovery result;
	std::set<std::string> foundLocales;
	for (const std::string &directory : directories) {
		std::error_code error;
		std::filesystem::directory_iterator entries(directory, error);
		if (error) {
			continue;
		}
		std::vector<std::filesystem::path> dictionaries;
		for (const auto &entry : entries) {
			if (entry.path().extension() == ".dic") {
				dictionaries.push_back(entry.path());
			}
		}
		std::sort(dictionaries.begin(), dictionaries.end());
		for (const std::filesystem::path &dictionary : dictionaries) {
			const std::string locale = dictionary.stem().string();
			if (!SafeLocale(locale) || foundLocales.count(locale) != 0) {
				continue;
			}
			std::filesystem::path affix = dictionary;
			affix.replace_extension(".aff");
			bool dictionaryMissing = false;
			bool affixMissing = false;
			const bool dictionaryReadable = IsReadableRegularFile(
				dictionary.string(), dictionaryMissing);
			const bool affixReadable = IsReadableRegularFile(
				affix.string(), affixMissing);
			if (!dictionaryReadable || !affixReadable) {
				result.issues.push_back({dictionary.string(),
					(dictionaryMissing || affixMissing) ?
					"dictionary pair is incomplete" :
					"dictionary pair is unreadable"});
				continue;
			}
			foundLocales.insert(locale);
			result.dictionaries.push_back(
				{locale, affix.string(), dictionary.string()});
		}
	}
	return result;
}

std::optional<std::size_t> PreferredSpellcheckDictionary(
	const std::vector<SpellcheckDictionaryInfo> &dictionaries,
	std::string_view locale) {
	if (dictionaries.empty()) {
		return std::nullopt;
	}
	const std::string normalized = NormalizeLocale(locale);
	if (normalized != "C" && normalized != "POSIX" && !normalized.empty()) {
		for (std::size_t index = 0; index < dictionaries.size(); ++index) {
			if (dictionaries[index].locale == normalized) {
				return index;
			}
		}
		const std::size_t separator = normalized.find('_');
		const std::string language = normalized.substr(0, separator);
		for (std::size_t index = 0; index < dictionaries.size(); ++index) {
			if (dictionaries[index].locale == language ||
				dictionaries[index].locale.compare(0, language.size() + 1,
					language + "_") == 0) {
				return index;
			}
		}
	}
	return 0;
}

std::optional<SpellcheckToken> FindNextSpellcheckToken(std::string_view text,
	std::size_t start, std::string_view dictionaryWordCharacters) {
	const std::set<int> extra = DecodeCharacterSet(dictionaryWordCharacters);
	std::size_t position = std::min(start, text.size());
	while (position < text.size()) {
		if (StartsUrl(text, position) && IsUrlBoundary(text, position)) {
			while (position < text.size()) {
				const DecodedCharacter current = Decode(text, position);
				if (!current.valid || IsSpace(current.codePoint)) {
					break;
				}
				position += current.width;
			}
			continue;
		}
		const DecodedCharacter first = Decode(text, position);
		const CharacterCategory firstCategory = CategoriseCharacter(first.codePoint);
		const bool firstLetter = first.valid &&
			(IsLetter(firstCategory) || extra.count(first.codePoint) != 0);
		const bool firstRejected = first.valid &&
			(IsDigit(firstCategory) ||
				firstCategory == Scintilla::Internal::ccPc);
		if (!firstLetter && !firstRejected) {
			position += first.width;
			continue;
		}

		const std::size_t tokenStart = position;
		bool rejected = firstRejected;
		position += first.width;
		while (position < text.size()) {
			const DecodedCharacter current = Decode(text, position);
			if (!current.valid) {
				break;
			}
			const CharacterCategory category = CategoriseCharacter(current.codePoint);
			if (IsLetter(category) || IsMark(category) ||
				extra.count(current.codePoint) != 0) {
				position += current.width;
				continue;
			}
			if (IsDigit(category) || category == Scintilla::Internal::ccPc) {
				rejected = true;
				position += current.width;
				continue;
			}
			if (IsJoiner(current.codePoint, category)) {
				const DecodedCharacter following = Decode(text,
					position + current.width);
				if (following.valid) {
					const CharacterCategory followingCategory =
						CategoriseCharacter(following.codePoint);
					if (IsLetter(followingCategory) ||
						extra.count(following.codePoint) != 0) {
						position += current.width;
						continue;
					}
				}
			}
			break;
		}
		if (!rejected) {
			return SpellcheckToken{tokenStart, position - tokenStart};
		}
	}
	return std::nullopt;
}

std::string SpellcheckPersonalWordsPath(std::string_view locale,
	std::string_view xdgDataHome, std::string_view home) {
	if (!SafeLocale(locale)) {
		return {};
	}
	std::string base;
	if (!xdgDataHome.empty()) {
		base = std::string(xdgDataHome);
	} else if (!home.empty()) {
		base = std::string(home) + "/.local/share";
	} else {
		return {};
	}
	return base + "/scalpel-editor/spelling/" + std::string(locale) + ".txt";
}

std::string SpellcheckPersonalWordsPath(std::string_view locale) {
	return SpellcheckPersonalWordsPath(locale,
		EnvironmentValue("XDG_DATA_HOME"), EnvironmentValue("HOME"));
}

class SpellcheckDictionary::Impl final {
public:
	Impl(SpellcheckDictionaryInfo info_, std::string personalWordsPath_,
		std::vector<std::string> personalWords_)
		: info(std::move(info_)), personalWordsPath(std::move(personalWordsPath_)),
		personalWords(std::move(personalWords_)),
		hunspell(info.affixPath.c_str(), info.dictionaryPath.c_str()),
		wordCharacters(hunspell.get_wordchars_cpp()) {
		for (const std::string &word : personalWords) {
			(void)hunspell.add(word);
		}
	}

	SpellcheckDictionaryInfo info;
	std::string personalWordsPath;
	std::vector<std::string> personalWords;
	Hunspell hunspell;
	std::string wordCharacters;
};

SpellcheckDictionary::SpellcheckDictionary(std::unique_ptr<Impl> implementation_)
	: implementation(std::move(implementation_)) {
}

SpellcheckDictionary::~SpellcheckDictionary() = default;
SpellcheckDictionary::SpellcheckDictionary(SpellcheckDictionary &&) noexcept = default;
SpellcheckDictionary &SpellcheckDictionary::operator=(
	SpellcheckDictionary &&) noexcept = default;

SpellcheckLoadResult SpellcheckDictionary::Load(
	const SpellcheckDictionaryInfo &info, std::string personalWordsPath) {
	bool affixMissing = false;
	bool dictionaryMissing = false;
	const bool affixReadable = IsReadableRegularFile(info.affixPath, affixMissing);
	const bool dictionaryReadable =
		IsReadableRegularFile(info.dictionaryPath, dictionaryMissing);
	if (affixMissing || dictionaryMissing) {
		return {SpellcheckLoadStatus::MissingDictionary,
			"Dictionary files are missing.", nullptr};
	}
	if (!affixReadable || !dictionaryReadable) {
		return {SpellcheckLoadStatus::UnreadableDictionary,
			"Dictionary files are not readable regular files.", nullptr};
	}
	const std::optional<std::string> affix =
		ReadBoundedFile(info.affixPath, kMaximumAffixBytes);
	if (!affix) {
		return {SpellcheckLoadStatus::InvalidDictionary,
			"The affix file is missing, unreadable, or too large.", nullptr};
	}
	const std::string encoding = DictionaryEncoding(*affix);
	if (encoding != "UTF-8") {
		return {SpellcheckLoadStatus::UnsupportedEncoding,
			encoding.empty() ? "The dictionary does not declare an encoding." :
				"Unsupported dictionary encoding: " + encoding,
			nullptr};
	}
	std::vector<std::string> personalWords;
	if (!LoadPersonalWords(personalWordsPath, personalWords)) {
		return {SpellcheckLoadStatus::PersonalWordsFailure,
			"The personal-word file is unreadable, oversized, or malformed.",
			nullptr};
	}
	try {
		return {SpellcheckLoadStatus::Success, {},
			std::unique_ptr<SpellcheckDictionary>(new SpellcheckDictionary(
				std::make_unique<Impl>(info, std::move(personalWordsPath),
					std::move(personalWords))))};
	} catch (const std::exception &error) {
		return {SpellcheckLoadStatus::InvalidDictionary, error.what(), nullptr};
	}
}

bool SpellcheckDictionary::Check(std::string_view word) {
	return implementation->hunspell.spell(std::string(word));
}

std::vector<std::string> SpellcheckDictionary::Suggest(std::string_view word) {
	return implementation->hunspell.suggest(std::string(word));
}

std::string_view SpellcheckDictionary::WordCharacters() const noexcept {
	return implementation->wordCharacters;
}

const SpellcheckDictionaryInfo &SpellcheckDictionary::Info() const noexcept {
	return implementation->info;
}

AddPersonalWordStatus SpellcheckDictionary::AddPersonalWord(
	std::string_view word) {
	if (!ValidPersonalWord(word)) {
		return AddPersonalWordStatus::InvalidWord;
	}
	if (std::find(implementation->personalWords.begin(),
		implementation->personalWords.end(), word) !=
		implementation->personalWords.end()) {
		return AddPersonalWordStatus::AlreadyPresent;
	}
	if (implementation->personalWords.size() >= kMaximumPersonalWords) {
		return AddPersonalWordStatus::LimitExceeded;
	}
	std::vector<std::string> updated = implementation->personalWords;
	updated.emplace_back(word);
	if (!SavePersonalWords(implementation->personalWordsPath, updated)) {
		return AddPersonalWordStatus::WriteFailure;
	}
	implementation->personalWords = std::move(updated);
	(void)implementation->hunspell.add(std::string(word));
	return AddPersonalWordStatus::Added;
}

}
