#include "WaylandClipboard.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>

namespace Scalpel {

namespace {

std::string LowerMime(std::string_view mimeType) {
	std::string lowered(mimeType);
	std::transform(lowered.begin(), lowered.end(), lowered.begin(),
		[](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
	return lowered;
}

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

int ClipboardMimeRank(std::string_view mimeType) noexcept {
	const std::string lowered = LowerMime(mimeType);
	if (lowered == ClipboardMimeUtf8) {
		return 3;
	}
	if (lowered == "utf8_string") {
		return 2;
	}
	if (lowered == ClipboardMimePlain) {
		return 1;
	}
	return 0;
}

bool IsValidClipboardUtf8(std::string_view text) noexcept {
	const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());
	std::size_t offset = 0;
	while (offset < text.size()) {
		const unsigned char lead = bytes[offset];
		std::size_t length = 0;
		uint32_t codePoint = 0;
		if (lead <= 0x7f) {
			length = 1;
			codePoint = lead;
		} else if (lead >= 0xc2 && lead <= 0xdf) {
			length = 2;
			codePoint = lead & 0x1f;
		} else if (lead >= 0xe0 && lead <= 0xef) {
			length = 3;
			codePoint = lead & 0x0f;
		} else if (lead >= 0xf0 && lead <= 0xf4) {
			length = 4;
			codePoint = lead & 0x07;
		} else {
			return false;
		}
		if (offset + length > text.size()) {
			return false;
		}
		for (std::size_t index = 1; index < length; ++index) {
			const unsigned char continuation = bytes[offset + index];
			if ((continuation & 0xc0) != 0x80) {
				return false;
			}
			codePoint = (codePoint << 6) | (continuation & 0x3f);
		}
		if ((length == 3 && codePoint < 0x800) ||
			(length == 4 && codePoint < 0x10000) ||
			(codePoint >= 0xd800 && codePoint <= 0xdfff) ||
			codePoint > 0x10ffff) {
			return false;
		}
		offset += length;
	}
	return true;
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
	const int rank = ClipboardMimeRank(mimeType);
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
	wl_data_source_offer(source, ClipboardMimeUtf8.data());
	wl_data_source_offer(source, ClipboardMimeUtf8String.data());
	wl_data_source_offer(source, ClipboardMimePlain.data());
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

void WaylandClipboard::AppendPollDescriptors(std::vector<pollfd> &descriptors) const {
	for (const ActiveTransfer &active : transfers) {
		if (active.transfer.Pending()) {
			descriptors.push_back({active.transfer.Descriptor(),
				static_cast<short>(
					active.transfer.Direction() == WaylandTransferDirection::Read ?
						POLLIN : POLLOUT),
				0});
		}
	}
}

void WaylandClipboard::ProcessPollDescriptors(const std::vector<pollfd> &descriptors,
	std::size_t firstDescriptor) {
	std::size_t descriptorIndex = firstDescriptor;
	for (ActiveTransfer &active : transfers) {
		if (!active.transfer.Pending()) {
			continue;
		}
		if (descriptorIndex < descriptors.size() &&
			descriptors[descriptorIndex].fd == active.transfer.Descriptor()) {
			const short revents = descriptors[descriptorIndex].revents;
			const short ready = active.transfer.Direction() == WaylandTransferDirection::Read ?
				POLLIN : POLLOUT;
			if (revents & (ready | POLLERR | POLLHUP | POLLNVAL)) {
				active.transfer.ProcessReady();
			}
		}
		++descriptorIndex;
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
		wl_data_source_destroy(source);
		source = nullptr;
		if (reportCancellation) {
			Report(sourceRequest, ClipboardOperation::Copy, ClipboardResultStatus::Cancelled);
		}
	}
	sourceRequest = 0;
	state.CancelOwnership();
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
		ClipboardResultStatus status = ResultStatus(result->status);
		if (active.operation == ClipboardOperation::Paste &&
			status == ClipboardResultStatus::Complete &&
			!IsValidClipboardUtf8(result->bytes)) {
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
		ClipboardMimeRank(mimeType) == 0) {
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
		clipboard.Report(clipboard.sourceRequest, ClipboardOperation::Copy,
			ClipboardResultStatus::Failed);
	}
}

void WaylandClipboard::DataSourceCancelled(void *data, wl_data_source *source_) {
	auto &clipboard = *static_cast<WaylandClipboard *>(data);
	if (source_ == clipboard.source) {
		wl_data_source_destroy(clipboard.source);
		clipboard.source = nullptr;
		clipboard.state.CancelOwnership();
		clipboard.Report(clipboard.sourceRequest, ClipboardOperation::Copy,
			ClipboardResultStatus::Cancelled);
		clipboard.sourceRequest = 0;
	}
}

void WaylandClipboard::DataSourceDropPerformed(void *, wl_data_source *) {
}

void WaylandClipboard::DataSourceFinished(void *, wl_data_source *) {
}

void WaylandClipboard::DataSourceAction(void *, wl_data_source *, uint32_t) {
}

}
