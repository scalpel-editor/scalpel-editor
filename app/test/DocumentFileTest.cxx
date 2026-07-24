#include "catch.hpp"

#include <cstdio>
#include <string>
#include <sys/stat.h>
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

class TempLink {
public:
	TempLink(const std::string &target, const std::string &linkPath) :
		path(linkPath) {
		REQUIRE(symlink(target.c_str(), path.c_str()) == 0);
	}
	~TempLink() {
		if (!path.empty()) {
			(void)std::remove(path.c_str());
		}
	}
	TempLink(const TempLink &) = delete;
	TempLink &operator=(const TempLink &) = delete;

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

TEST_CASE("document file write fails for an empty path") {
	CHECK_FALSE(Scalpel::WriteDocumentFile({}, "text"));
}

TEST_CASE("document file write fails without destroying a missing parent") {
	CHECK_FALSE(Scalpel::WriteDocumentFile(
		"/tmp/scalpel-document-file-missing-dir/nested.txt", "text"));
}

TEST_CASE("document file rewrite preserves mode bits") {
	TempFile file("original");
	REQUIRE(chmod(file.path.c_str(), 0600) == 0);

	REQUIRE(Scalpel::WriteDocumentFile(file.path, "rewritten"));
	struct stat after {};
	REQUIRE(stat(file.path.c_str(), &after) == 0);
	CHECK((after.st_mode & 0777) == 0600);

	const std::optional<std::string> read = Scalpel::ReadDocumentFile(file.path);
	REQUIRE(read.has_value());
	CHECK(*read == "rewritten");
}

TEST_CASE("document file write follows a symlink to the target file") {
	TempFile target("old-target");
	const std::string linkPath = target.path + ".link";
	TempLink link(target.path, linkPath);

	REQUIRE(Scalpel::WriteDocumentFile(linkPath, "through-link"));

	const std::optional<std::string> targetText =
		Scalpel::ReadDocumentFile(target.path);
	REQUIRE(targetText.has_value());
	CHECK(*targetText == "through-link");

	struct stat linkStat {};
	REQUIRE(lstat(linkPath.c_str(), &linkStat) == 0);
	CHECK(S_ISLNK(linkStat.st_mode));
}

TEST_CASE("document file path helpers split directories") {
	CHECK(Scalpel::DocumentDirectory("/home/user/note.txt") == "/home/user");
	CHECK(Scalpel::DocumentDirectory("/note.txt") == "/");
	CHECK(Scalpel::DocumentDirectory("note.txt").empty());
	CHECK(Scalpel::DocumentBaseName("/home/user/note.txt") == "note.txt");
	CHECK(Scalpel::DocumentBaseName("note.txt") == "note.txt");
}
