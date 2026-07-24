#include "PortalUri.h"

namespace Scalpel {
namespace {

[[nodiscard]] bool IsHexDigit(char c) noexcept {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
		(c >= 'A' && c <= 'F');
}

[[nodiscard]] int HexValue(char c) noexcept {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	return c - 'A' + 10;
}

}

bool PortalUriToLocalPath(std::string_view uri, std::string &out) {
	constexpr std::string_view scheme = "file://";
	if (uri.size() < scheme.size() ||
		uri.compare(0, scheme.size(), scheme) != 0) {
		return false;
	}

	const std::size_t pathOffset = uri.find('/', scheme.size());
	if (pathOffset == std::string_view::npos) {
		return false;
	}

	std::string decoded;
	decoded.reserve(uri.size() - pathOffset);
	for (std::size_t index = pathOffset; index < uri.size(); ++index) {
		const char c = uri[index];
		if (c == '%' && index + 2 < uri.size() &&
			IsHexDigit(uri[index + 1]) && IsHexDigit(uri[index + 2])) {
			decoded.push_back(static_cast<char>(
				(HexValue(uri[index + 1]) << 4) | HexValue(uri[index + 2])));
			index += 2;
		} else {
			decoded.push_back(c);
		}
	}
	out = std::move(decoded);
	return true;
}

}
