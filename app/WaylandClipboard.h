// Wayland clipboard offers, ownership, and asynchronous transfers.

#ifndef WAYLANDCLIPBOARD_H
#define WAYLANDCLIPBOARD_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "WaylandTransfer.h"

struct pollfd;
struct wl_data_device;
struct wl_data_device_listener;
struct wl_data_device_manager;
struct wl_data_offer;
struct wl_data_offer_listener;
struct wl_data_source;
struct wl_data_source_listener;
struct wl_seat;
struct wl_surface;

namespace Scalpel {

inline constexpr std::string_view ClipboardMimeUtf8 = "text/plain;charset=utf-8";
inline constexpr std::string_view ClipboardMimeUtf8String = "UTF8_STRING";
inline constexpr std::string_view ClipboardMimePlain = "text/plain";

[[nodiscard]] int ClipboardMimeRank(std::string_view mimeType) noexcept;
[[nodiscard]] bool IsValidClipboardUtf8(std::string_view text) noexcept;

struct WaylandClipboardPasteChoice {
	enum class Kind {
		Unavailable,
		NoText,
		OwnedText,
		Receive,
	};

	Kind kind = Kind::Unavailable;
	std::string text;
	std::string mimeType;
};

/** Plain selection state driven by the Wayland clipboard listener. */
class WaylandClipboardState final {
public:
	void SetManagerAvailable(bool available);
	void SetSeatAvailable(bool available);
	void RecordSerial(uint32_t serial) noexcept;
	void AddOffer(uintptr_t token);
	void OfferMime(uintptr_t token, std::string_view mimeType);
	void SelectOffer(std::optional<uintptr_t> token);
	void RemoveOffer(uintptr_t token);

	[[nodiscard]] bool Publish(std::string text);
	void CancelOwnership() noexcept;
	[[nodiscard]] WaylandClipboardPasteChoice ChoosePaste() const;
	[[nodiscard]] bool CanPaste() const;
	[[nodiscard]] std::optional<uint32_t> Serial() const noexcept { return serial; }
	[[nodiscard]] const std::string &OwnedText() const noexcept { return ownedText; }
	[[nodiscard]] std::optional<uintptr_t> SelectionOffer() const noexcept {
		return selectionOffer;
	}

private:
	struct Offer {
		std::string mimeType;
		int rank = 0;
	};

	bool managerAvailable = false;
	bool seatAvailable = false;
	std::optional<uint32_t> serial;
	std::unordered_map<uintptr_t, Offer> offers;
	std::optional<uintptr_t> selectionOffer;
	std::string ownedText;
	bool ownsSelection = false;
};

enum class ClipboardOperation {
	Copy,
	Paste,
};

enum class ClipboardResultStatus {
	Published,
	Complete,
	Unavailable,
	NoText,
	InvalidUtf8,
	Cancelled,
	Failed,
	TooLarge,
	TimedOut,
};

struct ClipboardResult {
	uint64_t request = 0;
	ClipboardOperation operation = ClipboardOperation::Copy;
	ClipboardResultStatus status = ClipboardResultStatus::Failed;
	std::string text;
};

/**
 * Owns the active seat's core Wayland clipboard objects.
 *
 * The window binds the manager and seat, forwards input serials, and includes
 * the exposed transfer descriptors and deadline in its one poll operation.
 */
class WaylandClipboard final {
public:
	using Clock = WaylandTransfer::Clock;
	using NowFunction = WaylandTransfer::NowFunction;

	explicit WaylandClipboard(NowFunction now = Clock::now);
	~WaylandClipboard() noexcept;

	WaylandClipboard(const WaylandClipboard &) = delete;
	WaylandClipboard &operator=(const WaylandClipboard &) = delete;

	void SetManager(wl_data_device_manager *manager);
	void SetSeat(wl_seat *seat);
	void RecordSerial(uint32_t serial) noexcept;
	void CopyText(uint64_t request, std::string text);
	void PasteText(uint64_t request);
	[[nodiscard]] bool CanPaste() const { return state.CanPaste(); }

	void AppendPollDescriptors(std::vector<pollfd> &descriptors) const;
	void ProcessPollDescriptors(const std::vector<pollfd> &descriptors,
		std::size_t firstDescriptor);
	[[nodiscard]] std::optional<std::chrono::milliseconds> TimeUntilTransfer() const;
	[[nodiscard]] std::vector<ClipboardResult> TakeResults();

private:
	struct ActiveTransfer {
		uint64_t request = 0;
		ClipboardOperation operation = ClipboardOperation::Copy;
		WaylandTransfer transfer;
	};

	void DestroyDataDevice() noexcept;
	void DestroySource(bool reportCancellation) noexcept;
	void DestroyOffer(wl_data_offer *offer) noexcept;
	void CancelTransfers() noexcept;
	void CollectTransferResults();
	void Report(uint64_t request, ClipboardOperation operation,
		ClipboardResultStatus status, std::string text = {});

	static void DataOfferMime(void *data, wl_data_offer *offer, const char *mimeType);
	static void DataOfferSourceActions(void *data, wl_data_offer *offer, uint32_t actions);
	static void DataOfferAction(void *data, wl_data_offer *offer, uint32_t action);
	static void DataDeviceOffer(void *data, wl_data_device *device, wl_data_offer *offer);
	static void DataDeviceEnter(void *data, wl_data_device *device, uint32_t serial,
		wl_surface *surface, int32_t x, int32_t y, wl_data_offer *offer);
	static void DataDeviceLeave(void *data, wl_data_device *device);
	static void DataDeviceMotion(void *data, wl_data_device *device,
		uint32_t time, int32_t x, int32_t y);
	static void DataDeviceDrop(void *data, wl_data_device *device);
	static void DataDeviceSelection(void *data, wl_data_device *device,
		wl_data_offer *offer);
	static void DataSourceTarget(void *data, wl_data_source *source, const char *mimeType);
	static void DataSourceSend(void *data, wl_data_source *source,
		const char *mimeType, int32_t descriptor);
	static void DataSourceCancelled(void *data, wl_data_source *source);
	static void DataSourceDropPerformed(void *data, wl_data_source *source);
	static void DataSourceFinished(void *data, wl_data_source *source);
	static void DataSourceAction(void *data, wl_data_source *source, uint32_t action);

	static const wl_data_offer_listener dataOfferListener;
	static const wl_data_device_listener dataDeviceListener;
	static const wl_data_source_listener dataSourceListener;

	WaylandClipboardState state;
	wl_data_device_manager *manager = nullptr;
	wl_data_device *device = nullptr;
	wl_data_source *source = nullptr;
	uint64_t sourceRequest = 0;
	std::unordered_map<wl_data_offer *, bool> offers;
	std::vector<ActiveTransfer> transfers;
	std::vector<ClipboardResult> results;
	NowFunction now;
};

}

#endif
