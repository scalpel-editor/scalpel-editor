// Clipboard requests and results exchanged with the application shell.

#ifndef APPLICATIONCLIPBOARD_H
#define APPLICATIONCLIPBOARD_H

#include <cstdint>
#include <string>

namespace Scalpel {

enum class ApplicationClipboardOperation {
	Copy,
	Paste,
};

enum class ApplicationClipboardStatus {
	Published,
	Complete,
	Unavailable,
	NoText,
	InvalidText,
	Cancelled,
	Failed,
	TooLarge,
	TimedOut,
	Superseded,
};

struct ApplicationClipboardRequest {
	uint64_t id = 0;
	ApplicationClipboardOperation operation = ApplicationClipboardOperation::Copy;
	std::string text;
};

struct ApplicationClipboardResult {
	uint64_t id = 0;
	ApplicationClipboardOperation operation = ApplicationClipboardOperation::Copy;
	ApplicationClipboardStatus status = ApplicationClipboardStatus::Failed;
};

}

#endif
