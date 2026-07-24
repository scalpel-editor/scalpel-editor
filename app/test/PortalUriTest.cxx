#include "catch.hpp"

#include "PortalUri.h"

TEST_CASE("portal URI maps a simple file path") {
	std::string path;
	REQUIRE(Scalpel::PortalUriToLocalPath("file:///home/user/notes.txt", path));
	CHECK(path == "/home/user/notes.txt");
}

TEST_CASE("portal URI percent-decodes a space") {
	std::string path;
	REQUIRE(Scalpel::PortalUriToLocalPath(
		"file:///home/user/my%20file.txt", path));
	CHECK(path == "/home/user/my file.txt");
}

TEST_CASE("portal URI percent-decodes uppercase hex") {
	// %2F is an encoded slash; the decoder treats the bytes literally without
	// special-casing separators, and accepts both upper- and lowercase hex.
	std::string path;
	REQUIRE(Scalpel::PortalUriToLocalPath("file:///tmp/a%2Fb%2bc", path));
	CHECK(path == "/tmp/a/b+c");
}

TEST_CASE("portal URI drops a host authority") {
	std::string path;
	REQUIRE(Scalpel::PortalUriToLocalPath("file://hostname/etc/hosts", path));
	CHECK(path == "/etc/hosts");
}

TEST_CASE("portal URI rejects a non-file scheme") {
	std::string path = "unchanged";
	CHECK_FALSE(Scalpel::PortalUriToLocalPath(
		"http://example.com/file.txt", path));
	CHECK(path == "unchanged");
}

TEST_CASE("portal URI rejects a missing path") {
	std::string path = "unchanged";
	CHECK_FALSE(Scalpel::PortalUriToLocalPath("file://host", path));
	CHECK(path == "unchanged");
}

TEST_CASE("portal URI keeps an incomplete escape literal") {
	// A '%' not followed by two hex digits is not an escape; it is copied.
	std::string path;
	REQUIRE(Scalpel::PortalUriToLocalPath("file:///tmp/50%25done/tail%2", path));
	CHECK(path == "/tmp/50%done/tail%2");
}

TEST_CASE("portal URI accepts a slash-only path after a host") {
	std::string path;
	REQUIRE(Scalpel::PortalUriToLocalPath("file://host/", path));
	CHECK(path == "/");
}
