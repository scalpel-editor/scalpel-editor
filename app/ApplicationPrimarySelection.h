// Primary-selection requests and results exchanged with the application shell.

#ifndef APPLICATIONPRIMARYSELECTION_H
#define APPLICATIONPRIMARYSELECTION_H

#include <cstdint>
#include <optional>
#include <string>

namespace Scalpel {

enum class ApplicationPrimarySelectionOperation {
	Publish,
	Paste,
};

enum class ApplicationPrimarySelectionStatus {
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

struct ApplicationPrimarySelectionRequest {
	uint64_t id = 0;
	ApplicationPrimarySelectionOperation operation =
		ApplicationPrimarySelectionOperation::Publish;
	std::optional<std::string> text;
};

struct ApplicationPrimarySelectionResult {
	uint64_t id = 0;
	ApplicationPrimarySelectionOperation operation =
		ApplicationPrimarySelectionOperation::Publish;
	ApplicationPrimarySelectionStatus status =
		ApplicationPrimarySelectionStatus::Failed;
};

}

#endif
