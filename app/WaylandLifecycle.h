// Compositor-driven state for the main Wayland window.

#ifndef WAYLANDLIFECYCLE_H
#define WAYLANDLIFECYCLE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Scalpel {

struct WindowSize {
	int width = 0;
	int height = 0;

	friend constexpr bool operator==(const WindowSize &left, const WindowSize &right) noexcept {
		return left.width == right.width && left.height == right.height;
	}
	friend constexpr bool operator!=(const WindowSize &left, const WindowSize &right) noexcept {
		return !(left == right);
	}
};

enum class WaylandGlobalKind {
	Compositor,
	WmBase,
	DecorationManager,
	SharedMemory,
	Output,
	Seat,
};

enum class WaylandLifecycleActionType {
	BindCompositor,
	BindWmBase,
	BindDecorationManager,
	ReleaseDecorationManager,
	BindSharedMemory,
	ReleaseSharedMemory,
	BindOutput,
	ReleaseOutput,
	BindSeat,
	ReleaseSeat,
	CreatePointer,
	ReleasePointer,
	CreateKeyboard,
	ReleaseKeyboard,
	Close,
};

struct WaylandToplevelState {
	std::optional<WindowSize> configureBounds;
	bool maximized = false;
	bool fullscreen = false;
	bool resizing = false;
	bool activated = false;
	bool windowMenuAvailable = false;
	bool maximizeAvailable = false;
	bool fullscreenAvailable = false;
	bool minimizeAvailable = false;
	bool serverSideDecoration = false;
};

struct WaylandLifecycleAction {
	WaylandLifecycleActionType type;
	uint32_t name = 0;
	uint32_t version = 0;

	friend constexpr bool operator==(const WaylandLifecycleAction &left,
		const WaylandLifecycleAction &right) noexcept {
		return left.type == right.type && left.name == right.name &&
			left.version == right.version;
	}
};

/** Coalesces protocol callbacks into changes consumed by the application loop. */
class WaylandLifecycle final {
public:
	WaylandLifecycle(int width, int height);

	[[nodiscard]] std::vector<WaylandLifecycleAction> AddGlobal(
		WaylandGlobalKind kind, uint32_t name, uint32_t version);
	[[nodiscard]] std::vector<WaylandLifecycleAction> RemoveGlobal(uint32_t name);
	[[nodiscard]] std::vector<WaylandLifecycleAction> UpdateSeatCapabilities(
		uint32_t name, bool hasPointer, bool hasKeyboard);
	void EnterOutput(uint32_t name) noexcept;
	void LeaveOutput(uint32_t name) noexcept;

	void ProposeSize(int width, int height) noexcept;
	void ProposeToplevel(int width, int height,
		const std::vector<uint32_t> &states) noexcept;
	void ProposeConfigureBounds(int width, int height) noexcept;
	void ProposeWmCapabilities(const std::vector<uint32_t> &capabilities) noexcept;
	void ProposeDecoration(bool serverSide) noexcept;
	[[nodiscard]] std::optional<WindowSize> CommitConfigure() noexcept;
	void RequestClose() noexcept;

	[[nodiscard]] std::optional<WindowSize> TakeResize() noexcept;
	[[nodiscard]] int Width() const noexcept { return currentSize.width; }
	[[nodiscard]] int Height() const noexcept { return currentSize.height; }
	[[nodiscard]] bool CloseRequested() const noexcept { return closeRequested; }
	[[nodiscard]] const WaylandToplevelState &ToplevelState() const noexcept {
		return toplevelState;
	}
	[[nodiscard]] size_t OutputCount() const noexcept;
	[[nodiscard]] size_t EnteredOutputCount() const noexcept;
	[[nodiscard]] bool OutputEntered(uint32_t name) const noexcept;
	[[nodiscard]] size_t SeatCount() const noexcept;
	[[nodiscard]] std::optional<uint32_t> ActiveSeat() const noexcept { return activeSeatName; }
	[[nodiscard]] bool PointerActive() const noexcept;
	[[nodiscard]] bool KeyboardActive() const noexcept;

private:
	struct Global {
		WaylandGlobalKind kind;
		uint32_t name;
		uint32_t version;
		bool entered = false;
		bool hasPointer = false;
		bool hasKeyboard = false;
	};

	[[nodiscard]] bool HasGlobal(uint32_t name) const noexcept;

	WindowSize currentSize;
	std::optional<WindowSize> proposedSize;
	std::optional<WindowSize> pendingResize;
	std::vector<Global> globals;
	std::optional<uint32_t> compositorName;
	std::optional<uint32_t> wmBaseName;
	std::optional<uint32_t> decorationManagerName;
	std::optional<uint32_t> sharedMemoryName;
	std::optional<uint32_t> activeSeatName;
	WaylandToplevelState toplevelState;
	std::optional<WaylandToplevelState> proposedToplevelState;
	bool closeRequested = false;
};

}

#endif
