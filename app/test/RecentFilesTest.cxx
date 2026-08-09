#include "catch.hpp"

#include <cstdio>
#include <string>
#include <unistd.h>

#include "DocumentFile.h"
#include "RecentFiles.h"

namespace {

class TempDirectory {
public:
	TempDirectory() {
		char pattern[] = "/tmp/scalpel-recent-XXXXXX";
		char *created = mkdtemp(pattern);
		REQUIRE(created != nullptr);
		path = created;
	}

	~TempDirectory() {
		const std::string state = path + "/nested/recent-files";
		(void)std::remove(state.c_str());
		(void)rmdir((path + "/nested").c_str());
		(void)rmdir(path.c_str());
	}

	TempDirectory(const TempDirectory &) = delete;
	TempDirectory &operator=(const TempDirectory &) = delete;

	std::string path;
};

}

TEST_CASE("recent files promote normalize deduplicate and cap paths") {
	Scalpel::RecentFiles recent;
	CHECK_FALSE(recent.Record(""));
	CHECK(recent.Record("/work/one.txt"));
	CHECK(recent.Record("/work/./two.txt"));
	CHECK(recent.Record("/work/one.txt"));
	REQUIRE(recent.Paths().size() == 2);
	CHECK(recent.Paths()[0] == "/work/one.txt");
	CHECK(recent.Paths()[1] == "/work/two.txt");
	CHECK_FALSE(recent.Record("/work/one.txt"));

	for (std::size_t index = 0; index < Scalpel::MaximumRecentFiles + 3; ++index) {
		CHECK(recent.Record("/many/" + std::to_string(index)));
	}
	REQUIRE(recent.Paths().size() == Scalpel::MaximumRecentFiles);
	CHECK(recent.Paths().front() == "/many/12");
	CHECK(recent.Paths().back() == "/many/3");
}

TEST_CASE("recent files remove and clear report changes") {
	Scalpel::RecentFiles recent;
	(void)recent.Record("/work/one");
	(void)recent.Record("/work/two");

	CHECK(recent.Remove("/work/./one"));
	CHECK_FALSE(recent.Remove("/work/missing"));
	REQUIRE(recent.Paths().size() == 1);
	CHECK(recent.Paths().front() == "/work/two");
	CHECK(recent.Clear());
	CHECK_FALSE(recent.Clear());
	CHECK(recent.Paths().empty());
}

TEST_CASE("recent files state path follows XDG state home") {
	CHECK(Scalpel::RecentFilesStatePath("/state", "/home/user") ==
		"/state/scalpel-editor/recent-files");
	CHECK(Scalpel::RecentFilesStatePath({}, "/home/user") ==
		"/home/user/.local/state/scalpel-editor/recent-files");
	CHECK(Scalpel::RecentFilesStatePath({}, {}).empty());
}

TEST_CASE("recent files state round trips unusual valid paths") {
	TempDirectory directory;
	const std::string statePath = directory.path + "/nested/recent-files";
	Scalpel::RecentFiles recent;
	(void)recent.Record("/work/name\nwith-newline.txt");
	(void)recent.Record("/work/second.txt");

	REQUIRE(Scalpel::SaveRecentFiles(statePath, recent));
	const Scalpel::RecentFiles loaded = Scalpel::LoadRecentFiles(statePath);
	CHECK(loaded.Paths() == recent.Paths());
}

TEST_CASE("recent files malformed and missing state is empty") {
	TempDirectory directory;
	const std::string statePath = directory.path + "/nested/recent-files";
	CHECK(Scalpel::LoadRecentFiles(statePath).Paths().empty());

	Scalpel::RecentFiles empty;
	REQUIRE(Scalpel::SaveRecentFiles(statePath, empty));
	REQUIRE(Scalpel::WriteDocumentFile(statePath, "not-a-recent-file").status ==
		Scalpel::DocumentFileWriteStatus::Success);
	CHECK(Scalpel::LoadRecentFiles(statePath).Paths().empty());
}

TEST_CASE("recent files state write failure leaves in-memory list") {
	Scalpel::RecentFiles recent;
	(void)recent.Record("/work/one");
	CHECK_FALSE(Scalpel::SaveRecentFiles({}, recent));
	CHECK_FALSE(Scalpel::SaveRecentFiles("/proc/scalpel-editor/recent-files", recent));
	REQUIRE(recent.Paths().size() == 1);
	CHECK(recent.Paths().front() == "/work/one");
}
