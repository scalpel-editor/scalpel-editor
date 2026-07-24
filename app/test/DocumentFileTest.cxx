#include "catch.hpp"

#include <cstdio>
#include <string>
#include <unistd.h>

#include "DocumentFile.h"

namespace {

class TempFile {
public:
	explicit TempFile(std::string_view contents) {
		char pattern[] = "/tmp/scalpel-doc-XXXXXX";
		const int fd = mkstemp(pattern);
		REQUIRE(fd >= 0);
		path = pattern;
		if (!contents.empty()) {
			const ssize_t written = write(fd, contents.data(), contents.size());
			REQUIRE(written == static_cast<ssize_t>(contents.size()));
		}
		REQUIRE(close(fd) == 0);
	}
	~TempFile() {
		if (!path.empty()) {
			(void)std::remove(path.c_str());
		}
	}
	TempFile(const TempFile &) = delete;
	TempFile &operator=(const TempFile &) = delete;

	std::string path;
};

}

TEST_CASE("document file round-trips bytes including invalid UTF-8") {
	const std::string payload = "line\n\xFFx\xC0\x80";
	TempFile file(payload);
	const std::optional<std::string> read = Scalpel::ReadDocumentFile(file.path);
	REQUIRE(read.has_value());
	CHECK(*read == payload);

	const std::string rewritten = "save\xFF";
	REQUIRE(Scalpel::WriteDocumentFile(file.path, rewritten));
	const std::optional<std::string> again = Scalpel::ReadDocumentFile(file.path);
	REQUIRE(again.has_value());
	CHECK(*again == rewritten);
}

TEST_CASE("document file read fails for a missing path") {
	CHECK_FALSE(Scalpel::ReadDocumentFile(
		"/tmp/scalpel-document-file-missing-path").has_value());
}

TEST_CASE("document file path helpers split directories") {
	CHECK(Scalpel::DocumentDirectory("/home/user/note.txt") == "/home/user");
	CHECK(Scalpel::DocumentDirectory("/note.txt") == "/");
	CHECK(Scalpel::DocumentDirectory("note.txt").empty());
	CHECK(Scalpel::DocumentBaseName("/home/user/note.txt") == "note.txt");
	CHECK(Scalpel::DocumentBaseName("note.txt") == "note.txt");
}
