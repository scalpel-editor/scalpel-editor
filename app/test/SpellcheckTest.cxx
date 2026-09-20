#include "catch.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "Spellcheck.h"

namespace {

class TempDirectory {
public:
	TempDirectory() {
		char pattern[] = "/tmp/scalpel-spellcheck-XXXXXX";
		char *created = mkdtemp(pattern);
		REQUIRE(created != nullptr);
		path = created;
	}

	~TempDirectory() {
		for (const std::string &file : files) {
			(void)std::remove(file.c_str());
		}
		(void)rmdir((path + "/personal").c_str());
		(void)rmdir(path.c_str());
	}

	void Write(std::string_view name, std::string_view bytes) {
		const std::string destination = path + "/" + std::string(name);
		std::ofstream output(destination, std::ios::binary);
		REQUIRE(output.good());
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		REQUIRE(output.good());
		files.push_back(destination);
	}

	std::string path;
	std::vector<std::string> files;
};

void WriteDictionary(TempDirectory &directory, std::string_view locale,
	std::string_view encoding = "UTF-8") {
	directory.Write(std::string(locale) + ".aff",
		"SET " + std::string(encoding) + "\nTRY abcdefghijklmnopqrstuvwxyz\n");
	directory.Write(std::string(locale) + ".dic",
		"8\nhello\nworld\ncan't\nwell-known\ncafé\nnaïve\nresume\n");
}

Scalpel::SpellcheckDictionaryInfo DictionaryInfo(
	const TempDirectory &directory, std::string locale = "test_US") {
	return {locale, directory.path + "/" + locale + ".aff",
		directory.path + "/" + locale + ".dic"};
}

std::string TokenText(std::string_view text,
	const std::optional<Scalpel::SpellcheckToken> &token) {
	REQUIRE(token.has_value());
	return std::string(text.substr(token->position, token->length));
}

}

TEST_CASE("spellcheck discovers complete dictionary pairs and reports incomplete pairs") {
	TempDirectory first;
	TempDirectory second;
	WriteDictionary(first, "en_US");
	first.Write("orphan.dic", "1\nword\n");
	WriteDictionary(second, "en_US");
	WriteDictionary(second, "fr_FR");

	const Scalpel::SpellcheckDiscovery discovery =
		Scalpel::DiscoverSpellcheckDictionaries({first.path, second.path});
	REQUIRE(discovery.dictionaries.size() == 2);
	CHECK(discovery.dictionaries[0].locale == "en_US");
	CHECK(discovery.dictionaries[0].dictionaryPath == first.path + "/en_US.dic");
	CHECK(discovery.dictionaries[1].locale == "fr_FR");
	REQUIRE(discovery.issues.size() == 1);
	CHECK(discovery.issues[0].message == "dictionary pair is incomplete");
}

TEST_CASE("spellcheck dictionary preference follows locale then language") {
	const std::vector<Scalpel::SpellcheckDictionaryInfo> dictionaries = {
		{"en_GB", {}, {}}, {"en_US", {}, {}}, {"fr_FR", {}, {}}};
	CHECK(Scalpel::PreferredSpellcheckDictionary(dictionaries, "en_US.UTF-8") == 1);
	CHECK(Scalpel::PreferredSpellcheckDictionary(dictionaries, "fr_CA@euro") == 2);
	CHECK(Scalpel::PreferredSpellcheckDictionary(dictionaries, "C") == 0);
	CHECK_FALSE(Scalpel::PreferredSpellcheckDictionary({}, "en_US").has_value());
}

TEST_CASE("spellcheck tokenization keeps Unicode word bytes and internal punctuation") {
	const std::string text = "  café cafe\xCC\x81 can't rock’n’roll well-known end-";
	std::size_t position = 0;
	const auto first = Scalpel::FindNextSpellcheckToken(text, position);
	CHECK(TokenText(text, first) == "café");
	position = first->position + first->length;
	const auto second = Scalpel::FindNextSpellcheckToken(text, position);
	CHECK(TokenText(text, second) == "cafe\xCC\x81");
	position = second->position + second->length;
	const auto third = Scalpel::FindNextSpellcheckToken(text, position);
	CHECK(TokenText(text, third) == "can't");
	position = third->position + third->length;
	const auto fourth = Scalpel::FindNextSpellcheckToken(text, position);
	CHECK(TokenText(text, fourth) == "rock’n’roll");
	position = fourth->position + fourth->length;
	const auto fifth = Scalpel::FindNextSpellcheckToken(text, position);
	CHECK(TokenText(text, fifth) == "well-known");
	position = fifth->position + fifth->length;
	CHECK(TokenText(text,
		Scalpel::FindNextSpellcheckToken(text, position)) == "end");
}

TEST_CASE("spellcheck tokenization skips identifiers digits URLs and malformed bytes") {
	const std::string text =
		"abc123 123abc _hidden snake_case (https://example.test/mispell) "
		"www.bad.test okay "
		"bad\xFFword final";
	const auto first = Scalpel::FindNextSpellcheckToken(text, 0);
	CHECK(TokenText(text, first) == "okay");
	const auto second = Scalpel::FindNextSpellcheckToken(text,
		first->position + first->length);
	CHECK(TokenText(text, second) == "bad");
	const auto third = Scalpel::FindNextSpellcheckToken(text,
		second->position + second->length);
	CHECK(TokenText(text, third) == "word");
	const auto fourth = Scalpel::FindNextSpellcheckToken(text,
		third->position + third->length);
	CHECK(TokenText(text, fourth) == "final");
}

TEST_CASE("spellcheck tokenization accepts dictionary word characters") {
	const std::string text = "foo·bar next";
	CHECK(TokenText(text,
		Scalpel::FindNextSpellcheckToken(text, 0, "·")) == "foo·bar");
}

TEST_CASE("spellcheck loads UTF-8 fixture checks words and owns suggestions") {
	TempDirectory directory;
	WriteDictionary(directory, "test_US");
	auto loaded = Scalpel::SpellcheckDictionary::Load(DictionaryInfo(directory));
	REQUIRE(loaded.status == Scalpel::SpellcheckLoadStatus::Success);
	REQUIRE(loaded.dictionary != nullptr);
	CHECK(loaded.dictionary->Check("hello"));
	CHECK(loaded.dictionary->Check("café"));
	CHECK_FALSE(loaded.dictionary->Check("helo"));
	const std::vector<std::string> suggestions = loaded.dictionary->Suggest("helo");
	CHECK(std::find(suggestions.begin(), suggestions.end(), "hello") !=
		suggestions.end());
}

TEST_CASE("spellcheck dictionary word characters preserve identifier and joiner rules") {
	TempDirectory directory;
	WriteDictionary(directory, "test_US");
	directory.Write("test_US.aff", "SET UTF-8\nWORDCHARS 0123456789_’'-·\n");
	auto loaded = Scalpel::SpellcheckDictionary::Load(DictionaryInfo(directory));
	REQUIRE(loaded.dictionary != nullptr);
	const std::string text =
		"abc123 123abc snake_case _hidden ‘hello’ 'world' -next- can't rock’n’roll foo·bar";
	std::size_t position = 0;
	for (const std::string expected : {"hello", "world", "next", "can't", "rock’n’roll", "foo·bar"}) {
		const auto token = Scalpel::FindNextSpellcheckToken(text, position,
			loaded.dictionary->WordCharacters());
		CHECK(TokenText(text, token) == expected);
		position = token->position + token->length;
	}
	CHECK_FALSE(Scalpel::FindNextSpellcheckToken(text, position,
		loaded.dictionary->WordCharacters()).has_value());
}

TEST_CASE("spellcheck URL runs stop at Unicode whitespace and malformed bytes") {
	for (const std::string separator : {"\t", "\n", "\v", "\f", "\r", " ",
		"\xC2\x85", "\xC2\xA0", "\xE2\x80\x83", "\xE2\x80\xA8", "\xE2\x80\xA9",
		"\xFF", "\xC2"}) {
		const std::string prefix = "https://example.test/café" + separator;
		const std::string text = prefix + "mispell";
		const auto token = Scalpel::FindNextSpellcheckToken(text, 0);
		CHECK(TokenText(text, token) == "mispell");
		CHECK(token->position == prefix.size());
	}
}

TEST_CASE("spellcheck rejects missing and non-UTF-8 dictionaries explicitly") {
	TempDirectory directory;
	auto missing = Scalpel::SpellcheckDictionary::Load(DictionaryInfo(directory));
	CHECK(missing.status == Scalpel::SpellcheckLoadStatus::MissingDictionary);
	CHECK(missing.dictionary == nullptr);

	WriteDictionary(directory, "latin_US", "ISO8859-1");
	auto unsupported = Scalpel::SpellcheckDictionary::Load(
		DictionaryInfo(directory, "latin_US"));
	CHECK(unsupported.status == Scalpel::SpellcheckLoadStatus::UnsupportedEncoding);
	CHECK(unsupported.message.find("ISO8859-1") != std::string::npos);
}

TEST_CASE("personal words persist atomically and load into a new dictionary") {
	TempDirectory directory;
	WriteDictionary(directory, "test_US");
	const std::string personal = directory.path + "/personal/test_US.txt";
	auto loaded = Scalpel::SpellcheckDictionary::Load(
		DictionaryInfo(directory), personal);
	REQUIRE(loaded.dictionary != nullptr);
	CHECK(loaded.dictionary->AddPersonalWord("scalpelish") ==
		Scalpel::AddPersonalWordStatus::Added);
	CHECK(loaded.dictionary->Check("scalpelish"));
	CHECK(loaded.dictionary->AddPersonalWord("scalpelish") ==
		Scalpel::AddPersonalWordStatus::AlreadyPresent);

	auto reloaded = Scalpel::SpellcheckDictionary::Load(
		DictionaryInfo(directory), personal);
	REQUIRE(reloaded.dictionary != nullptr);
	CHECK(reloaded.dictionary->Check("scalpelish"));
	directory.files.push_back(personal);
}

TEST_CASE("personal word persistence failure does not mutate live dictionary") {
	TempDirectory directory;
	WriteDictionary(directory, "test_US");
	auto loaded = Scalpel::SpellcheckDictionary::Load(
		DictionaryInfo(directory), "/proc/scalpel-editor/words");
	REQUIRE(loaded.dictionary != nullptr);
	CHECK_FALSE(loaded.dictionary->Check("unpersistableword"));
	CHECK(loaded.dictionary->AddPersonalWord("unpersistableword") ==
		Scalpel::AddPersonalWordStatus::WriteFailure);
	CHECK_FALSE(loaded.dictionary->Check("unpersistableword"));
}

TEST_CASE("personal words reject file delimiters without changing persisted words") {
	TempDirectory directory;
	WriteDictionary(directory, "test_US");
	const std::string personal = directory.path + "/personal/test_US.txt";
	auto loaded = Scalpel::SpellcheckDictionary::Load(DictionaryInfo(directory), personal);
	REQUIRE(loaded.dictionary != nullptr);
	REQUIRE(loaded.dictionary->AddPersonalWord("scalpelish") ==
		Scalpel::AddPersonalWordStatus::Added);
	directory.files.push_back(personal);
	for (const char delimiter : {'\0', '\r', '\n'}) {
		const std::string word = std::string("injected") + delimiter + "otherword";
		CHECK(loaded.dictionary->AddPersonalWord(word) ==
			Scalpel::AddPersonalWordStatus::InvalidWord);
		CHECK_FALSE(loaded.dictionary->Check("injected"));
		CHECK_FALSE(loaded.dictionary->Check("otherword"));
	}
	auto reloaded = Scalpel::SpellcheckDictionary::Load(DictionaryInfo(directory), personal);
	REQUIRE(reloaded.dictionary != nullptr);
	CHECK(reloaded.dictionary->Check("scalpelish"));
	CHECK_FALSE(reloaded.dictionary->Check("injected"));
	CHECK_FALSE(reloaded.dictionary->Check("otherword"));
}

TEST_CASE("personal word paths are locale-safe and follow XDG data home") {
	CHECK(Scalpel::SpellcheckPersonalWordsPath("en_US", "/data", "/home/me") ==
		"/data/scalpel-editor/spelling/en_US.txt");
	CHECK(Scalpel::SpellcheckPersonalWordsPath("en_US", {}, "/home/me") ==
		"/home/me/.local/share/scalpel-editor/spelling/en_US.txt");
	CHECK(Scalpel::SpellcheckPersonalWordsPath("../bad", "/data", "/home/me").empty());
}
