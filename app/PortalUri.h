// Convert desktop-portal file:// URIs into local filesystem paths.

#ifndef PORTALURI_H
#define PORTALURI_H

#include <string>
#include <string_view>

namespace Scalpel {

/**
 * Convert a portal "file://" URI into a local filesystem path, percent-decoding
 * the path component. Returns false for a URI that is not a local file:// URI.
 * An optional host (file://host/path) is dropped; the path starts at its slash.
 * On success out holds the decoded path; on failure out is left unchanged.
 */
[[nodiscard]] bool PortalUriToLocalPath(std::string_view uri, std::string &out);

}

#endif
