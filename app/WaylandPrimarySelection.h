// Wayland primary-selection offers, ownership, and asynchronous transfers.

#ifndef WAYLANDPRIMARYSELECTION_H
#define WAYLANDPRIMARYSELECTION_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "WaylandTransfer.h"

struct wl_seat;
struct zwp_primary_selection_device_manager_v1;
struct zwp_primary_selection_device_v1;
struct zwp_primary_selection_device_v1_listener;
struct zwp_primary_selection_offer_v1;
struct zwp_primary_selection_offer_v1_listener;
struct zwp_primary_selection_source_v1;
struct zwp_primary_selection_source_v1_listener;

namespace Scalpel {

class WaylandEventLoop;

struct WaylandPrimaryPasteChoice {
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

/** Plain primary-selection state driven by the protocol listener. */
class WaylandPrimarySelectionState final {
public:
	void SetManagerAvailable(bool available);
	void SetSeatAvailable(bool available);
	void RecordSerial(uint32_t serial) noexcept;
	void AddOffer(uintptr_t token);
	void OfferMime(uintptr_t token, std::string_view mimeType);
	void SelectOffer(std::optional<uintptr_t> token);
	void RemoveOffer(uintptr_t token);

	[[nodiscard]] bool Publish(std::optional<std::string> text);
	void CancelOwnership() noexcept;
	[[nodiscard]] WaylandPrimaryPasteChoice ChoosePaste() const;
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

enum class PrimarySelectionOperation {
	Publish,
	Paste,
};

enum class PrimarySelectionResultStatus {
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

struct PrimarySelectionResult {
	uint64_t request = 0;
	PrimarySelectionOperation operation = PrimarySelectionOperation::Publish;
	PrimarySelectionResultStatus status = PrimarySelectionResultStatus::Failed;
	std::string text;
};

/** Owns the active seat's optional primary-selection protocol objects. */
class WaylandPrimarySelection final {
public:
	using Clock = WaylandTransfer::Clock;
	using NowFunction = WaylandTransfer::NowFunction;

	explicit WaylandPrimarySelection(NowFunction now = Clock::now);
	~WaylandPrimarySelection() noexcept;

	WaylandPrimarySelection(const WaylandPrimarySelection &) = delete;
	WaylandPrimarySelection &operator=(const WaylandPrimarySelection &) = delete;

	void SetManager(zwp_primary_selection_device_manager_v1 *manager);
	void SetSeat(wl_seat *seat);
	void RecordSerial(uint32_t serial) noexcept;
	void PublishText(uint64_t request, std::optional<std::string> text);
	void PasteText(uint64_t request);
	[[nodiscard]] bool CanPaste() const { return state.CanPaste(); }

	void AddPollSources(WaylandEventLoop &eventLoop);
	void ProcessPollTimeouts();
	[[nodiscard]] std::optional<std::chrono::milliseconds> TimeUntilTransfer() const;
	[[nodiscard]] std::vector<PrimarySelectionResult> TakeResults();

private:
	struct ActiveTransfer {
		uint64_t request = 0;
		PrimarySelectionOperation operation = PrimarySelectionOperation::Publish;
		WaylandTransfer transfer;
	};

	void DestroyDevice() noexcept;
	void DestroySource(bool reportCancellation) noexcept;
	void CancelSourceTransfers(uint64_t request) noexcept;
	void CancelTransfers() noexcept;
	void CollectTransferResults();
	void Report(uint64_t request, PrimarySelectionOperation operation,
		PrimarySelectionResultStatus status, std::string text = {});

	static void OfferMime(void *data, zwp_primary_selection_offer_v1 *offer,
		const char *mimeType);
	static void DeviceDataOffer(void *data,
		zwp_primary_selection_device_v1 *device,
		zwp_primary_selection_offer_v1 *offer);
	static void DeviceSelection(void *data,
		zwp_primary_selection_device_v1 *device,
		zwp_primary_selection_offer_v1 *offer);
	static void SourceSend(void *data, zwp_primary_selection_source_v1 *source,
		const char *mimeType, int32_t descriptor);
	static void SourceCancelled(void *data,
		zwp_primary_selection_source_v1 *source);

	static const zwp_primary_selection_offer_v1_listener offerListener;
	static const zwp_primary_selection_device_v1_listener deviceListener;
	static const zwp_primary_selection_source_v1_listener sourceListener;

	WaylandPrimarySelectionState state;
	zwp_primary_selection_device_manager_v1 *manager = nullptr;
	zwp_primary_selection_device_v1 *device = nullptr;
	zwp_primary_selection_source_v1 *source = nullptr;
	uint64_t sourceRequest = 0;
	std::unordered_map<zwp_primary_selection_offer_v1 *, bool> offers;
	std::vector<ActiveTransfer> transfers;
	std::vector<PrimarySelectionResult> results;
	NowFunction now;
};

}

#endif
