#include "WaylandScale.h"

#include <algorithm>
#include <climits>
#include <stdexcept>
#include <utility>

namespace Scalpel {

int ScaledBufferDimension(int logical, uint32_t scaleNumerator) {
	if (logical <= 0) {
		throw std::invalid_argument("Wayland buffer dimension requires a positive logical size");
	}
	if (scaleNumerator == 0) {
		throw std::invalid_argument("Wayland buffer dimension requires a positive scale");
	}
	const uint64_t scaled = static_cast<uint64_t>(logical) * scaleNumerator;
	const uint64_t rounded =
		(scaled + WaylandScaleDenominator - 1) / WaylandScaleDenominator;
	if (rounded == 0 || rounded > INT_MAX) {
		throw std::overflow_error("Wayland scale exceeds supported buffer dimensions");
	}
	return static_cast<int>(rounded);
}

WaylandScaleState::WaylandScaleState(int logicalWidth_, int logicalHeight_) :
	logicalWidth(logicalWidth_), logicalHeight(logicalHeight_) {
	if (logicalWidth <= 0 || logicalHeight <= 0) {
		throw std::invalid_argument("Wayland scale requires a positive logical size");
	}
	configuration = Calculate();
}

void WaylandScaleState::AddOutput(uint32_t name) {
	if (std::find_if(outputs.begin(), outputs.end(),
		[name](const Output &output) { return output.name == name; }) == outputs.end()) {
		outputs.push_back({name});
	}
}

void WaylandScaleState::RemoveOutput(uint32_t name) {
	const auto found = std::find_if(outputs.begin(), outputs.end(),
		[name](const Output &output) { return output.name == name; });
	if (found != outputs.end()) {
		outputs.erase(found);
		Refresh();
	}
}

void WaylandScaleState::EnterOutput(uint32_t name) {
	const auto found = std::find_if(outputs.begin(), outputs.end(),
		[name](const Output &output) { return output.name == name; });
	if (found != outputs.end() && !found->entered) {
		found->entered = true;
		Refresh();
	}
}

void WaylandScaleState::LeaveOutput(uint32_t name) {
	const auto found = std::find_if(outputs.begin(), outputs.end(),
		[name](const Output &output) { return output.name == name; });
	if (found != outputs.end() && found->entered) {
		found->entered = false;
		Refresh();
	}
}

void WaylandScaleState::SetOutputScale(uint32_t name, int scale) {
	if (scale <= 0) {
		throw std::invalid_argument("Wayland output scale must be positive");
	}
	const auto found = std::find_if(outputs.begin(), outputs.end(),
		[name](const Output &output) { return output.name == name; });
	if (found != outputs.end() && found->scale != scale) {
		found->scale = scale;
		Refresh();
	}
}

void WaylandScaleState::SetPreferredBufferScale(int scale) {
	if (scale <= 0) {
		throw std::invalid_argument("Wayland preferred buffer scale must be positive");
	}
	if (preferredBufferScale != scale) {
		preferredBufferScale = scale;
		Refresh();
	}
}

void WaylandScaleState::SetFractionalPreferredScale(uint32_t scaleNumerator) {
	if (scaleNumerator == 0) {
		throw std::invalid_argument("Wayland fractional scale must be positive");
	}
	if (fractionalPreferredScale != scaleNumerator) {
		fractionalPreferredScale = scaleNumerator;
		Refresh();
	}
}

void WaylandScaleState::ClearFractionalPreferredScale() {
	if (fractionalPreferredScale) {
		fractionalPreferredScale.reset();
		Refresh();
	}
}

void WaylandScaleState::SetFractionalProtocols(
	bool viewporterAvailable_, bool fractionalScaleAvailable_) {
	if (viewporterAvailable != viewporterAvailable_ ||
		fractionalScaleAvailable != fractionalScaleAvailable_) {
		viewporterAvailable = viewporterAvailable_;
		fractionalScaleAvailable = fractionalScaleAvailable_;
		Refresh();
	}
}

void WaylandScaleState::SetBufferScaleAvailable(bool available) {
	if (bufferScaleAvailable != available) {
		bufferScaleAvailable = available;
		Refresh();
	}
}

void WaylandScaleState::Resize(int logicalWidth_, int logicalHeight_) {
	if (logicalWidth_ <= 0 || logicalHeight_ <= 0) {
		throw std::invalid_argument("Wayland scale requires a positive logical size");
	}
	if (logicalWidth != logicalWidth_ || logicalHeight != logicalHeight_) {
		logicalWidth = logicalWidth_;
		logicalHeight = logicalHeight_;
		Refresh();
	}
}

std::optional<WaylandScaleConfiguration>
WaylandScaleState::PendingConfiguration() const noexcept {
	if (!configurationPending) {
		return std::nullopt;
	}
	return configuration;
}

void WaylandScaleState::MarkConfigurationApplied(
	const WaylandScaleConfiguration &applied) noexcept {
	if (applied == configuration) {
		configurationPending = false;
	}
}

std::optional<WaylandScaleConfiguration>
WaylandScaleState::TakeConfiguration() noexcept {
	const std::optional<WaylandScaleConfiguration> pending =
		PendingConfiguration();
	if (pending) {
		MarkConfigurationApplied(*pending);
	}
	return pending;
}

void WaylandScaleState::Refresh() {
	const WaylandScaleConfiguration next = Calculate();
	if (!(next == configuration)) {
		configuration = next;
		configurationPending = true;
	}
}

WaylandScaleConfiguration WaylandScaleState::Calculate() const {
	const bool fractional = viewporterAvailable && fractionalScaleAvailable &&
		fractionalPreferredScale.has_value();
	uint32_t numerator = WaylandScaleDenominator;
	int surfaceBufferScale = 1;
	if (fractional) {
		numerator = *fractionalPreferredScale;
	} else {
		int integerScale = preferredBufferScale.value_or(1);
		if (!preferredBufferScale) {
			for (const Output &output : outputs) {
				if (output.entered) {
					integerScale = std::max(integerScale, output.scale);
				}
			}
		}
		if (!bufferScaleAvailable) {
			integerScale = 1;
		}
		if (integerScale > INT_MAX / static_cast<int>(WaylandScaleDenominator)) {
			throw std::overflow_error("Wayland integer scale is too large");
		}
		numerator = static_cast<uint32_t>(integerScale) * WaylandScaleDenominator;
		surfaceBufferScale = integerScale;
	}
	const uint64_t cursorRounded =
		(static_cast<uint64_t>(numerator) + WaylandScaleDenominator - 1) /
		WaylandScaleDenominator;
	if (cursorRounded == 0 || cursorRounded > INT_MAX) {
		throw std::overflow_error("Wayland scale exceeds supported cursor size");
	}
	return {
		logicalWidth,
		logicalHeight,
		ScaledBufferDimension(logicalWidth, numerator),
		ScaledBufferDimension(logicalHeight, numerator),
		numerator,
		surfaceBufferScale,
		static_cast<int>(cursorRounded),
		fractional,
	};
}

}
