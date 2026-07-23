#include "WaylandLifecycle.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Scalpel {

namespace {

constexpr uint32_t ToplevelMaximized = 1; // XDG_TOPLEVEL_STATE_MAXIMIZED
constexpr uint32_t ToplevelFullscreen = 2; // XDG_TOPLEVEL_STATE_FULLSCREEN
constexpr uint32_t ToplevelResizing = 3; // XDG_TOPLEVEL_STATE_RESIZING
constexpr uint32_t ToplevelActivated = 4; // XDG_TOPLEVEL_STATE_ACTIVATED

constexpr uint32_t WindowMenuCapability = 1; // XDG_TOPLEVEL_WM_CAPABILITIES_WINDOW_MENU
constexpr uint32_t MaximizeCapability = 2; // XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE
constexpr uint32_t FullscreenCapability = 3; // XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN
constexpr uint32_t MinimizeCapability = 4; // XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE

}

WaylandLifecycle::WaylandLifecycle(int width, int height) : currentSize{width, height} {
	if (width <= 0 || height <= 0) {
		throw std::invalid_argument("WaylandLifecycle requires a positive size");
	}
}

std::vector<WaylandLifecycleAction> WaylandLifecycle::AddGlobal(
	WaylandGlobalKind kind, uint32_t name, uint32_t version) {
	if (closeRequested || HasGlobal(name)) {
		return {};
	}
	globals.push_back(Global{kind, name, version});
	switch (kind) {
	case WaylandGlobalKind::Compositor:
		if (!compositorName) {
			compositorName = name;
			return {{WaylandLifecycleActionType::BindCompositor, name, version}};
		}
		break;
	case WaylandGlobalKind::WmBase:
		if (!wmBaseName) {
			wmBaseName = name;
			return {{WaylandLifecycleActionType::BindWmBase, name, version}};
		}
		break;
	case WaylandGlobalKind::DecorationManager:
		if (!decorationManagerName) {
			decorationManagerName = name;
			return {{WaylandLifecycleActionType::BindDecorationManager, name, version}};
		}
		break;
	case WaylandGlobalKind::SharedMemory:
		if (!sharedMemoryName) {
			sharedMemoryName = name;
			return {{WaylandLifecycleActionType::BindSharedMemory, name, version}};
		}
		break;
	case WaylandGlobalKind::DataDeviceManager:
		if (!dataDeviceManagerName) {
			dataDeviceManagerName = name;
			return {{WaylandLifecycleActionType::BindDataDeviceManager, name, version}};
		}
		break;
	case WaylandGlobalKind::PrimarySelectionManager:
		if (!primarySelectionManagerName) {
			primarySelectionManagerName = name;
			return {{WaylandLifecycleActionType::BindPrimarySelectionManager, name, version}};
		}
		break;
	case WaylandGlobalKind::TextInputManager:
		if (!textInputManagerName) {
			textInputManagerName = name;
			return {{WaylandLifecycleActionType::BindTextInputManager, name, version}};
		}
		break;
	case WaylandGlobalKind::Presentation:
		if (!presentationName) {
			presentationName = name;
			return {{WaylandLifecycleActionType::BindPresentation, name, version}};
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
	if (removed.kind == WaylandGlobalKind::DecorationManager &&
		decorationManagerName == name) {
		std::vector<WaylandLifecycleAction> actions = {
			{WaylandLifecycleActionType::ReleaseDecorationManager, name}};
		decorationManagerName.reset();
		const auto replacement = std::find_if(globals.begin(), globals.end(),
			[](const Global &global) {
				return global.kind == WaylandGlobalKind::DecorationManager;
			});
		if (replacement != globals.end()) {
			decorationManagerName = replacement->name;
			actions.push_back({WaylandLifecycleActionType::BindDecorationManager,
				replacement->name, replacement->version});
		}
		return actions;
	}
	if (removed.kind == WaylandGlobalKind::SharedMemory && sharedMemoryName == name) {
		std::vector<WaylandLifecycleAction> actions = {
			{WaylandLifecycleActionType::ReleaseSharedMemory, name}};
		sharedMemoryName.reset();
		const auto replacement = std::find_if(globals.begin(), globals.end(),
			[](const Global &global) {
				return global.kind == WaylandGlobalKind::SharedMemory;
			});
		if (replacement != globals.end()) {
			sharedMemoryName = replacement->name;
			actions.push_back({WaylandLifecycleActionType::BindSharedMemory,
				replacement->name, replacement->version});
		}
		return actions;
	}
	if (removed.kind == WaylandGlobalKind::DataDeviceManager &&
		dataDeviceManagerName == name) {
		std::vector<WaylandLifecycleAction> actions = {
			{WaylandLifecycleActionType::ReleaseDataDeviceManager, name}};
		dataDeviceManagerName.reset();
		const auto replacement = std::find_if(globals.begin(), globals.end(),
			[](const Global &global) {
				return global.kind == WaylandGlobalKind::DataDeviceManager;
			});
		if (replacement != globals.end()) {
			dataDeviceManagerName = replacement->name;
			actions.push_back({WaylandLifecycleActionType::BindDataDeviceManager,
				replacement->name, replacement->version});
		}
		return actions;
	}
	if (removed.kind == WaylandGlobalKind::PrimarySelectionManager &&
		primarySelectionManagerName == name) {
		std::vector<WaylandLifecycleAction> actions = {
			{WaylandLifecycleActionType::ReleasePrimarySelectionManager, name}};
		primarySelectionManagerName.reset();
		const auto replacement = std::find_if(globals.begin(), globals.end(),
			[](const Global &global) {
				return global.kind == WaylandGlobalKind::PrimarySelectionManager;
			});
		if (replacement != globals.end()) {
			primarySelectionManagerName = replacement->name;
			actions.push_back({WaylandLifecycleActionType::BindPrimarySelectionManager,
				replacement->name, replacement->version});
		}
		return actions;
	}
	if (removed.kind == WaylandGlobalKind::TextInputManager &&
		textInputManagerName == name) {
		std::vector<WaylandLifecycleAction> actions = {
			{WaylandLifecycleActionType::ReleaseTextInputManager, name}};
		textInputManagerName.reset();
		const auto replacement = std::find_if(globals.begin(), globals.end(),
			[](const Global &global) {
				return global.kind == WaylandGlobalKind::TextInputManager;
			});
		if (replacement != globals.end()) {
			textInputManagerName = replacement->name;
			actions.push_back({WaylandLifecycleActionType::BindTextInputManager,
				replacement->name, replacement->version});
		}
		return actions;
	}
	if (removed.kind == WaylandGlobalKind::Presentation &&
		presentationName == name) {
		std::vector<WaylandLifecycleAction> actions = {
			{WaylandLifecycleActionType::ReleasePresentation, name}};
		presentationName.reset();
		const auto replacement = std::find_if(globals.begin(), globals.end(),
			[](const Global &global) {
				return global.kind == WaylandGlobalKind::Presentation;
			});
		if (replacement != globals.end()) {
			presentationName = replacement->name;
			actions.push_back({WaylandLifecycleActionType::BindPresentation,
				replacement->name, replacement->version});
		}
		return actions;
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

void WaylandLifecycle::ProposeToplevel(int width, int height,
	const std::vector<uint32_t> &states) noexcept {
	if (width > 0 && height > 0) {
		proposedSize = WindowSize{width, height};
	} else {
		proposedSize.reset();
	}
	WaylandToplevelState proposed = proposedToplevelState.value_or(toplevelState);
	proposed.maximized = false;
	proposed.fullscreen = false;
	proposed.resizing = false;
	proposed.activated = false;
	for (const uint32_t state : states) {
		switch (state) {
		case ToplevelMaximized:
			proposed.maximized = true;
			break;
		case ToplevelFullscreen:
			proposed.fullscreen = true;
			break;
		case ToplevelResizing:
			proposed.resizing = true;
			break;
		case ToplevelActivated:
			proposed.activated = true;
			break;
		default:
			break;
		}
	}
	proposedToplevelState = proposed;
}

void WaylandLifecycle::ProposeConfigureBounds(int width, int height) noexcept {
	WaylandToplevelState proposed = proposedToplevelState.value_or(toplevelState);
	proposed.configureBounds = width > 0 && height > 0 ?
		std::optional<WindowSize>{WindowSize{width, height}} : std::nullopt;
	proposedToplevelState = proposed;
}

void WaylandLifecycle::ProposeWmCapabilities(
	const std::vector<uint32_t> &capabilities) noexcept {
	WaylandToplevelState proposed = proposedToplevelState.value_or(toplevelState);
	proposed.windowMenuAvailable = false;
	proposed.maximizeAvailable = false;
	proposed.fullscreenAvailable = false;
	proposed.minimizeAvailable = false;
	for (const uint32_t capability : capabilities) {
		switch (capability) {
		case WindowMenuCapability:
			proposed.windowMenuAvailable = true;
			break;
		case MaximizeCapability:
			proposed.maximizeAvailable = true;
			break;
		case FullscreenCapability:
			proposed.fullscreenAvailable = true;
			break;
		case MinimizeCapability:
			proposed.minimizeAvailable = true;
			break;
		default:
			break;
		}
	}
	proposedToplevelState = proposed;
}

void WaylandLifecycle::ProposeDecoration(bool serverSide) noexcept {
	WaylandToplevelState proposed = proposedToplevelState.value_or(toplevelState);
	proposed.serverSideDecoration = serverSide;
	proposedToplevelState = proposed;
}

std::optional<WindowSize> WaylandLifecycle::CommitConfigure() noexcept {
	std::optional<WindowSize> committed;
	if (proposedSize && *proposedSize != currentSize) {
		currentSize = *proposedSize;
		pendingResize = currentSize;
		committed = currentSize;
	}
	proposedSize.reset();
	if (proposedToplevelState) {
		toplevelState = *proposedToplevelState;
		proposedToplevelState.reset();
	}
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
