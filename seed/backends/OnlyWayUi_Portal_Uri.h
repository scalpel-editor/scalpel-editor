#pragma once

#include <OnlyWayUi/Config/Config.h>
namespace Backend {

// Convert a portal "file://" URI into a local filesystem path, percent-decoding the path component. Returns false for a
// URI that is not a local file:// URI. An optional host (file://host/path) is dropped; the path starts at its slash. On
// success out holds the decoded path; on failure out is left unchanged.
bool PortalUriToLocalPath(const char* uri, OnlyWayUi::String& out);

} // namespace Backend
