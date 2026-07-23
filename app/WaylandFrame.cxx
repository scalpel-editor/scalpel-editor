#include "WaylandFrame.h"

#include <algorithm>
#include <climits>
#include <stdexcept>
#include <utility>

namespace Scalpel {

namespace {

[[nodiscard]] bool Empty(const FrameRectangle &rectangle) noexcept {
	return rectangle.left >= rectangle.right || rectangle.top >= rectangle.bottom;
}

[[nodiscard]] FrameRectangle Bounds(
	const std::vector<FrameRectangle> &rectangles) noexcept {
	FrameRectangle bounds = rectangles.front();
	for (const FrameRectangle &rectangle : rectangles) {
		bounds.left = std::min(bounds.left, rectangle.left);
		bounds.top = std::min(bounds.top, rectangle.top);
		bounds.right = std::max(bounds.right, rectangle.right);
		bounds.bottom = std::max(bounds.bottom, rectangle.bottom);
	}
	return bounds;
}

void AppendDamage(std::vector<FrameRectangle> &target,
	const std::vector<FrameRectangle> &source, std::size_t maximumRectangles) {
	target.insert(target.end(), source.begin(), source.end());
	if (target.size() > maximumRectangles) {
		target = {Bounds(target)};
	}
}

}

std::vector<FrameRectangle> ClipFrameDamage(
	const std::vector<FrameRectangle> &damage, int width, int height,
	std::size_t maximumRectangles) {
	if (width <= 0 || height <= 0) {
		throw std::invalid_argument("frame damage requires positive buffer dimensions");
	}
	if (maximumRectangles == 0) {
		throw std::invalid_argument("frame damage requires a positive rectangle limit");
	}
	std::vector<FrameRectangle> clipped;
	clipped.reserve(std::min(damage.size(), maximumRectangles));
	for (const FrameRectangle &rectangle : damage) {
		const FrameRectangle bounded{
			std::clamp(rectangle.left, 0, width),
			std::clamp(rectangle.top, 0, height),
			std::clamp(rectangle.right, 0, width),
			std::clamp(rectangle.bottom, 0, height),
		};
		if (!Empty(bounded)) {
			clipped.push_back(bounded);
		}
	}
	if (clipped.size() > maximumRectangles) {
		return {Bounds(clipped)};
	}
	return clipped;
}

std::vector<FrameRectangle> ScaleFrameDamage(
	const std::vector<FrameRectangle> &logicalDamage,
	int logicalWidth, int logicalHeight, int bufferScale,
	std::size_t maximumRectangles) {
	if (bufferScale <= 0) {
		throw std::invalid_argument("frame damage requires a positive buffer scale");
	}
	return ScaleFrameDamageFractional(logicalDamage, logicalWidth, logicalHeight,
		static_cast<uint32_t>(bufferScale), 1, maximumRectangles);
}

std::vector<FrameRectangle> ScaleFrameDamageFractional(
	const std::vector<FrameRectangle> &logicalDamage,
	int logicalWidth, int logicalHeight, uint32_t scaleNumerator,
	uint32_t scaleDenominator, std::size_t maximumRectangles) {
	if (scaleNumerator == 0 || scaleDenominator == 0) {
		throw std::invalid_argument("frame damage requires a positive scale");
	}
	const std::vector<FrameRectangle> clipped = ClipFrameDamage(
		logicalDamage, logicalWidth, logicalHeight, maximumRectangles);
	std::vector<FrameRectangle> scaled;
	scaled.reserve(clipped.size());
	for (const FrameRectangle &rectangle : clipped) {
		const auto floorScaled = [scaleNumerator, scaleDenominator](int value) {
			const uint64_t result =
				static_cast<uint64_t>(value) * scaleNumerator / scaleDenominator;
			if (result > INT_MAX) {
				throw std::overflow_error("scaled frame damage exceeds integer coordinates");
			}
			return static_cast<int>(result);
		};
		const auto ceilScaled = [scaleNumerator, scaleDenominator](int value) {
			const uint64_t result =
				(static_cast<uint64_t>(value) * scaleNumerator +
					scaleDenominator - 1) / scaleDenominator;
			if (result > INT_MAX) {
				throw std::overflow_error("scaled frame damage exceeds integer coordinates");
			}
			return static_cast<int>(result);
		};
		scaled.push_back({
			floorScaled(rectangle.left),
			floorScaled(rectangle.top),
			ceilScaled(rectangle.right),
			ceilScaled(rectangle.bottom),
		});
	}
	return scaled;
}

std::vector<FrameRectangle> ScaleFrameDamageToBuffer(
	const std::vector<FrameRectangle> &logicalDamage,
	int logicalWidth, int logicalHeight, int bufferWidth, int bufferHeight,
	std::size_t maximumRectangles) {
	if (bufferWidth <= 0 || bufferHeight <= 0) {
		throw std::invalid_argument("frame damage requires positive buffer dimensions");
	}
	const std::vector<FrameRectangle> clipped = ClipFrameDamage(
		logicalDamage, logicalWidth, logicalHeight, maximumRectangles);
	std::vector<FrameRectangle> scaled;
	scaled.reserve(clipped.size());
	for (const FrameRectangle &rectangle : clipped) {
		scaled.push_back({
			static_cast<int>(
				static_cast<int64_t>(rectangle.left) * bufferWidth / logicalWidth),
			static_cast<int>(
				static_cast<int64_t>(rectangle.top) * bufferHeight / logicalHeight),
			static_cast<int>(
				(static_cast<int64_t>(rectangle.right) * bufferWidth +
					logicalWidth - 1) / logicalWidth),
			static_cast<int>(
				(static_cast<int64_t>(rectangle.bottom) * bufferHeight +
					logicalHeight - 1) / logicalHeight),
		});
	}
	return scaled;
}

std::vector<DamageRectangle> WaylandBufferDamage(
	const std::vector<FrameRectangle> &damage) {
	std::vector<DamageRectangle> rectangles;
	rectangles.reserve(damage.size());
	for (const FrameRectangle &rectangle : damage) {
		if (!Empty(rectangle)) {
			rectangles.push_back({rectangle.left, rectangle.top,
				rectangle.right - rectangle.left,
				rectangle.bottom - rectangle.top});
		}
	}
	return rectangles;
}

std::vector<DamageRectangle> EglBufferDamage(
	const std::vector<FrameRectangle> &damage, int bufferHeight) {
	if (bufferHeight <= 0) {
		throw std::invalid_argument("EGL damage requires a positive buffer height");
	}
	std::vector<DamageRectangle> rectangles;
	rectangles.reserve(damage.size());
	for (const FrameRectangle &rectangle : damage) {
		if (!Empty(rectangle)) {
			rectangles.push_back({rectangle.left,
				bufferHeight - rectangle.bottom,
				rectangle.right - rectangle.left,
				rectangle.bottom - rectangle.top});
		}
	}
	return rectangles;
}

FramePlan ScaleFramePlan(
	FramePlan plan, int logicalWidth, int logicalHeight,
	int bufferWidth, int bufferHeight) {
	const std::vector<FrameRectangle> scaledSubmission =
		ScaleFrameDamageToBuffer(
			plan.submissionDamage, logicalWidth, logicalHeight,
			bufferWidth, bufferHeight);
	const std::vector<FrameRectangle> paintBounds =
		plan.repaintDamage.empty() ?
			std::vector<FrameRectangle>{} :
			std::vector<FrameRectangle>{Bounds(plan.repaintDamage)};
	const std::vector<FrameRectangle> scaledRepaint =
		ScaleFrameDamageToBuffer(
			paintBounds, logicalWidth, logicalHeight,
			bufferWidth, bufferHeight);
	plan.waylandDamage = WaylandBufferDamage(scaledSubmission);
	plan.eglDamage = EglBufferDamage(scaledRepaint, bufferHeight);
	return plan;
}

void WaylandFrameState::Invalidate(FrameRectangle rectangle) {
	invalidated = true;
	pendingDamage.push_back(rectangle);
	if (pendingDamage.size() > MaximumDamageRectangles) {
		pendingDamage = {Bounds(pendingDamage)};
	}
}

std::optional<FramePlan> WaylandFrameState::BeginFrame(
	int bufferWidth, int bufferHeight, int bufferAge,
	bool bufferAgeSupported, bool damageSwapSupported) {
	if (!CanSubmit()) {
		return std::nullopt;
	}
	std::vector<FrameRectangle> submissionDamage = ClipFrameDamage(
		pendingDamage, bufferWidth, bufferHeight, MaximumDamageRectangles);
	if (submissionDamage.empty()) {
		submissionDamage = {{0, 0, bufferWidth, bufferHeight}};
	}
	const bool bufferSizeChanged = lastSubmittedBufferWidth != 0 &&
		(bufferWidth != lastSubmittedBufferWidth ||
			bufferHeight != lastSubmittedBufferHeight);
	if (bufferSizeChanged) {
		damageHistory.clear();
	}
	pendingDamage.clear();
	invalidated = false;
	activeDamage = submissionDamage;
	activeSubmission = nextSubmission++;
	activeBufferWidth = bufferWidth;
	activeBufferHeight = bufferHeight;
	painting = true;

	std::vector<FrameRectangle> repaintDamage = submissionDamage;
	const bool validAge = !bufferSizeChanged &&
		bufferAgeSupported && bufferAge > 0 &&
		static_cast<std::size_t>(bufferAge - 1) <= damageHistory.size();
	if (!validAge) {
		repaintDamage = {{0, 0, bufferWidth, bufferHeight}};
	} else {
		for (int age = 1; age < bufferAge; ++age) {
			AppendDamage(repaintDamage,
				damageHistory[static_cast<std::size_t>(age - 1)],
				MaximumDamageRectangles);
		}
	}
	repaintDamage = ClipFrameDamage(
		repaintDamage, bufferWidth, bufferHeight, MaximumDamageRectangles);
	const std::vector<FrameRectangle> paintBounds = {Bounds(repaintDamage)};

	return FramePlan{
		activeSubmission,
		std::move(submissionDamage),
		repaintDamage,
		WaylandBufferDamage(activeDamage),
		EglBufferDamage(paintBounds, bufferHeight),
		!damageSwapSupported,
	};
}

std::optional<uint64_t> WaylandFrameState::PrepareFrame(
	uint64_t submission, bool presentationRequested) {
	if (!painting || submission != activeSubmission || submissionPrepared) {
		throw std::logic_error("prepared frame does not match the active paint");
	}
	submissionPrepared = true;
	callbackOutstanding = true;
	if (!presentationRequested) {
		return std::nullopt;
	}
	std::optional<uint64_t> expired;
	if (outstandingFeedback.size() == MaximumPresentationFeedback) {
		expired = outstandingFeedback.front();
		outstandingFeedback.erase(outstandingFeedback.begin());
	}
	outstandingFeedback.push_back(submission);
	return expired;
}

void WaylandFrameState::SubmitFrame(uint64_t submission) {
	if (!painting || submission != activeSubmission || !submissionPrepared) {
		throw std::logic_error("submitted frame does not match the active paint");
	}
	painting = false;
	submissionPrepared = false;
	damageHistory.insert(damageHistory.begin(), std::move(activeDamage));
	if (damageHistory.size() > MaximumDamageHistory) {
		damageHistory.resize(MaximumDamageHistory);
	}
	lastSubmittedBufferWidth = activeBufferWidth;
	lastSubmittedBufferHeight = activeBufferHeight;
	activeSubmission = 0;
	activeBufferWidth = 0;
	activeBufferHeight = 0;
}

void WaylandFrameState::CancelPaint() {
	if (!painting) {
		return;
	}
	AppendDamage(pendingDamage, activeDamage, MaximumDamageRectangles);
	invalidated = true;
	const auto feedback = std::find(
		outstandingFeedback.begin(), outstandingFeedback.end(), activeSubmission);
	if (feedback != outstandingFeedback.end()) {
		outstandingFeedback.erase(feedback);
	}
	activeDamage.clear();
	activeSubmission = 0;
	activeBufferWidth = 0;
	activeBufferHeight = 0;
	submissionPrepared = false;
	painting = false;
}

void WaylandFrameState::FrameCallbackDone() noexcept {
	callbackOutstanding = false;
}

void WaylandFrameState::CancelFrameCallback() noexcept {
	callbackOutstanding = false;
}

void WaylandFrameState::ResetDamageHistory() noexcept {
	damageHistory.clear();
	lastSubmittedBufferWidth = 0;
	lastSubmittedBufferHeight = 0;
}

bool WaylandFrameState::FeedbackOutstanding(uint64_t submission) const noexcept {
	return std::find(outstandingFeedback.begin(), outstandingFeedback.end(),
		submission) != outstandingFeedback.end();
}

void WaylandFrameState::Presented(uint64_t submission, uint64_t seconds,
	uint32_t nanoseconds, uint32_t refreshNanoseconds,
	uint64_t sequence, uint32_t flags) {
	const auto found = std::find(
		outstandingFeedback.begin(), outstandingFeedback.end(), submission);
	if (found == outstandingFeedback.end()) {
		return;
	}
	outstandingFeedback.erase(found);
	presentationResults.push_back({
		submission, PresentationResult::Kind::Presented, seconds, nanoseconds,
		refreshNanoseconds, sequence, flags,
	});
}

void WaylandFrameState::Discarded(uint64_t submission) {
	const auto found = std::find(
		outstandingFeedback.begin(), outstandingFeedback.end(), submission);
	if (found == outstandingFeedback.end()) {
		return;
	}
	outstandingFeedback.erase(found);
	presentationResults.push_back({
		submission, PresentationResult::Kind::Discarded,
	});
}

std::vector<PresentationResult> WaylandFrameState::TakePresentationResults() {
	return std::exchange(presentationResults, {});
}

}
