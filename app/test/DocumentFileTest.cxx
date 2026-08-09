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

/** Sparse regular file of the given reported size; does not allocate payload. */
class SparseTempFile {
public:
	explicit SparseTempFile(off_t size) {
		char pattern[] = "/tmp/scalpel-doc-sparse-XXXXXX";
		const int fd = mkstemp(pattern);
		REQUIRE(fd >= 0);
		path = pattern;
		REQUIRE(ftruncate(fd, size) == 0);
		REQUIRE(close(fd) == 0);
	}
	~SparseTempFile() {
		if (!path.empty()) {
			(void)std::remove(path.c_str());
		}
	}
	SparseTempFile(const SparseTempFile &) = delete;
	SparseTempFile &operator=(const SparseTempFile &) = delete;

	std::string path;
};

}

TEST_CASE("document file round-trips bytes including invalid UTF-8") {
	const std::string payload = "line\n\xFFx\xC0\x80";
	TempFile file(payload);
	const Scalpel::DocumentFileReadResult read =
		Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(read.bytes == payload);

	const std::string rewritten = "save\xFF";
	REQUIRE(Scalpel::WriteDocumentFile(file.path, rewritten));
	const Scalpel::DocumentFileReadResult again =
		Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(again.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(again.bytes == rewritten);
}

TEST_CASE("document file read fails for a missing path") {
	CHECK(Scalpel::ReadDocumentFile("/tmp/scalpel-document-file-missing-path",
			  Scalpel::DocumentFileHardLimitBytes)
			  .status == Scalpel::DocumentFileReadStatus::ReadFailure);
}

TEST_CASE("document file read fails for empty path directory and non-regular") {
	CHECK(Scalpel::ReadDocumentFile({}, Scalpel::DocumentFileHardLimitBytes)
			  .status == Scalpel::DocumentFileReadStatus::ReadFailure);
	// Directories must not throw from iostream filebuf (libstdc++ EISDIR).
	CHECK(Scalpel::ReadDocumentFile("/tmp", Scalpel::DocumentFileHardLimitBytes)
			  .status == Scalpel::DocumentFileReadStatus::ReadFailure);
	CHECK(Scalpel::ReadDocumentFile("/dev/null",
			  Scalpel::DocumentFileHardLimitBytes)
			  .status == Scalpel::DocumentFileReadStatus::ReadFailure);
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

	const Scalpel::DocumentFileReadResult read =
		Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(read.bytes == "rewritten");
}

TEST_CASE("document file write follows a symlink to the target file") {
	TempFile target("old-target");
	const std::string linkPath = target.path + ".link";
	TempLink link(target.path, linkPath);

	REQUIRE(Scalpel::WriteDocumentFile(linkPath, "through-link"));

	const Scalpel::DocumentFileReadResult targetText =
		Scalpel::ReadDocumentFile(target.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(targetText.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(targetText.bytes == "through-link");

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

TEST_CASE("document file read accepts an exact size limit boundary") {
	constexpr std::size_t limit = 32;
	const std::string payload(limit, 'a');
	TempFile file(payload);

	const Scalpel::DocumentFileReadResult read =
		Scalpel::ReadDocumentFile(file.path, limit);
	REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(read.bytes == payload);
	CHECK(read.bytes.size() == limit);
}

TEST_CASE("document file read rejects fstat-visible oversized files") {
	constexpr std::size_t limit = 32;
	SparseTempFile file(static_cast<off_t>(limit + 1));

	const Scalpel::DocumentFileReadResult read =
		Scalpel::ReadDocumentFile(file.path, limit);
	CHECK(read.status == Scalpel::DocumentFileReadStatus::TooLarge);
	CHECK(read.bytes.empty());
}

TEST_CASE("document file read enforces the limit while reading") {
	// /proc regular files often report st_size 0, so the size check runs only
	// against accumulated read bytes rather than fstat.
	constexpr std::size_t limit = 16;
	const Scalpel::DocumentFileReadResult read =
		Scalpel::ReadDocumentFile("/proc/cpuinfo", limit);
	REQUIRE(read.status == Scalpel::DocumentFileReadStatus::TooLarge);
	CHECK(read.bytes.empty());
}

TEST_CASE("document file read rejects production hard-limit sparse files") {
	// Sparse file: fstat reports the size without allocating a 256 MiB payload.
	SparseTempFile file(
		static_cast<off_t>(Scalpel::DocumentFileHardLimitBytes + 1));
	const Scalpel::DocumentFileReadResult read =
		Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	CHECK(read.status == Scalpel::DocumentFileReadStatus::TooLarge);
	CHECK(read.bytes.empty());
}
