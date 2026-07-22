#include "WaylandLifecycle.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Scalpel {

WaylandLifecycle::WaylandLifecycle(int width, int height) : currentSize{width, height} {
	if (width <= 0 || height <= 0) {
		throw std::invalid_argument("WaylandLifecycle requires a positive size");
	}
}

std::vector<WaylandLifecycleAction> WaylandLifecycle::AddGlobal(
	WaylandGlobalKind kind, uint32_t name, uint32_t version) {
	if (HasGlobal(name)) {
		return {};
	}
	globals.push_back(Global{kind, name, version});
	switch (kind) {
	case WaylandGlobalKind::Compositor:
		if (!compositorName && !closeRequested) {
			compositorName = name;
			return {{WaylandLifecycleActionType::BindCompositor, name, version}};
		}
		break;
	case WaylandGlobalKind::WmBase:
		if (!wmBaseName && !closeRequested) {
			wmBaseName = name;
			return {{WaylandLifecycleActionType::BindWmBase, name, version}};
		}
		break;
	case WaylandGlobalKind::Output:
		return {{WaylandLifecycleActionType::BindOutput, name, version}};
	case WaylandGlobalKind::Seat:
		if (!activeSeatName) {
			activeSeatName = name;
			return {{WaylandLifecycleActionType::BindSeat, name, version}};
		}
		break;
	}
	return {};
}

std::vector<WaylandLifecycleAction> WaylandLifecycle::RemoveGlobal(uint32_t name) {
	const auto found = std::find_if(globals.begin(), globals.end(),
		[name](const Global &global) { return global.name == name; });
	if (found == globals.end()) {
		return {};
	}
	const Global removed = *found;
	globals.erase(found);
	if ((compositorName && *compositorName == name) ||
		(wmBaseName && *wmBaseName == name)) {
		RequestClose();
		return {{WaylandLifecycleActionType::Close, name}};
	}
	if (removed.kind == WaylandGlobalKind::Output) {
		return {{WaylandLifecycleActionType::ReleaseOutput, name}};
	}
	if (removed.kind == WaylandGlobalKind::Seat && activeSeatName == name) {
		std::vector<WaylandLifecycleAction> actions;
		if (removed.hasPointer) {
			actions.push_back({WaylandLifecycleActionType::ReleasePointer, name});
		}
		if (removed.hasKeyboard) {
			actions.push_back({WaylandLifecycleActionType::ReleaseKeyboard, name});
		}
		actions.push_back({WaylandLifecycleActionType::ReleaseSeat, name});
		activeSeatName.reset();
		const auto replacement = std::find_if(globals.begin(), globals.end(),
			[](const Global &global) { return global.kind == WaylandGlobalKind::Seat; });
		if (replacement != globals.end()) {
			activeSeatName = replacement->name;
			actions.push_back({WaylandLifecycleActionType::BindSeat,
				replacement->name, replacement->version});
		}
		return actions;
	}
	return {};
}

std::vector<WaylandLifecycleAction> WaylandLifecycle::UpdateSeatCapabilities(
	uint32_t name, bool hasPointer, bool hasKeyboard) {
	if (activeSeatName != name) {
		return {};
	}
	const auto found = std::find_if(globals.begin(), globals.end(),
		[name](const Global &global) { return global.name == name; });
	if (found == globals.end() || found->kind != WaylandGlobalKind::Seat) {
		return {};
	}
	std::vector<WaylandLifecycleAction> actions;
	if (found->hasPointer && !hasPointer) {
		actions.push_back({WaylandLifecycleActionType::ReleasePointer, name});
	}
	if (found->hasKeyboard && !hasKeyboard) {
		actions.push_back({WaylandLifecycleActionType::ReleaseKeyboard, name});
	}
	if (!found->hasPointer && hasPointer) {
		actions.push_back({WaylandLifecycleActionType::CreatePointer, name});
	}
	if (!found->hasKeyboard && hasKeyboard) {
		actions.push_back({WaylandLifecycleActionType::CreateKeyboard, name});
	}
	found->hasPointer = hasPointer;
	found->hasKeyboard = hasKeyboard;
	return actions;
}

void WaylandLifecycle::EnterOutput(uint32_t name) noexcept {
	const auto found = std::find_if(globals.begin(), globals.end(),
		[name](const Global &global) { return global.name == name; });
	if (found != globals.end() && found->kind == WaylandGlobalKind::Output) {
		found->entered = true;
	}
}

void WaylandLifecycle::LeaveOutput(uint32_t name) noexcept {
	const auto found = std::find_if(globals.begin(), globals.end(),
		[name](const Global &global) { return global.name == name; });
	if (found != globals.end() && found->kind == WaylandGlobalKind::Output) {
		found->entered = false;
	}
}

void WaylandLifecycle::ProposeSize(int width, int height) noexcept {
	if (width > 0 && height > 0) {
		proposedSize = WindowSize{width, height};
	} else {
		proposedSize.reset();
	}
}

std::optional<WindowSize> WaylandLifecycle::CommitConfigure() noexcept {
	std::optional<WindowSize> committed;
	if (proposedSize && *proposedSize != currentSize) {
		currentSize = *proposedSize;
		pendingResize = currentSize;
		committed = currentSize;
	}
	proposedSize.reset();
	return committed;
}

void WaylandLifecycle::RequestClose() noexcept {
	closeRequested = true;
}

std::optional<WindowSize> WaylandLifecycle::TakeResize() noexcept {
	return std::exchange(pendingResize, std::nullopt);
}

size_t WaylandLifecycle::OutputCount() const noexcept {
	return static_cast<size_t>(std::count_if(globals.begin(), globals.end(),
		[](const Global &global) { return global.kind == WaylandGlobalKind::Output; }));
}

size_t WaylandLifecycle::EnteredOutputCount() const noexcept {
	return static_cast<size_t>(std::count_if(globals.begin(), globals.end(),
		[](const Global &global) {
			return global.kind == WaylandGlobalKind::Output && global.entered;
		}));
}

bool WaylandLifecycle::OutputEntered(uint32_t name) const noexcept {
	const auto found = std::find_if(globals.begin(), globals.end(),
		[name](const Global &global) { return global.name == name; });
	return found != globals.end() && found->kind == WaylandGlobalKind::Output && found->entered;
}

size_t WaylandLifecycle::SeatCount() const noexcept {
	return static_cast<size_t>(std::count_if(globals.begin(), globals.end(),
		[](const Global &global) { return global.kind == WaylandGlobalKind::Seat; }));
}

bool WaylandLifecycle::PointerActive() const noexcept {
	if (!activeSeatName) {
		return false;
	}
	const auto found = std::find_if(globals.begin(), globals.end(),
		[this](const Global &global) { return global.name == *activeSeatName; });
	return found != globals.end() && found->hasPointer;
}

bool WaylandLifecycle::KeyboardActive() const noexcept {
	if (!activeSeatName) {
		return false;
	}
	const auto found = std::find_if(globals.begin(), globals.end(),
		[this](const Global &global) { return global.name == *activeSeatName; });
	return found != globals.end() && found->hasKeyboard;
}

bool WaylandLifecycle::HasGlobal(uint32_t name) const noexcept {
	return std::any_of(globals.begin(), globals.end(),
		[name](const Global &global) { return global.name == name; });
}

}
