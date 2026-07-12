#include "OnlyWayUi_Portal_Uri.h"
#include <OnlyWayUi/Config/Config.h>
#include <cstring>

namespace Backend {

namespace {

bool IsHexDigit(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int HexValue(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return c - 'A' + 10;
}

} // namespace

bool PortalUriToLocalPath(const char* uri, OnlyWayUi::String& out)
{
	const char* const scheme = "file://";
	const size_t scheme_length = 7;
	if (std::strncmp(uri, scheme, scheme_length) != 0)
		return false;

	const char* path = std::strchr(uri + scheme_length, '/');
	if (!path)
		return false;

	out.clear();
	for (const char* c = path; *c; ++c)
	{
		if (*c == '%' && IsHexDigit(c[1]) && IsHexDigit(c[2]))
		{
			out.push_back(char((HexValue(c[1]) << 4) | HexValue(c[2])));
			c += 2;
		}
		else
		{
			out.push_back(*c);
		}
	}
	return true;
}

} // namespace Backend
