// Plain state for compositor-paced frame submission and damage tracking.

#ifndef WAYLANDFRAME_H
#define WAYLANDFRAME_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Scalpel {

struct FrameRectangle {
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;

	friend constexpr bool operator==(const FrameRectangle &left_,
		const FrameRectangle &right_) noexcept {
		return left_.left == right_.left && left_.top == right_.top &&
			left_.right == right_.right && left_.bottom == right_.bottom;
	}
};

struct DamageRectangle {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;

	friend constexpr bool operator==(const DamageRectangle &left,
		const DamageRectangle &right) noexcept {
		return left.x == right.x && left.y == right.y &&
			left.width == right.width && left.height == right.height;
	}
};

struct FramePlan {
	uint64_t submission = 0;
	std::vector<FrameRectangle> submissionDamage;
	std::vector<FrameRectangle> repaintDamage;
	std::vector<DamageRectangle> waylandDamage;
	std::vector<DamageRectangle> eglDamage;
	bool fullSwap = false;
};

struct PresentationResult {
	enum class Kind {
		Presented,
		Discarded,
	};

	uint64_t submission = 0;
	Kind kind = Kind::Discarded;
	uint64_t seconds = 0;
	uint32_t nanoseconds = 0;
	uint32_t refreshNanoseconds = 0;
	uint64_t sequence = 0;
	uint32_t flags = 0;
};

[[nodiscard]] std::vector<FrameRectangle> ClipFrameDamage(
	const std::vector<FrameRectangle> &damage, int width, int height,
	std::size_t maximumRectangles = 16);
[[nodiscard]] std::vector<FrameRectangle> ScaleFrameDamage(
	const std::vector<FrameRectangle> &logicalDamage,
	int logicalWidth, int logicalHeight, int bufferScale,
	std::size_t maximumRectangles = 16);
[[nodiscard]] std::vector<DamageRectangle> WaylandBufferDamage(
	const std::vector<FrameRectangle> &damage);
[[nodiscard]] std::vector<DamageRectangle> EglBufferDamage(
	const std::vector<FrameRectangle> &damage, int bufferHeight);

class WaylandFrameState final {
public:
	void Invalidate(FrameRectangle rectangle);
	[[nodiscard]] bool Invalidated() const noexcept { return invalidated; }
	[[nodiscard]] bool Painting() const noexcept { return painting; }
	[[nodiscard]] bool CallbackOutstanding() const noexcept {
		return callbackOutstanding;
	}
	[[nodiscard]] bool CanSubmit() const noexcept {
		return invalidated && !painting && !callbackOutstanding;
	}

	[[nodiscard]] std::optional<FramePlan> BeginFrame(
		int bufferWidth, int bufferHeight, int bufferAge,
		bool bufferAgeSupported, bool damageSwapSupported);
	[[nodiscard]] std::optional<uint64_t> PrepareFrame(
		uint64_t submission, bool presentationRequested);
	void SubmitFrame(uint64_t submission);
	void CancelPaint();
	void FrameCallbackDone() noexcept;
	void CancelFrameCallback() noexcept;

	void Presented(uint64_t submission, uint64_t seconds, uint32_t nanoseconds,
		uint32_t refreshNanoseconds, uint64_t sequence, uint32_t flags);
	void Discarded(uint64_t submission);
	[[nodiscard]] std::vector<PresentationResult> TakePresentationResults();
	[[nodiscard]] bool FeedbackOutstanding(uint64_t submission) const noexcept;
	[[nodiscard]] std::size_t DamageHistorySize() const noexcept {
		return damageHistory.size();
	}

private:
	static constexpr std::size_t MaximumDamageRectangles = 16;
	static constexpr std::size_t MaximumDamageHistory = 4;
	static constexpr std::size_t MaximumPresentationFeedback = 8;

	bool invalidated = false;
	bool painting = false;
	bool submissionPrepared = false;
	bool callbackOutstanding = false;
	uint64_t nextSubmission = 1;
	uint64_t activeSubmission = 0;
	std::vector<FrameRectangle> pendingDamage;
	std::vector<FrameRectangle> activeDamage;
	std::vector<std::vector<FrameRectangle>> damageHistory;
	std::vector<uint64_t> outstandingFeedback;
	std::vector<PresentationResult> presentationResults;
};

}

#endif
