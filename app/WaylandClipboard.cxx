#include "WaylandClipboard.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>

#include "WaylandEventLoop.h"

namespace Scalpel {

namespace {

ClipboardResultStatus ResultStatus(WaylandTransferStatus status) noexcept {
	switch (status) {
	case WaylandTransferStatus::Complete:
		return ClipboardResultStatus::Complete;
	case WaylandTransferStatus::Cancelled:
		return ClipboardResultStatus::Cancelled;
	case WaylandTransferStatus::Failed:
		return ClipboardResultStatus::Failed;
	case WaylandTransferStatus::TooLarge:
		return ClipboardResultStatus::TooLarge;
	case WaylandTransferStatus::TimedOut:
		return ClipboardResultStatus::TimedOut;
	}
	return ClipboardResultStatus::Failed;
}

}

void WaylandClipboardState::SetManagerAvailable(bool available) {
	managerAvailable = available;
	if (!available) {
		SetSeatAvailable(false);
		ownsSelection = false;
		ownedText.clear();
	}
}

void WaylandClipboardState::SetSeatAvailable(bool available) {
	seatAvailable = available;
	if (!available) {
		serial.reset();
		offers.clear();
		selectionOffer.reset();
		ownsSelection = false;
		ownedText.clear();
	}
}

void WaylandClipboardState::RecordSerial(uint32_t serial_) noexcept {
	if (managerAvailable && seatAvailable) {
		serial = serial_;
	}
}

void WaylandClipboardState::AddOffer(uintptr_t token) {
	offers.try_emplace(token);
}

void WaylandClipboardState::OfferMime(uintptr_t token, std::string_view mimeType) {
	const auto found = offers.find(token);
	if (found == offers.end()) {
		return;
	}
	const int rank = WaylandTextMimeRank(mimeType);
	if (rank > found->second.rank) {
		found->second.mimeType = mimeType;
		found->second.rank = rank;
	}
}

void WaylandClipboardState::SelectOffer(std::optional<uintptr_t> token) {
	selectionOffer.reset();
	if (token && offers.find(*token) != offers.end()) {
		selectionOffer = token;
	}
	for (auto iterator = offers.begin(); iterator != offers.end();) {
		if (!selectionOffer || iterator->first != *selectionOffer) {
			iterator = offers.erase(iterator);
		} else {
			++iterator;
		}
	}
}

void WaylandClipboardState::RemoveOffer(uintptr_t token) {
	offers.erase(token);
	if (selectionOffer == token) {
		selectionOffer.reset();
	}
}

bool WaylandClipboardState::Publish(std::string text) {
	if (!managerAvailable || !seatAvailable || !serial) {
		return false;
	}
	ownedText = std::move(text);
	ownsSelection = true;
	selectionOffer.reset();
	return true;
}

void WaylandClipboardState::CancelOwnership() noexcept {
	ownsSelection = false;
	ownedText.clear();
}

WaylandClipboardPasteChoice WaylandClipboardState::ChoosePaste() const {
	if (!managerAvailable || !seatAvailable) {
		return {WaylandClipboardPasteChoice::Kind::Unavailable, {}, {}};
	}
	if (ownsSelection) {
		return {WaylandClipboardPasteChoice::Kind::OwnedText, ownedText, {}};
	}
	if (!selectionOffer) {
		return {WaylandClipboardPasteChoice::Kind::NoText, {}, {}};
	}
	const auto found = offers.find(*selectionOffer);
	if (found == offers.end() || found->second.rank == 0) {
		return {WaylandClipboardPasteChoice::Kind::NoText, {}, {}};
	}
	return {WaylandClipboardPasteChoice::Kind::Receive, {}, found->second.mimeType};
}

bool WaylandClipboardState::CanPaste() const {
	const WaylandClipboardPasteChoice choice = ChoosePaste();
	return choice.kind == WaylandClipboardPasteChoice::Kind::OwnedText ||
		choice.kind == WaylandClipboardPasteChoice::Kind::Receive;
}

const wl_data_offer_listener WaylandClipboard::dataOfferListener = {
	WaylandClipboard::DataOfferMime,
	WaylandClipboard::DataOfferSourceActions,
	WaylandClipboard::DataOfferAction,
};

const wl_data_device_listener WaylandClipboard::dataDeviceListener = {
	WaylandClipboard::DataDeviceOffer,
	WaylandClipboard::DataDeviceEnter,
	WaylandClipboard::DataDeviceLeave,
	WaylandClipboard::DataDeviceMotion,
	WaylandClipboard::DataDeviceDrop,
	WaylandClipboard::DataDeviceSelection,
};

const wl_data_source_listener WaylandClipboard::dataSourceListener = {
	WaylandClipboard::DataSourceTarget,
	WaylandClipboard::DataSourceSend,
	WaylandClipboard::DataSourceCancelled,
	WaylandClipboard::DataSourceDropPerformed,
	WaylandClipboard::DataSourceFinished,
	WaylandClipboard::DataSourceAction,
};

WaylandClipboard::WaylandClipboard(NowFunction now_) : now(std::move(now_)) {
	if (!now) {
		throw std::invalid_argument("WaylandClipboard requires a clock");
	}
}

WaylandClipboard::~WaylandClipboard() noexcept {
	SetManager(nullptr);
}

void WaylandClipboard::SetManager(wl_data_device_manager *manager_) {
	if (manager == manager_) {
		return;
	}
	DestroySource(true);
	DestroyDataDevice();
	DestroySource(false);
	manager = manager_;
	state.SetManagerAvailable(manager != nullptr);
}

void WaylandClipboard::SetSeat(wl_seat *seat) {
	DestroySource(true);
	DestroyDataDevice();
	state.SetSeatAvailable(seat != nullptr);
	if (!manager || !seat) {
		return;
	}
	device = wl_data_device_manager_get_data_device(manager, seat);
	if (!device || wl_data_device_add_listener(device, &dataDeviceListener, this) != 0) {
		if (device) {
			wl_data_device_destroy(device);
			device = nullptr;
		}
		state.SetSeatAvailable(false);
	}
}

void WaylandClipboard::RecordSerial(uint32_t serial) noexcept {
	state.RecordSerial(serial);
}

void WaylandClipboard::CopyText(uint64_t request, std::string text) {
	DestroySource(true);
	if (!state.Publish(std::move(text))) {
		Report(request, ClipboardOperation::Copy, ClipboardResultStatus::Unavailable);
		return;
	}
	source = wl_data_device_manager_create_data_source(manager);
	if (!source || wl_data_source_add_listener(source, &dataSourceListener, this) != 0) {
		if (source) {
			wl_data_source_destroy(source);
			source = nullptr;
		}
		state.CancelOwnership();
		Report(request, ClipboardOperation::Copy, ClipboardResultStatus::Failed);
		return;
	}
	sourceRequest = request;
	wl_data_source_offer(source, WaylandTextMimeUtf8.data());
	wl_data_source_offer(source, WaylandTextMimeUtf8String.data());
	wl_data_source_offer(source, WaylandTextMimePlain.data());
	wl_data_device_set_selection(device, source, *state.Serial());
	Report(request, ClipboardOperation::Copy, ClipboardResultStatus::Published);
}

void WaylandClipboard::PasteText(uint64_t request) {
	for (ActiveTransfer &active : transfers) {
		if (active.operation == ClipboardOperation::Paste) {
			active.transfer.Cancel();
		}
	}
	CollectTransferResults();

	const WaylandClipboardPasteChoice choice = state.ChoosePaste();
	switch (choice.kind) {
	case WaylandClipboardPasteChoice::Kind::Unavailable:
		Report(request, ClipboardOperation::Paste, ClipboardResultStatus::Unavailable);
		return;
	case WaylandClipboardPasteChoice::Kind::NoText:
		Report(request, ClipboardOperation::Paste, ClipboardResultStatus::NoText);
		return;
	case WaylandClipboardPasteChoice::Kind::OwnedText:
		Report(request, ClipboardOperation::Paste, ClipboardResultStatus::Complete,
			choice.text);
		return;
	case WaylandClipboardPasteChoice::Kind::Receive:
		break;
	}

	const std::optional<uintptr_t> selected = state.SelectionOffer();
	if (!selected) {
		Report(request, ClipboardOperation::Paste, ClipboardResultStatus::NoText);
		return;
	}
	wl_data_offer *offer = reinterpret_cast<wl_data_offer *>(*selected);
	if (offers.find(offer) == offers.end()) {
		Report(request, ClipboardOperation::Paste, ClipboardResultStatus::NoText);
		return;
	}
	int descriptors[2]{-1, -1};
	if (pipe2(descriptors, O_CLOEXEC) != 0) {
		Report(request, ClipboardOperation::Paste, ClipboardResultStatus::Failed);
		return;
	}
	try {
		transfers.push_back({request, ClipboardOperation::Paste,
			WaylandTransfer::Read(descriptors[0], WaylandTransfer::DefaultMaximumBytes,
				WaylandTransfer::DefaultTimeout, now)});
	} catch (...) {
		(void)close(descriptors[1]);
		Report(request, ClipboardOperation::Paste, ClipboardResultStatus::Failed);
		return;
	}
	wl_data_offer_receive(offer, choice.mimeType.c_str(), descriptors[1]);
	(void)close(descriptors[1]);
}

void WaylandClipboard::AddPollSources(WaylandEventLoop &eventLoop) {
	for (ActiveTransfer &active : transfers) {
		if (active.transfer.Pending()) {
			WaylandTransfer &transfer = active.transfer;
			const short ready =
				transfer.Direction() == WaylandTransferDirection::Read ?
					POLLIN : POLLOUT;
			eventLoop.AddSource(transfer.Descriptor(), ready,
				[&transfer, ready](short revents) {
					if (revents & (ready | POLLERR | POLLHUP | POLLNVAL)) {
						transfer.ProcessReady();
					}
				});
		}
	}
}

void WaylandClipboard::ProcessPollTimeouts() {
	for (ActiveTransfer &active : transfers) {
		active.transfer.CheckDeadline();
	}
	CollectTransferResults();
}

std::optional<std::chrono::milliseconds> WaylandClipboard::TimeUntilTransfer() const {
	std::optional<std::chrono::milliseconds> shortest;
	for (const ActiveTransfer &active : transfers) {
		const std::optional<std::chrono::milliseconds> remaining =
			active.transfer.TimeUntilDeadline();
		if (remaining && (!shortest || *remaining < *shortest)) {
			shortest = remaining;
		}
	}
	return shortest;
}

std::vector<ClipboardResult> WaylandClipboard::TakeResults() {
	return std::exchange(results, {});
}

void WaylandClipboard::DestroyDataDevice() noexcept {
	CancelTransfers();
	for (auto &[offer, ignored] : offers) {
		(void)ignored;
		wl_data_offer_destroy(offer);
	}
	offers.clear();
	state.SetSeatAvailable(false);
	if (device) {
		if (wl_data_device_get_version(device) >= WL_DATA_DEVICE_RELEASE_SINCE_VERSION) {
			wl_data_device_release(device);
		} else {
			wl_data_device_destroy(device);
		}
		device = nullptr;
	}
}

void WaylandClipboard::DestroySource(bool reportCancellation) noexcept {
	if (source) {
		CancelSourceTransfers(sourceRequest);
		wl_data_source_destroy(source);
		source = nullptr;
		if (reportCancellation) {
			Report(sourceRequest, ClipboardOperation::Copy, ClipboardResultStatus::Cancelled);
		}
	}
	sourceRequest = 0;
	state.CancelOwnership();
}

void WaylandClipboard::CancelSourceTransfers(uint64_t request) noexcept {
	for (ActiveTransfer &active : transfers) {
		if (active.operation == ClipboardOperation::Copy &&
			active.request == request) {
			active.transfer.Cancel();
			(void)active.transfer.TakeResult();
		}
	}
	transfers.erase(std::remove_if(transfers.begin(), transfers.end(),
		[request](const ActiveTransfer &active) {
			return active.operation == ClipboardOperation::Copy &&
				active.request == request;
		}), transfers.end());
}

void WaylandClipboard::DestroyOffer(wl_data_offer *offer) noexcept {
	if (!offer) {
		return;
	}
	const auto found = offers.find(offer);
	if (found != offers.end()) {
		state.RemoveOffer(reinterpret_cast<uintptr_t>(offer));
		wl_data_offer_destroy(offer);
		offers.erase(found);
	}
}

void WaylandClipboard::CancelTransfers() noexcept {
	for (ActiveTransfer &active : transfers) {
		active.transfer.Cancel();
	}
	CollectTransferResults();
}

void WaylandClipboard::CollectTransferResults() {
	for (ActiveTransfer &active : transfers) {
		std::optional<WaylandTransferResult> result = active.transfer.TakeResult();
		if (!result) {
			continue;
		}
		// Source writes only feed peers. Ownership outcomes are Published,
		// Cancelled, or the create-time Failed path—not write completion.
		if (active.operation == ClipboardOperation::Copy) {
			continue;
		}
		ClipboardResultStatus status = ResultStatus(result->status);
		if (status == ClipboardResultStatus::Complete &&
			!IsValidWaylandText(result->bytes)) {
			status = ClipboardResultStatus::InvalidUtf8;
			result->bytes.clear();
		}
		Report(active.request, active.operation, status, std::move(result->bytes));
	}
	transfers.erase(std::remove_if(transfers.begin(), transfers.end(),
		[](const ActiveTransfer &active) {
			return !active.transfer.Pending();
		}), transfers.end());
}

void WaylandClipboard::Report(uint64_t request, ClipboardOperation operation,
	ClipboardResultStatus status, std::string text) {
	results.push_back({request, operation, status, std::move(text)});
}

void WaylandClipboard::DataOfferMime(void *data, wl_data_offer *offer,
	const char *mimeType) {
	if (mimeType) {
		static_cast<WaylandClipboard *>(data)->state.OfferMime(
			reinterpret_cast<uintptr_t>(offer), mimeType);
	}
}

void WaylandClipboard::DataOfferSourceActions(void *, wl_data_offer *, uint32_t) {
}

void WaylandClipboard::DataOfferAction(void *, wl_data_offer *, uint32_t) {
}

void WaylandClipboard::DataDeviceOffer(void *data, wl_data_device *,
	wl_data_offer *offer) {
	auto &clipboard = *static_cast<WaylandClipboard *>(data);
	if (!offer || wl_data_offer_add_listener(offer, &dataOfferListener, &clipboard) != 0) {
		if (offer) {
			wl_data_offer_destroy(offer);
		}
		return;
	}
	clipboard.offers.emplace(offer, true);
	clipboard.state.AddOffer(reinterpret_cast<uintptr_t>(offer));
}

void WaylandClipboard::DataDeviceEnter(void *data, wl_data_device *, uint32_t,
	wl_surface *, int32_t, int32_t, wl_data_offer *offer) {
	static_cast<WaylandClipboard *>(data)->DestroyOffer(offer);
}

void WaylandClipboard::DataDeviceLeave(void *, wl_data_device *) {
}

void WaylandClipboard::DataDeviceMotion(void *, wl_data_device *, uint32_t,
	int32_t, int32_t) {
}

void WaylandClipboard::DataDeviceDrop(void *, wl_data_device *) {
}

void WaylandClipboard::DataDeviceSelection(void *data, wl_data_device *,
	wl_data_offer *offer) {
	auto &clipboard = *static_cast<WaylandClipboard *>(data);
	for (auto iterator = clipboard.offers.begin(); iterator != clipboard.offers.end();) {
		if (iterator->first != offer) {
			clipboard.state.RemoveOffer(reinterpret_cast<uintptr_t>(iterator->first));
			wl_data_offer_destroy(iterator->first);
			iterator = clipboard.offers.erase(iterator);
		} else {
			++iterator;
		}
	}
	clipboard.state.SelectOffer(offer ?
		std::optional<uintptr_t>{reinterpret_cast<uintptr_t>(offer)} : std::nullopt);
}

void WaylandClipboard::DataSourceTarget(void *, wl_data_source *, const char *) {
}

void WaylandClipboard::DataSourceSend(void *data, wl_data_source *source_,
	const char *mimeType, int32_t descriptor) {
	auto &clipboard = *static_cast<WaylandClipboard *>(data);
	if (source_ != clipboard.source || !mimeType ||
		WaylandTextMimeRank(mimeType) == 0) {
		(void)close(descriptor);
		return;
	}
	try {
		clipboard.transfers.push_back({clipboard.sourceRequest, ClipboardOperation::Copy,
			WaylandTransfer::Write(descriptor, clipboard.state.OwnedText(),
				WaylandTransfer::DefaultTimeout, clipboard.now)});
		clipboard.CollectTransferResults();
	} catch (...) {
		(void)close(descriptor);
	}
}

void WaylandClipboard::DataSourceCancelled(void *data, wl_data_source *source_) {
	auto &clipboard = *static_cast<WaylandClipboard *>(data);
	if (source_ == clipboard.source) {
		const uint64_t request = clipboard.sourceRequest;
		clipboard.CancelSourceTransfers(request);
		wl_data_source_destroy(clipboard.source);
		clipboard.source = nullptr;
		clipboard.sourceRequest = 0;
		clipboard.state.CancelOwnership();
		clipboard.Report(request, ClipboardOperation::Copy,
			ClipboardResultStatus::Cancelled);
	}
}

void WaylandClipboard::DataSourceDropPerformed(void *, wl_data_source *) {
}

void WaylandClipboard::DataSourceFinished(void *, wl_data_source *) {
}

void WaylandClipboard::DataSourceAction(void *, wl_data_source *, uint32_t) {
}

}
