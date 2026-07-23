#include "WaylandPrimarySelection.h"

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

#include "primary-selection-client-protocol.h"
#include "WaylandEventLoop.h"

namespace Scalpel {

namespace {

PrimarySelectionResultStatus ResultStatus(WaylandTransferStatus status) noexcept {
	switch (status) {
	case WaylandTransferStatus::Complete:
		return PrimarySelectionResultStatus::Complete;
	case WaylandTransferStatus::Cancelled:
		return PrimarySelectionResultStatus::Cancelled;
	case WaylandTransferStatus::Failed:
		return PrimarySelectionResultStatus::Failed;
	case WaylandTransferStatus::TooLarge:
		return PrimarySelectionResultStatus::TooLarge;
	case WaylandTransferStatus::TimedOut:
		return PrimarySelectionResultStatus::TimedOut;
	}
	return PrimarySelectionResultStatus::Failed;
}

}

void WaylandPrimarySelectionState::SetManagerAvailable(bool available) {
	managerAvailable = available;
	if (!available) {
		SetSeatAvailable(false);
		ownsSelection = false;
		ownedText.clear();
	}
}

void WaylandPrimarySelectionState::SetSeatAvailable(bool available) {
	seatAvailable = available;
	if (!available) {
		serial.reset();
		offers.clear();
		selectionOffer.reset();
		ownsSelection = false;
		ownedText.clear();
	}
}

void WaylandPrimarySelectionState::RecordSerial(uint32_t serial_) noexcept {
	if (managerAvailable && seatAvailable) {
		serial = serial_;
	}
}

void WaylandPrimarySelectionState::AddOffer(uintptr_t token) {
	offers.try_emplace(token);
}

void WaylandPrimarySelectionState::OfferMime(
	uintptr_t token, std::string_view mimeType) {
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

void WaylandPrimarySelectionState::SelectOffer(std::optional<uintptr_t> token) {
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

void WaylandPrimarySelectionState::RemoveOffer(uintptr_t token) {
	offers.erase(token);
	if (selectionOffer == token) {
		selectionOffer.reset();
	}
}

bool WaylandPrimarySelectionState::Publish(std::optional<std::string> text) {
	if (!managerAvailable || !seatAvailable || !serial) {
		return false;
	}
	if (text) {
		selectionOffer.reset();
		ownedText = std::move(*text);
		ownsSelection = true;
	} else {
		ownedText.clear();
		ownsSelection = false;
	}
	return true;
}

void WaylandPrimarySelectionState::CancelOwnership() noexcept {
	ownsSelection = false;
	ownedText.clear();
}

WaylandPrimaryPasteChoice WaylandPrimarySelectionState::ChoosePaste() const {
	if (!managerAvailable || !seatAvailable) {
		return {WaylandPrimaryPasteChoice::Kind::Unavailable, {}, {}};
	}
	if (ownsSelection) {
		return {WaylandPrimaryPasteChoice::Kind::OwnedText, ownedText, {}};
	}
	if (!selectionOffer) {
		return {WaylandPrimaryPasteChoice::Kind::NoText, {}, {}};
	}
	const auto found = offers.find(*selectionOffer);
	if (found == offers.end() || found->second.rank == 0) {
		return {WaylandPrimaryPasteChoice::Kind::NoText, {}, {}};
	}
	return {WaylandPrimaryPasteChoice::Kind::Receive, {}, found->second.mimeType};
}

bool WaylandPrimarySelectionState::CanPaste() const {
	const WaylandPrimaryPasteChoice choice = ChoosePaste();
	return choice.kind == WaylandPrimaryPasteChoice::Kind::OwnedText ||
		choice.kind == WaylandPrimaryPasteChoice::Kind::Receive;
}

const zwp_primary_selection_offer_v1_listener
	WaylandPrimarySelection::offerListener = {
		WaylandPrimarySelection::OfferMime,
	};

const zwp_primary_selection_device_v1_listener
	WaylandPrimarySelection::deviceListener = {
		WaylandPrimarySelection::DeviceDataOffer,
		WaylandPrimarySelection::DeviceSelection,
	};

const zwp_primary_selection_source_v1_listener
	WaylandPrimarySelection::sourceListener = {
		WaylandPrimarySelection::SourceSend,
		WaylandPrimarySelection::SourceCancelled,
	};

WaylandPrimarySelection::WaylandPrimarySelection(NowFunction now_) :
	now(std::move(now_)) {
	if (!now) {
		throw std::invalid_argument("WaylandPrimarySelection requires a clock");
	}
}

WaylandPrimarySelection::~WaylandPrimarySelection() noexcept {
	SetManager(nullptr);
}

void WaylandPrimarySelection::SetManager(
	zwp_primary_selection_device_manager_v1 *manager_) {
	if (manager == manager_) {
		return;
	}
	DestroySource(true);
	DestroyDevice();
	manager = manager_;
	state.SetManagerAvailable(manager != nullptr);
}

void WaylandPrimarySelection::SetSeat(wl_seat *seat) {
	DestroySource(true);
	DestroyDevice();
	state.SetSeatAvailable(seat != nullptr);
	if (!manager || !seat) {
		return;
	}
	device = zwp_primary_selection_device_manager_v1_get_device(manager, seat);
	if (!device ||
		zwp_primary_selection_device_v1_add_listener(
			device, &deviceListener, this) != 0) {
		if (device) {
			zwp_primary_selection_device_v1_destroy(device);
			device = nullptr;
		}
		state.SetSeatAvailable(false);
	}
}

void WaylandPrimarySelection::RecordSerial(uint32_t serial) noexcept {
	state.RecordSerial(serial);
}

void WaylandPrimarySelection::PublishText(
	uint64_t request, std::optional<std::string> text) {
	if (!text) {
		if (!state.Publish(std::nullopt)) {
			Report(request, PrimarySelectionOperation::Publish,
				PrimarySelectionResultStatus::Unavailable);
			return;
		}
		if (source) {
			zwp_primary_selection_device_v1_set_selection(
				device, nullptr, *state.Serial());
			DestroySource(false);
		}
		Report(request, PrimarySelectionOperation::Publish,
			PrimarySelectionResultStatus::Published);
		return;
	}
	DestroySource(true);
	if (!state.Publish(std::move(text))) {
		Report(request, PrimarySelectionOperation::Publish,
			PrimarySelectionResultStatus::Unavailable);
		return;
	}
	source = zwp_primary_selection_device_manager_v1_create_source(manager);
	if (!source ||
		zwp_primary_selection_source_v1_add_listener(
			source, &sourceListener, this) != 0) {
		if (source) {
			zwp_primary_selection_source_v1_destroy(source);
			source = nullptr;
		}
		state.CancelOwnership();
		Report(request, PrimarySelectionOperation::Publish,
			PrimarySelectionResultStatus::Failed);
		return;
	}
	sourceRequest = request;
	zwp_primary_selection_source_v1_offer(source, WaylandTextMimeUtf8.data());
	zwp_primary_selection_source_v1_offer(
		source, WaylandTextMimeUtf8String.data());
	zwp_primary_selection_source_v1_offer(source, WaylandTextMimePlain.data());
	zwp_primary_selection_device_v1_set_selection(
		device, source, *state.Serial());
	Report(request, PrimarySelectionOperation::Publish,
		PrimarySelectionResultStatus::Published);
}

void WaylandPrimarySelection::PasteText(uint64_t request) {
	for (ActiveTransfer &active : transfers) {
		if (active.operation == PrimarySelectionOperation::Paste) {
			active.transfer.Cancel();
		}
	}
	CollectTransferResults();

	const WaylandPrimaryPasteChoice choice = state.ChoosePaste();
	switch (choice.kind) {
	case WaylandPrimaryPasteChoice::Kind::Unavailable:
		Report(request, PrimarySelectionOperation::Paste,
			PrimarySelectionResultStatus::Unavailable);
		return;
	case WaylandPrimaryPasteChoice::Kind::NoText:
		Report(request, PrimarySelectionOperation::Paste,
			PrimarySelectionResultStatus::NoText);
		return;
	case WaylandPrimaryPasteChoice::Kind::OwnedText:
		Report(request, PrimarySelectionOperation::Paste,
			PrimarySelectionResultStatus::Complete, choice.text);
		return;
	case WaylandPrimaryPasteChoice::Kind::Receive:
		break;
	}

	const std::optional<uintptr_t> selected = state.SelectionOffer();
	if (!selected) {
		Report(request, PrimarySelectionOperation::Paste,
			PrimarySelectionResultStatus::NoText);
		return;
	}
	auto *offer = reinterpret_cast<zwp_primary_selection_offer_v1 *>(*selected);
	if (offers.find(offer) == offers.end()) {
		Report(request, PrimarySelectionOperation::Paste,
			PrimarySelectionResultStatus::NoText);
		return;
	}
	int descriptors[2]{-1, -1};
	if (pipe2(descriptors, O_CLOEXEC) != 0) {
		Report(request, PrimarySelectionOperation::Paste,
			PrimarySelectionResultStatus::Failed);
		return;
	}
	try {
		transfers.push_back({request, PrimarySelectionOperation::Paste,
			WaylandTransfer::Read(descriptors[0],
				WaylandTransfer::DefaultMaximumBytes,
				WaylandTransfer::DefaultTimeout, now)});
	} catch (...) {
		(void)close(descriptors[1]);
		Report(request, PrimarySelectionOperation::Paste,
			PrimarySelectionResultStatus::Failed);
		return;
	}
	zwp_primary_selection_offer_v1_receive(
		offer, choice.mimeType.c_str(), descriptors[1]);
	(void)close(descriptors[1]);
}

void WaylandPrimarySelection::AddPollSources(WaylandEventLoop &eventLoop) {
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

void WaylandPrimarySelection::ProcessPollTimeouts() {
	for (ActiveTransfer &active : transfers) {
		active.transfer.CheckDeadline();
	}
	CollectTransferResults();
}

std::optional<std::chrono::milliseconds>
WaylandPrimarySelection::TimeUntilTransfer() const {
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

std::vector<PrimarySelectionResult> WaylandPrimarySelection::TakeResults() {
	return std::exchange(results, {});
}

void WaylandPrimarySelection::DestroyDevice() noexcept {
	CancelTransfers();
	for (auto &[offer, ignored] : offers) {
		(void)ignored;
		zwp_primary_selection_offer_v1_destroy(offer);
	}
	offers.clear();
	state.SetSeatAvailable(false);
	if (device) {
		zwp_primary_selection_device_v1_destroy(device);
		device = nullptr;
	}
}

void WaylandPrimarySelection::DestroySource(bool reportCancellation) noexcept {
	if (source) {
		CancelSourceTransfers(sourceRequest);
		zwp_primary_selection_source_v1_destroy(source);
		source = nullptr;
		if (reportCancellation) {
			Report(sourceRequest, PrimarySelectionOperation::Publish,
				PrimarySelectionResultStatus::Cancelled);
		}
	}
	sourceRequest = 0;
	state.CancelOwnership();
}

void WaylandPrimarySelection::CancelSourceTransfers(uint64_t request) noexcept {
	for (ActiveTransfer &active : transfers) {
		if (active.operation == PrimarySelectionOperation::Publish &&
			active.request == request) {
			active.transfer.Cancel();
			(void)active.transfer.TakeResult();
		}
	}
	transfers.erase(std::remove_if(transfers.begin(), transfers.end(),
		[request](const ActiveTransfer &active) {
			return active.operation == PrimarySelectionOperation::Publish &&
				active.request == request;
		}), transfers.end());
}

void WaylandPrimarySelection::CancelTransfers() noexcept {
	for (ActiveTransfer &active : transfers) {
		active.transfer.Cancel();
	}
	CollectTransferResults();
}

void WaylandPrimarySelection::CollectTransferResults() {
	for (ActiveTransfer &active : transfers) {
		std::optional<WaylandTransferResult> result = active.transfer.TakeResult();
		if (!result) {
			continue;
		}
		PrimarySelectionResultStatus status = ResultStatus(result->status);
		if (active.operation == PrimarySelectionOperation::Paste &&
			status == PrimarySelectionResultStatus::Complete &&
			!IsValidWaylandText(result->bytes)) {
			status = PrimarySelectionResultStatus::InvalidUtf8;
			result->bytes.clear();
		}
		Report(active.request, active.operation, status, std::move(result->bytes));
	}
	transfers.erase(std::remove_if(transfers.begin(), transfers.end(),
		[](const ActiveTransfer &active) {
			return !active.transfer.Pending();
		}), transfers.end());
}

void WaylandPrimarySelection::Report(uint64_t request,
	PrimarySelectionOperation operation, PrimarySelectionResultStatus status,
	std::string text) {
	results.push_back({request, operation, status, std::move(text)});
}

void WaylandPrimarySelection::OfferMime(
	void *data, zwp_primary_selection_offer_v1 *offer, const char *mimeType) {
	if (mimeType) {
		static_cast<WaylandPrimarySelection *>(data)->state.OfferMime(
			reinterpret_cast<uintptr_t>(offer), mimeType);
	}
}

void WaylandPrimarySelection::DeviceDataOffer(
	void *data, zwp_primary_selection_device_v1 *,
	zwp_primary_selection_offer_v1 *offer) {
	auto &selection = *static_cast<WaylandPrimarySelection *>(data);
	if (!offer ||
		zwp_primary_selection_offer_v1_add_listener(
			offer, &offerListener, &selection) != 0) {
		if (offer) {
			zwp_primary_selection_offer_v1_destroy(offer);
		}
		return;
	}
	selection.offers.emplace(offer, true);
	selection.state.AddOffer(reinterpret_cast<uintptr_t>(offer));
}

void WaylandPrimarySelection::DeviceSelection(
	void *data, zwp_primary_selection_device_v1 *,
	zwp_primary_selection_offer_v1 *offer) {
	auto &selection = *static_cast<WaylandPrimarySelection *>(data);
	for (auto iterator = selection.offers.begin();
		iterator != selection.offers.end();) {
		if (iterator->first != offer) {
			selection.state.RemoveOffer(
				reinterpret_cast<uintptr_t>(iterator->first));
			zwp_primary_selection_offer_v1_destroy(iterator->first);
			iterator = selection.offers.erase(iterator);
		} else {
			++iterator;
		}
	}
	selection.state.SelectOffer(offer ?
		std::optional<uintptr_t>{reinterpret_cast<uintptr_t>(offer)} :
		std::nullopt);
}

void WaylandPrimarySelection::SourceSend(
	void *data, zwp_primary_selection_source_v1 *source_,
	const char *mimeType, int32_t descriptor) {
	auto &selection = *static_cast<WaylandPrimarySelection *>(data);
	if (source_ != selection.source || !mimeType ||
		WaylandTextMimeRank(mimeType) == 0) {
		(void)close(descriptor);
		return;
	}
	try {
		selection.transfers.push_back({
			selection.sourceRequest, PrimarySelectionOperation::Publish,
			WaylandTransfer::Write(descriptor, selection.state.OwnedText(),
				WaylandTransfer::DefaultTimeout, selection.now)});
		selection.CollectTransferResults();
	} catch (...) {
		(void)close(descriptor);
		selection.Report(selection.sourceRequest,
			PrimarySelectionOperation::Publish,
			PrimarySelectionResultStatus::Failed);
	}
}

void WaylandPrimarySelection::SourceCancelled(
	void *data, zwp_primary_selection_source_v1 *source_) {
	auto &selection = *static_cast<WaylandPrimarySelection *>(data);
	if (source_ == selection.source) {
		zwp_primary_selection_source_v1_destroy(selection.source);
		selection.source = nullptr;
		selection.state.CancelOwnership();
		selection.Report(selection.sourceRequest,
			PrimarySelectionOperation::Publish,
			PrimarySelectionResultStatus::Cancelled);
		selection.sourceRequest = 0;
	}
}

}
