// Plain state for coherent Wayland surface and buffer scaling.

#ifndef WAYLANDSCALE_H
#define WAYLANDSCALE_H

#include <cstdint>
#include <optional>
#include <vector>

namespace Scalpel {

/** Preferred Wayland scale denominator (fractional-scale-v1). */
constexpr uint32_t WaylandScaleDenominator = 120;

/**
 * Convert one logical surface dimension to buffer pixels for the preferred
 * scale numerator over WaylandScaleDenominator. Rounds upward so the buffer
 * covers the full destination (same rule as the main surface configuration).
 */
[[nodiscard]] int ScaledBufferDimension(int logical, uint32_t scaleNumerator);

struct WaylandScaleConfiguration {
	int logicalWidth = 0;
	int logicalHeight = 0;
	int bufferWidth = 0;
	int bufferHeight = 0;
	uint32_t scaleNumerator = 120;
	int surfaceBufferScale = 1;
	int cursorScale = 1;
	bool viewportDestination = false;

	friend constexpr bool operator==(const WaylandScaleConfiguration &left,
		const WaylandScaleConfiguration &right) noexcept {
		return left.logicalWidth == right.logicalWidth &&
			left.logicalHeight == right.logicalHeight &&
			left.bufferWidth == right.bufferWidth &&
			left.bufferHeight == right.bufferHeight &&
			left.scaleNumerator == right.scaleNumerator &&
			left.surfaceBufferScale == right.surfaceBufferScale &&
			left.cursorScale == right.cursorScale &&
			left.viewportDestination == right.viewportDestination;
	}
};

/**
 * Selects one scale for a Wayland surface without owning protocol objects.
 *
 * Integer fallback uses the compositor's preferred buffer scale when known,
 * otherwise the greatest scale among entered outputs. Fractional scale is
 * selected only while both optional protocols are available.
 */
class WaylandScaleState final {
public:
	WaylandScaleState(int logicalWidth, int logicalHeight);

	void AddOutput(uint32_t name);
	void RemoveOutput(uint32_t name);
	void EnterOutput(uint32_t name);
	void LeaveOutput(uint32_t name);
	void SetOutputScale(uint32_t name, int scale);
	void SetPreferredBufferScale(int scale);
	void SetFractionalPreferredScale(uint32_t scaleNumerator);
	void ClearFractionalPreferredScale();
	void SetFractionalProtocols(bool viewporterAvailable,
		bool fractionalScaleAvailable);
	void SetBufferScaleAvailable(bool available);
	void Resize(int logicalWidth, int logicalHeight);

	[[nodiscard]] const WaylandScaleConfiguration &Configuration() const noexcept {
		return configuration;
	}
	[[nodiscard]] std::optional<WaylandScaleConfiguration>
		PendingConfiguration() const noexcept;
	void MarkConfigurationApplied(
		const WaylandScaleConfiguration &applied) noexcept;
	[[nodiscard]] std::optional<WaylandScaleConfiguration> TakeConfiguration() noexcept;

private:
	struct Output {
		uint32_t name = 0;
		int scale = 1;
		bool entered = false;
	};

	void Refresh();
	[[nodiscard]] WaylandScaleConfiguration Calculate() const;

	int logicalWidth = 0;
	int logicalHeight = 0;
	std::vector<Output> outputs;
	std::optional<int> preferredBufferScale;
	std::optional<uint32_t> fractionalPreferredScale;
	bool viewporterAvailable = false;
	bool fractionalScaleAvailable = false;
	bool bufferScaleAvailable = true;
	WaylandScaleConfiguration configuration;
	bool configurationPending = true;
};

}

#endif
