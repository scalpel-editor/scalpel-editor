#include "OnlyWayUi_Platform_Wayland.h"
#include <OnlyWayUi/Config/Config.h>
#include <OnlyWayUi/Core/BasicTypes.h>
#include <OnlyWayUi/Core/Input.h>
#include <OnlyWayUi/Core/Log.h>
#include <OnlyWayUi/Core/StringUtilities.h>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <fcntl.h>
#include <iterator>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-cursor.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon-names.h>

struct wl_buffer;
struct wl_data_device;
struct wl_data_offer;
struct wl_data_source;

static constexpr const char* MimeTextUtf8 = "text/plain;charset=utf-8";
static constexpr const char* MimeTextPlain = "text/plain";
static constexpr size_t MaxClipboardTextBytes = 16 * 1024 * 1024;
static constexpr double ClipboardReadIdleTimeout = 0.5;
static constexpr double ClipboardReadTimeout = 5.0;
static constexpr double ClipboardWriteTimeout = 5.0;

struct CursorSettings {
	OnlyWayUi::String theme_name;
	OnlyWayUi::String theme_source;
	int size = 24;
	bool has_size = false;
};

static constexpr const char* DefaultCursorNames[] = {"default", "left_ptr", "arrow"};
static constexpr const char* MoveCursorNames[] = {"move", "fleur", "all-scroll"};
static constexpr const char* PointerCursorNames[] = {"pointer", "hand2", "pointing_hand"};
static constexpr const char* ResizeCursorNames[] = {"se-resize", "bottom_right_corner", "size_fdiag"};
static constexpr const char* CrossCursorNames[] = {"crosshair", "cross", "tcross"};
static constexpr const char* TextCursorNames[] = {"text", "xterm", "ibeam"};
static constexpr const char* UnavailableCursorNames[] = {"not-allowed", "forbidden", "crossed_circle"};

static double GetMonotonicTime()
{
	timespec now {};
	clock_gettime(CLOCK_MONOTONIC, &now);
	return double(now.tv_sec) + double(now.tv_nsec) / 1000000000.0;
}

static OnlyWayUi::String GetEnvironmentValue(const char* name)
{
	if (const char* value = getenv(name))
		return value;
	return {};
}

static bool ParseCursorSize(const OnlyWayUi::String& string, int& size)
{
	const OnlyWayUi::String stripped = OnlyWayUi::StringUtilities::StripWhitespace(string);
	if (stripped.empty())
		return false;

	char* end = nullptr;
	const long parsed_size = strtol(stripped.c_str(), &end, 10);
	if (!end || *end != '\0' || parsed_size <= 0 || parsed_size > 1024)
		return false;

	size = int(parsed_size);
	return true;
}

static bool IsKdeSession()
{
	const OnlyWayUi::String current_desktop = OnlyWayUi::StringUtilities::ToLower(GetEnvironmentValue("XDG_CURRENT_DESKTOP"));
	if (current_desktop.find("kde") != OnlyWayUi::String::npos || current_desktop.find("plasma") != OnlyWayUi::String::npos)
		return true;

	const OnlyWayUi::String session_desktop = OnlyWayUi::StringUtilities::ToLower(GetEnvironmentValue("XDG_SESSION_DESKTOP"));
	if (session_desktop.find("kde") != OnlyWayUi::String::npos || session_desktop.find("plasma") != OnlyWayUi::String::npos)
		return true;

	return !GetEnvironmentValue("KDE_FULL_SESSION").empty();
}

static OnlyWayUi::String MakeHomePath(const char* relative_path)
{
	const OnlyWayUi::String home = GetEnvironmentValue("HOME");
	if (home.empty())
		return {};

	OnlyWayUi::String path = home;
	path += "/";
	path += relative_path;
	return path;
}

static void ReadKdeCursorConfig(const OnlyWayUi::String& path, CursorSettings& settings)
{
	std::ifstream stream(path.c_str());
	if (!stream)
		return;

	bool in_mouse_group = false;
	OnlyWayUi::String line;
	while (std::getline(stream, line))
	{
		line = OnlyWayUi::StringUtilities::StripWhitespace(line);
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;

		if (line.front() == '[' && line.back() == ']')
		{
			const OnlyWayUi::String group_name = line.substr(1, line.size() - 2);
			in_mouse_group = (group_name == "Mouse");
			continue;
		}

		if (!in_mouse_group)
			continue;

		const size_t equals = line.find('=');
		if (equals == OnlyWayUi::String::npos)
			continue;

		const OnlyWayUi::String key = OnlyWayUi::StringUtilities::StripWhitespace(line.substr(0, equals));
		const OnlyWayUi::String value = OnlyWayUi::StringUtilities::StripWhitespace(line.substr(equals + 1));
		if (value.empty())
			continue;

		if (settings.theme_name.empty() && key == "cursorTheme")
		{
			settings.theme_name = value;
			settings.theme_source = path;
		}
		else if (!settings.has_size && (key == "cursorSize" || key == "CursorSize"))
		{
			int parsed_size = 0;
			if (ParseCursorSize(value, parsed_size))
			{
				settings.size = parsed_size;
				settings.has_size = true;
			}
		}
	}
}

static CursorSettings ResolveCursorSettings()
{
	CursorSettings settings;

	const OnlyWayUi::String environment_theme = OnlyWayUi::StringUtilities::StripWhitespace(GetEnvironmentValue("XCURSOR_THEME"));
	if (!environment_theme.empty())
	{
		settings.theme_name = environment_theme;
		settings.theme_source = "XCURSOR_THEME";
	}

	int environment_size = 0;
	if (ParseCursorSize(GetEnvironmentValue("XCURSOR_SIZE"), environment_size))
	{
		settings.size = environment_size;
		settings.has_size = true;
	}

	if (IsKdeSession())
	{
		// XDG_CONFIG_HOME replaces ~/.config; do not fall through to the home path when it is set.
		const OnlyWayUi::String xdg_config_home = GetEnvironmentValue("XDG_CONFIG_HOME");
		const OnlyWayUi::String config_home = !xdg_config_home.empty() ? xdg_config_home : MakeHomePath(".config");
		if (!config_home.empty())
		{
			ReadKdeCursorConfig(config_home + "/kcminputrc", settings);
			ReadKdeCursorConfig(config_home + "/kdedefaults/kcminputrc", settings);
		}
	}

	return settings;
}

static int GetTextMimeTypeRank(const char* mime_type)
{
	if (!mime_type)
		return 0;

	const OnlyWayUi::String mime_type_lower = OnlyWayUi::StringUtilities::ToLower(mime_type);
	if (mime_type_lower == MimeTextUtf8)
		return 5;
	if (mime_type_lower == MimeTextPlain)
		return 4;
	if (OnlyWayUi::StringUtilities::StartsWith(mime_type_lower, "text/plain;"))
		return 3;
	if (mime_type_lower == "utf8_string" || mime_type_lower == "text" || mime_type_lower == "string")
		return 2;
	if (OnlyWayUi::StringUtilities::StartsWith(mime_type_lower, "text/"))
		return 1;

	return 0;
}

static bool IsTextMimeType(const char* mime_type)
{
	return GetTextMimeTypeRank(mime_type) > 0;
}

static bool GetUtf16EndianFromMimeType(const OnlyWayUi::String& mime_type, bool& big_endian)
{
	const OnlyWayUi::String mime_type_lower = OnlyWayUi::StringUtilities::ToLower(mime_type);
	if (mime_type_lower.find("utf-16be") != OnlyWayUi::String::npos || mime_type_lower.find("utf16be") != OnlyWayUi::String::npos)
	{
		big_endian = true;
		return true;
	}
	if (mime_type_lower.find("utf-16le") != OnlyWayUi::String::npos || mime_type_lower.find("utf16le") != OnlyWayUi::String::npos)
	{
		big_endian = false;
		return true;
	}
	if (mime_type_lower.find("utf-16") != OnlyWayUi::String::npos || mime_type_lower.find("utf16") != OnlyWayUi::String::npos)
	{
		big_endian = false;
		return true;
	}

	return false;
}

static bool IsUtf8MimeType(const OnlyWayUi::String& mime_type)
{
	const OnlyWayUi::String mime_type_lower = OnlyWayUi::StringUtilities::ToLower(mime_type);
	return mime_type_lower == MimeTextUtf8 || mime_type_lower == "utf8_string" ||
		mime_type_lower.find("charset=utf-8") != OnlyWayUi::String::npos ||
		mime_type_lower.find("charset=utf8") != OnlyWayUi::String::npos;
}

static bool GetUtf16EndianFromBom(const OnlyWayUi::String& text, bool& big_endian)
{
	const auto byte_at = [&text](size_t index) { return static_cast<unsigned char>(text[index]); };
	if (text.size() >= 2)
	{
		if (byte_at(0) == 0xfe && byte_at(1) == 0xff)
		{
			big_endian = true;
			return true;
		}
		if (byte_at(0) == 0xff && byte_at(1) == 0xfe)
		{
			big_endian = false;
			return true;
		}
	}

	return false;
}

static bool GetUtf16EndianFromData(const OnlyWayUi::String& text, bool& big_endian)
{
	if (GetUtf16EndianFromBom(text, big_endian))
		return true;

	const size_t sample_size = (text.size() < size_t(128) ? text.size() : size_t(128));
	size_t even_zero_count = 0;
	size_t odd_zero_count = 0;
	for (size_t i = 0; i < sample_size; ++i)
	{
		if (text[i] == '\0')
		{
			if (i % 2 == 0)
				++even_zero_count;
			else
				++odd_zero_count;
		}
	}

	const size_t pairs = sample_size / 2;
	if (pairs >= 4 && odd_zero_count >= pairs / 2 && even_zero_count <= pairs / 8)
	{
		big_endian = false;
		return true;
	}
	if (pairs >= 4 && even_zero_count >= pairs / 2 && odd_zero_count <= pairs / 8)
	{
		big_endian = true;
		return true;
	}

	return false;
}

static void AppendUtf8(OnlyWayUi::String& output, uint32_t codepoint)
{
	if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
		codepoint = uint32_t(OnlyWayUi::Character::Replacement);

	output += OnlyWayUi::StringUtilities::ToUTF8(static_cast<OnlyWayUi::Character>(codepoint));
}

static OnlyWayUi::String ConvertUtf16ToUtf8(const OnlyWayUi::String& text, bool big_endian)
{
	const auto read_u16 = [&text, big_endian](size_t index) {
		const uint16_t first = static_cast<unsigned char>(text[index]);
		const uint16_t second = (index + 1 < text.size() ? static_cast<unsigned char>(text[index + 1]) : 0);
		return uint16_t(big_endian ? (first << 8) | second : (second << 8) | first);
	};

	OnlyWayUi::String output;
	output.reserve(text.size());

	size_t index = 0;
	if (text.size() >= 2)
	{
		const uint16_t bom = read_u16(0);
		if (bom == 0xfeff || bom == 0xfffe)
			index = 2;
	}

	while (index < text.size())
	{
		const uint16_t code_unit = read_u16(index);
		index += 2;
		if (code_unit == 0 && index >= text.size())
			break;

		if (code_unit >= 0xd800 && code_unit <= 0xdbff)
		{
			if (index < text.size())
			{
				const uint16_t next_code_unit = read_u16(index);
				if (next_code_unit >= 0xdc00 && next_code_unit <= 0xdfff)
				{
					index += 2;
					const uint32_t high = uint32_t(code_unit - 0xd800);
					const uint32_t low = uint32_t(next_code_unit - 0xdc00);
					AppendUtf8(output, 0x10000 + ((high << 10) | low));
					continue;
				}
			}

			AppendUtf8(output, uint32_t(OnlyWayUi::Character::Replacement));
		}
		else if (code_unit >= 0xdc00 && code_unit <= 0xdfff)
		{
			AppendUtf8(output, uint32_t(OnlyWayUi::Character::Replacement));
		}
		else
		{
			AppendUtf8(output, code_unit);
		}
	}

	return output;
}

static OnlyWayUi::String DecodeClipboardText(OnlyWayUi::String text, const OnlyWayUi::String& mime_type)
{
	bool big_endian = false;
	const bool has_utf16_mime_type = GetUtf16EndianFromMimeType(mime_type, big_endian);
	if (has_utf16_mime_type)
	{
		GetUtf16EndianFromBom(text, big_endian);
		return ConvertUtf16ToUtf8(text, big_endian);
	}

	bool data_big_endian = false;
	if (GetUtf16EndianFromBom(text, data_big_endian))
		return ConvertUtf16ToUtf8(text, data_big_endian);

	const bool looks_like_utf16 = !IsUtf8MimeType(mime_type) && GetUtf16EndianFromData(text, data_big_endian);
	if (looks_like_utf16)
		big_endian = data_big_endian;

	if (looks_like_utf16)
		return ConvertUtf16ToUtf8(text, big_endian);

	return text;
}

static void CloseFd(int& fd)
{
	if (fd >= 0)
	{
		close(fd);
		fd = -1;
	}
}

static void SetCloseOnExec(int fd)
{
	const int flags = fcntl(fd, F_GETFD);
	if (flags >= 0)
		fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static void SetNonBlocking(int fd)
{
	const int flags = fcntl(fd, F_GETFL);
	if (flags >= 0)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static ssize_t WriteNoSigpipe(int fd, const char* data, size_t size)
{
	sigset_t sigpipe_set;
	sigset_t old_signal_mask;
	sigset_t pending_signals;
	sigemptyset(&sigpipe_set);
	sigaddset(&sigpipe_set, SIGPIPE);
	const bool had_pending_sigpipe = (sigpending(&pending_signals) == 0 && sigismember(&pending_signals, SIGPIPE) == 1);
	const bool blocked_sigpipe = (sigprocmask(SIG_BLOCK, &sigpipe_set, &old_signal_mask) == 0);

	const ssize_t bytes_written = write(fd, data, size);

	if (blocked_sigpipe)
	{
		if (!had_pending_sigpipe)
		{
			timespec timeout {};
			sigtimedwait(&sigpipe_set, nullptr, &timeout);
		}
		sigprocmask(SIG_SETMASK, &old_signal_mask, nullptr);
	}

	return bytes_written;
}

struct ClipboardOffer_Wayland {
	explicit ClipboardOffer_Wayland(wl_data_offer* offer) : offer(offer) {}
	~ClipboardOffer_Wayland()
	{
		if (offer)
			wl_data_offer_destroy(offer);
	}

	wl_data_offer* offer = nullptr;
	OnlyWayUi::String text_mime_type;
	int text_mime_rank = 0;

	void OfferMimeType(const char* mime_type)
	{
		const int mime_rank = GetTextMimeTypeRank(mime_type);
		if (mime_rank > text_mime_rank)
		{
			text_mime_type = mime_type;
			text_mime_rank = mime_rank;
		}
	}

	const char* GetPreferredMimeType() const
	{
		return text_mime_type.empty() ? nullptr : text_mime_type.c_str();
	}
};

class ClipboardManager_Wayland {
public:
	ClipboardManager_Wayland(wl_display* display, wl_data_device_manager* data_device_manager) : display(display), data_device_manager(data_device_manager) {}
	~ClipboardManager_Wayland()
	{
		CancelRead();
		CancelWrites();
		DeliverEmptyPending();
		current_offer = nullptr;
		offers.clear();
		DestroyActiveSource();
		DestroyDataDevice();
	}

	void SetSeat(wl_seat* seat)
	{
		DestroyDataDevice();

		if (!data_device_manager || !seat)
			return;

		data_device = wl_data_device_manager_get_data_device(data_device_manager, seat);
		if (!data_device)
			return;

		wl_data_device_add_listener(data_device, &data_device_listener, this);
	}

	void SetSeatSerial(uint32_t serial)
	{
		selection_serial = serial;
		has_selection_serial = true;
	}

	void SetText(const OnlyWayUi::String& text)
	{
		owned_text = text;
		owns_selection = true;

		if (!data_device_manager || !data_device || !has_selection_serial)
			return;

		DestroyActiveSource();

		wl_data_source* source = wl_data_device_manager_create_data_source(data_device_manager);
		if (!source)
			return;

		active_source = source;
		wl_data_source_add_listener(active_source, &data_source_listener, this);
		wl_data_source_offer(active_source, MimeTextUtf8);
		wl_data_source_offer(active_source, MimeTextPlain);
		wl_data_device_set_selection(data_device, active_source, selection_serial);
		wl_display_flush(display);
	}

	void RequestText(OnlyWayUi::Function<void(OnlyWayUi::String)> callback)
	{
		if (owns_selection)
		{
			if (callback)
				callback(owned_text);
			return;
		}

		CancelRead();
		DeliverEmptyPending();

		if (!current_offer)
		{
			if (callback)
				callback(OnlyWayUi::String());
			return;
		}

		if (!current_offer->GetPreferredMimeType())
		{
			pending_read_callback = std::move(callback);
			return;
		}

		StartRead(std::move(callback));
	}

	void StartRead(OnlyWayUi::Function<void(OnlyWayUi::String)> callback)
	{
		const char* mime_type = current_offer ? current_offer->GetPreferredMimeType() : nullptr;
		if (!mime_type)
		{
			if (callback)
				callback(OnlyWayUi::String());
			return;
		}

		int pipe_fd[2] = {-1, -1};
		if (pipe(pipe_fd) != 0)
		{
			if (callback)
				callback(OnlyWayUi::String());
			return;
		}

		SetCloseOnExec(pipe_fd[0]);
		SetCloseOnExec(pipe_fd[1]);
		SetNonBlocking(pipe_fd[0]);

		read_fd = pipe_fd[0];
		read_callback = std::move(callback);
		read_mime_type = mime_type;
		read_text.clear();
		read_start_time = GetMonotonicTime();
		read_last_data_time = 0.0;

		wl_data_offer_accept(current_offer->offer, selection_serial, mime_type);
		wl_data_offer_receive(current_offer->offer, mime_type, pipe_fd[1]);
		CloseFd(pipe_fd[1]);
		wl_display_flush(display);

		ProcessRead();
	}

	void GetText(OnlyWayUi::String& text) const
	{
		if (owns_selection)
			text = owned_text;
		else
			text.clear();
	}

	int GetReadFd() const
	{
		return read_fd;
	}

	int GetWriteFd() const
	{
		return pending_writes.empty() ? -1 : pending_writes.front().fd;
	}

	void ProcessRead()
	{
		if (read_fd < 0)
			return;

		char buffer[4096];
		while (true)
		{
			const ssize_t bytes_read = read(read_fd, buffer, sizeof(buffer));
			if (bytes_read > 0)
			{
				if (read_text.size() + size_t(bytes_read) > MaxClipboardTextBytes)
				{
					FinishRead(OnlyWayUi::String());
					return;
				}

				read_text.append(buffer, size_t(bytes_read));
				read_last_data_time = GetMonotonicTime();
			}
			else if (bytes_read == 0)
			{
				FinishRead(std::move(read_text));
				return;
			}
			else if (errno == EINTR)
			{
				continue;
			}
			else if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				const double now = GetMonotonicTime();
				if (!read_text.empty() && now - read_last_data_time >= ClipboardReadIdleTimeout)
				{
					FinishRead(std::move(read_text));
					return;
				}
				if (read_text.empty() && now - read_start_time >= ClipboardReadTimeout)
				{
					FinishRead(OnlyWayUi::String());
					return;
				}
				return;
			}
			else
			{
				FinishRead(OnlyWayUi::String());
				return;
			}
		}
	}

	void ProcessWrite()
	{
		while (!pending_writes.empty())
		{
			PendingWrite& write = pending_writes.front();
			if (GetMonotonicTime() - write.start_time >= ClipboardWriteTimeout)
			{
				CloseFd(write.fd);
				pending_writes.erase(pending_writes.begin());
				continue;
			}

			while (write.offset < write.text.size())
			{
				const ssize_t bytes_written = WriteNoSigpipe(write.fd, write.text.data() + write.offset, write.text.size() - write.offset);
				if (bytes_written > 0)
				{
					write.offset += size_t(bytes_written);
				}
				else if (bytes_written < 0 && errno == EINTR)
				{
					continue;
				}
				else if (bytes_written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
				{
					return;
				}
				else
				{
					break;
				}
			}

			CloseFd(write.fd);
			pending_writes.erase(pending_writes.begin());
		}
	}

private:
	struct PendingWrite {
		int fd = -1;
		OnlyWayUi::String text;
		size_t offset = 0;
		double start_time = 0.0;
	};

	void DestroyDataDevice()
	{
		CancelRead();
		CancelWrites();
		DeliverEmptyPending();
		current_offer = nullptr;
		offers.clear();

		if (data_device)
		{
			if (wl_data_device_get_version(data_device) >= WL_DATA_DEVICE_RELEASE_SINCE_VERSION)
				wl_data_device_release(data_device);
			else
				wl_data_device_destroy(data_device);
			data_device = nullptr;
		}
	}

	void DestroyActiveSource()
	{
		if (active_source)
		{
			wl_data_source_destroy(active_source);
			active_source = nullptr;
		}
	}

	void CancelRead()
	{
		CloseFd(read_fd);
		read_mime_type.clear();
		read_text.clear();
		read_start_time = 0.0;
		read_last_data_time = 0.0;

		// The read was aborted rather than completed. RequestClipboardText promises the
		// callback runs exactly once, so deliver empty text instead of dropping it. Take
		// ownership before invoking so a re-entrant request from inside the callback starts
		// from a clean state.
		OnlyWayUi::Function<void(OnlyWayUi::String)> callback = std::move(read_callback);
		read_callback = nullptr;
		if (callback)
			callback(OnlyWayUi::String());
	}

	// Deliver empty text to a callback still waiting for a usable mime type, exactly once.
	void DeliverEmptyPending()
	{
		OnlyWayUi::Function<void(OnlyWayUi::String)> callback = std::move(pending_read_callback);
		pending_read_callback = nullptr;
		if (callback)
			callback(OnlyWayUi::String());
	}

	void CancelWrites()
	{
		for (PendingWrite& write : pending_writes)
			CloseFd(write.fd);
		pending_writes.clear();
	}

	void FinishRead(OnlyWayUi::String text)
	{
		CloseFd(read_fd);
		text = DecodeClipboardText(std::move(text), read_mime_type);
		read_mime_type.clear();
		read_text.clear();
		read_start_time = 0.0;
		read_last_data_time = 0.0;

		OnlyWayUi::Function<void(OnlyWayUi::String)> callback = std::move(read_callback);
		read_callback = nullptr;
		if (callback)
			callback(std::move(text));
	}

	void SetPendingOffer(wl_data_offer* offer)
	{
		auto offer_data = OnlyWayUi::MakeUnique<ClipboardOffer_Wayland>(offer);
		wl_data_offer_add_listener(offer, &data_offer_listener, this);
		offers[offer] = std::move(offer_data);
	}

	void SetSelectionOffer(wl_data_offer* offer)
	{
		if (read_fd >= 0)
			FinishRead(OnlyWayUi::String());
		if (pending_read_callback)
		{
			OnlyWayUi::Function<void(OnlyWayUi::String)> callback = std::move(pending_read_callback);
			pending_read_callback = nullptr;
			callback(OnlyWayUi::String());
		}

		for (auto it = offers.begin(); it != offers.end();)
		{
			if (it->first != offer)
				it = offers.erase(it);
			else
				++it;
		}

		current_offer = nullptr;

		if (!offer)
			return;

		auto it = offers.find(offer);
		if (it != offers.end())
			current_offer = it->second.get();
	}

	static void DataOfferHandleOffer(void* data, wl_data_offer* offer, const char* mime_type)
	{
		auto* manager = static_cast<ClipboardManager_Wayland*>(data);
		auto it = manager->offers.find(offer);
		if (it == manager->offers.end() || !mime_type)
			return;

		ClipboardOffer_Wayland* offer_data = it->second.get();
		const bool had_text_mime_type = (offer_data->GetPreferredMimeType() != nullptr);
		offer_data->OfferMimeType(mime_type);

		if (!had_text_mime_type && offer_data == manager->current_offer && manager->pending_read_callback)
		{
			OnlyWayUi::Function<void(OnlyWayUi::String)> callback = std::move(manager->pending_read_callback);
			manager->pending_read_callback = nullptr;
			manager->StartRead(std::move(callback));
		}
	}

	static void DataOfferHandleSourceActions(void*, wl_data_offer*, uint32_t) {}
	static void DataOfferHandleAction(void*, wl_data_offer*, uint32_t) {}

	static void DataDeviceHandleDataOffer(void* data, wl_data_device*, wl_data_offer* offer)
	{
		static_cast<ClipboardManager_Wayland*>(data)->SetPendingOffer(offer);
	}

	static void DataDeviceHandleEnter(void* data, wl_data_device*, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer* offer)
	{
		auto* manager = static_cast<ClipboardManager_Wayland*>(data);
		manager->offers.erase(offer);
	}

	static void DataDeviceHandleLeave(void*, wl_data_device*) {}
	static void DataDeviceHandleMotion(void*, wl_data_device*, uint32_t, wl_fixed_t, wl_fixed_t) {}
	static void DataDeviceHandleDrop(void*, wl_data_device*) {}

	static void DataDeviceHandleSelection(void* data, wl_data_device*, wl_data_offer* offer)
	{
		static_cast<ClipboardManager_Wayland*>(data)->SetSelectionOffer(offer);
	}

	static void DataSourceHandleTarget(void*, wl_data_source*, const char*) {}

	static void DataSourceHandleSend(void* data, wl_data_source*, const char* mime_type, int32_t fd)
	{
		auto* manager = static_cast<ClipboardManager_Wayland*>(data);
		if (IsTextMimeType(mime_type))
		{
				SetCloseOnExec(fd);
				SetNonBlocking(fd);
				manager->pending_writes.push_back(PendingWrite{fd, manager->owned_text, 0, GetMonotonicTime()});
				manager->ProcessWrite();
			}
		else
		{
			close(fd);
		}
	}

	static void DataSourceHandleCancelled(void* data, wl_data_source* source)
	{
		auto* manager = static_cast<ClipboardManager_Wayland*>(data);
		if (manager->active_source == source)
		{
			manager->active_source = nullptr;
			wl_data_source_destroy(source);
			manager->owns_selection = false;
		}
	}

	static void DataSourceHandleDndDropPerformed(void*, wl_data_source*) {}
	static void DataSourceHandleDndFinished(void*, wl_data_source*) {}
	static void DataSourceHandleAction(void*, wl_data_source*, uint32_t) {}

	wl_display* display = nullptr;
	wl_data_device_manager* data_device_manager = nullptr;
	wl_data_device* data_device = nullptr;
	wl_data_source* active_source = nullptr;
	OnlyWayUi::UnorderedMap<wl_data_offer*, OnlyWayUi::UniquePtr<ClipboardOffer_Wayland>> offers;
	ClipboardOffer_Wayland* current_offer = nullptr;

	uint32_t selection_serial = 0;
	bool has_selection_serial = false;
	bool owns_selection = false;
	OnlyWayUi::String owned_text;

	int read_fd = -1;
	OnlyWayUi::String read_mime_type;
	OnlyWayUi::String read_text;
	OnlyWayUi::Function<void(OnlyWayUi::String)> read_callback;
	OnlyWayUi::Function<void(OnlyWayUi::String)> pending_read_callback;
	double read_start_time = 0.0;
	double read_last_data_time = 0.0;
	OnlyWayUi::Vector<PendingWrite> pending_writes;

	static const wl_data_offer_listener data_offer_listener;
	static const wl_data_device_listener data_device_listener;
	static const wl_data_source_listener data_source_listener;
};

const wl_data_offer_listener ClipboardManager_Wayland::data_offer_listener = {
	ClipboardManager_Wayland::DataOfferHandleOffer,
	ClipboardManager_Wayland::DataOfferHandleSourceActions,
	ClipboardManager_Wayland::DataOfferHandleAction,
};

const wl_data_device_listener ClipboardManager_Wayland::data_device_listener = {
	ClipboardManager_Wayland::DataDeviceHandleDataOffer,
	ClipboardManager_Wayland::DataDeviceHandleEnter,
	ClipboardManager_Wayland::DataDeviceHandleLeave,
	ClipboardManager_Wayland::DataDeviceHandleMotion,
	ClipboardManager_Wayland::DataDeviceHandleDrop,
	ClipboardManager_Wayland::DataDeviceHandleSelection,
};

const wl_data_source_listener ClipboardManager_Wayland::data_source_listener = {
	ClipboardManager_Wayland::DataSourceHandleTarget,
	ClipboardManager_Wayland::DataSourceHandleSend,
	ClipboardManager_Wayland::DataSourceHandleCancelled,
	ClipboardManager_Wayland::DataSourceHandleDndDropPerformed,
	ClipboardManager_Wayland::DataSourceHandleDndFinished,
	ClipboardManager_Wayland::DataSourceHandleAction,
};

SystemInterface_Wayland::SystemInterface_Wayland(wl_display* display, wl_shm* shm, wl_data_device_manager* data_device_manager) :
	display(display), shm(shm), clipboard_manager(OnlyWayUi::MakeUnique<ClipboardManager_Wayland>(display, data_device_manager))
{
	clock_start_time = ClockNow();
}

SystemInterface_Wayland::~SystemInterface_Wayland()
{
	if (cursor_theme)
		wl_cursor_theme_destroy(cursor_theme);
}

void SystemInterface_Wayland::SetPointer(wl_pointer* in_pointer)
{
	pointer = in_pointer;
}

void SystemInterface_Wayland::SetSeat(wl_seat* seat)
{
	clipboard_manager->SetSeat(seat);
}

void SystemInterface_Wayland::SetCursorSurface(wl_surface* surface)
{
	cursor_surface = surface;
}

void SystemInterface_Wayland::SetPointerSerial(uint32_t serial)
{
	pointer_serial = serial;
	has_pointer_serial = true;
}

void SystemInterface_Wayland::ClearPointerSerial()
{
	has_pointer_serial = false;
}

void SystemInterface_Wayland::SetSeatSerial(uint32_t serial)
{
	clipboard_manager->SetSeatSerial(serial);
}

int SystemInterface_Wayland::GetClipboardReadFd() const
{
	return clipboard_manager->GetReadFd();
}

int SystemInterface_Wayland::GetClipboardWriteFd() const
{
	return clipboard_manager->GetWriteFd();
}

void SystemInterface_Wayland::ProcessClipboardRead()
{
	clipboard_manager->ProcessRead();
}

void SystemInterface_Wayland::ProcessClipboardWrite()
{
	clipboard_manager->ProcessWrite();
}

void SystemInterface_Wayland::SetClock(clockid_t in_clock_id)
{
	// Reject a clock this platform cannot read; keep the monotonic fallback.
	timespec probe {};
	if (clock_gettime(in_clock_id, &probe) != 0)
		return;

	clock_id = in_clock_id;
	clock_start_time = ClockNow();
}

void SystemInterface_Wayland::SetPredictedFrameTime(double clock_seconds)
{
	const double elapsed = clock_seconds - clock_start_time;
	if (elapsed > predicted_frame_time)
		predicted_frame_time = elapsed;
	has_predicted_frame_time = true;
}

void SystemInterface_Wayland::ClearPredictedFrameTime()
{
	has_predicted_frame_time = false;
}

double SystemInterface_Wayland::GetElapsedTime()
{
	if (has_predicted_frame_time)
		return predicted_frame_time;

	// Live clock, floored by the last prediction (which runs up to one refresh ahead of the live clock) so the returned
	// time never steps backwards across ClearPredictedFrameTime.
	const double elapsed = ClockNow() - clock_start_time;
	return (elapsed > predicted_frame_time) ? elapsed : predicted_frame_time;
}

double SystemInterface_Wayland::ClockNow() const
{
	timespec now {};
	if (clock_gettime(clock_id, &now) != 0)
		return 0.0;
	return double(now.tv_sec) + double(now.tv_nsec) / 1.0e9;
}

void SystemInterface_Wayland::LoadCursorTheme()
{
	// Resolve and attempt load once per instance once shm is available. A failed attempt is not retried on every
	// ApplyCursor; that would re-read env and config files for no gain.
	if (cursor_theme || cursor_theme_load_attempted || !shm)
		return;

	cursor_theme_load_attempted = true;

	const CursorSettings settings = ResolveCursorSettings();
	const int cursor_size = settings.size;
	const char* theme_name = settings.theme_name.empty() ? nullptr : settings.theme_name.c_str();

	if (theme_name)
	{
		if (settings.theme_source.empty())
			OnlyWayUi::Log::Message(OnlyWayUi::Log::LT_INFO, "Loading Wayland cursor theme '%s' at size %d.", theme_name, cursor_size);
		else
			OnlyWayUi::Log::Message(OnlyWayUi::Log::LT_INFO, "Loading Wayland cursor theme '%s' at size %d from %s.", theme_name, cursor_size,
				settings.theme_source.c_str());

		cursor_theme = wl_cursor_theme_load(theme_name, cursor_size, shm);
		if (!cursor_theme)
		{
			OnlyWayUi::Log::Message(OnlyWayUi::Log::LT_WARNING, "Failed to load Wayland cursor theme '%s'; falling back to default.", theme_name);
			cursor_theme = wl_cursor_theme_load(nullptr, cursor_size, shm);
		}
	}
	else
	{
		OnlyWayUi::Log::Message(OnlyWayUi::Log::LT_INFO, "Loading default Wayland cursor theme at size %d.", cursor_size);
		cursor_theme = wl_cursor_theme_load(nullptr, cursor_size, shm);
	}

	if (!cursor_theme)
		OnlyWayUi::Log::Message(OnlyWayUi::Log::LT_WARNING, "Failed to load Wayland cursor theme.");
}

void SystemInterface_Wayland::ApplyCursor(const char* const* cursor_names, size_t cursor_name_count)
{
	if (!pointer || !cursor_surface || !has_pointer_serial || !cursor_names || cursor_name_count == 0)
		return;

	LoadCursorTheme();
	if (!cursor_theme)
		return;

	wl_cursor* cursor = nullptr;
	for (size_t i = 0; i < cursor_name_count; ++i)
	{
		if (!cursor_names[i])
			continue;

		cursor = wl_cursor_theme_get_cursor(cursor_theme, cursor_names[i]);
		if (cursor && cursor->image_count > 0)
			break;
	}

	if (!cursor || cursor->image_count == 0)
	{
		for (const char* fallback_name : DefaultCursorNames)
		{
			cursor = wl_cursor_theme_get_cursor(cursor_theme, fallback_name);
			if (cursor && cursor->image_count > 0)
				break;
		}
	}

	if (!cursor || cursor->image_count == 0)
	{
		const char* requested_name = cursor_names[0] ? cursor_names[0] : "";
		if (last_failed_cursor_name != requested_name)
		{
			OnlyWayUi::Log::Message(OnlyWayUi::Log::LT_WARNING, "Failed to load Wayland cursor '%s'.", requested_name);
			last_failed_cursor_name = requested_name;
		}
		return;
	}

	wl_cursor_image* image = cursor->images[0];
	wl_buffer* buffer = wl_cursor_image_get_buffer(image);
	if (!buffer)
		return;

	wl_pointer_set_cursor(pointer, pointer_serial, cursor_surface, int32_t(image->hotspot_x), int32_t(image->hotspot_y));
	wl_surface_attach(cursor_surface, buffer, 0, 0);
	wl_surface_damage(cursor_surface, 0, 0, int32_t(image->width), int32_t(image->height));
	wl_surface_commit(cursor_surface);
	wl_display_flush(display);
}

void SystemInterface_Wayland::SetMouseCursor(const OnlyWayUi::String& cursor_name)
{
	if (cursor_name.empty() || cursor_name == "arrow")
		ApplyCursor(DefaultCursorNames, std::size(DefaultCursorNames));
	else if (cursor_name == "move" || OnlyWayUi::StringUtilities::StartsWith(cursor_name, "rmlui-scroll"))
		ApplyCursor(MoveCursorNames, std::size(MoveCursorNames));
	else if (cursor_name == "pointer")
		ApplyCursor(PointerCursorNames, std::size(PointerCursorNames));
	else if (cursor_name == "resize")
		ApplyCursor(ResizeCursorNames, std::size(ResizeCursorNames));
	else if (cursor_name == "cross")
		ApplyCursor(CrossCursorNames, std::size(CrossCursorNames));
	else if (cursor_name == "text")
		ApplyCursor(TextCursorNames, std::size(TextCursorNames));
	else if (cursor_name == "unavailable")
		ApplyCursor(UnavailableCursorNames, std::size(UnavailableCursorNames));
	else
	{
		const char* custom_cursor_names[] = {cursor_name.c_str()};
		ApplyCursor(custom_cursor_names, std::size(custom_cursor_names));
	}
}

void SystemInterface_Wayland::SetClipboardText(const OnlyWayUi::String& text)
{
	clipboard_manager->SetText(text);
}

void SystemInterface_Wayland::RequestClipboardText(OnlyWayUi::Function<void(OnlyWayUi::String)> callback)
{
	clipboard_manager->RequestText(std::move(callback));
}

void SystemInterface_Wayland::GetClipboardText(OnlyWayUi::String& text)
{
	clipboard_manager->GetText(text);
}

namespace OwuiWayland {

KeyboardState::KeyboardState()
{
	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
}

KeyboardState::~KeyboardState()
{
	if (state)
		xkb_state_unref(state);
	if (keymap)
		xkb_keymap_unref(keymap);
	if (context)
		xkb_context_unref(context);
}

void KeyboardState::SetKeymapFromString(const char* keymap_string)
{
	if (!context || !keymap_string)
		return;

	xkb_keymap* new_keymap = xkb_keymap_new_from_string(context, keymap_string, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!new_keymap)
	{
		OnlyWayUi::Log::Message(OnlyWayUi::Log::LT_WARNING, "Failed to load Wayland keyboard map.");
		return;
	}

	xkb_state* new_state = xkb_state_new(new_keymap);
	if (!new_state)
	{
		xkb_keymap_unref(new_keymap);
		OnlyWayUi::Log::Message(OnlyWayUi::Log::LT_WARNING, "Failed to create Wayland keyboard state.");
		return;
	}

	if (state)
		xkb_state_unref(state);
	if (keymap)
		xkb_keymap_unref(keymap);

	keymap = new_keymap;
	state = new_state;
	modifiers = 0;
}

void KeyboardState::Reset()
{
	modifiers = 0;

	xkb_state* new_state = nullptr;
	if (keymap)
		new_state = xkb_state_new(keymap);

	if (state)
		xkb_state_unref(state);
	state = new_state;

	if (keymap && !state)
		OnlyWayUi::Log::Message(OnlyWayUi::Log::LT_WARNING, "Failed to reset Wayland keyboard state.");
}

void KeyboardState::UpdateModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
	if (!state)
		return;

	xkb_state_update_mask(state, depressed, latched, locked, 0, 0, group);
	modifiers = ConvertKeyModifiers(state);
}

int ConvertKeyModifiers(xkb_state* state)
{
	int result = 0;
	if (!state)
		return result;

	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE))
		result |= OnlyWayUi::Input::KM_SHIFT;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_EFFECTIVE))
		result |= OnlyWayUi::Input::KM_CAPSLOCK;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE))
		result |= OnlyWayUi::Input::KM_CTRL;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE))
		result |= OnlyWayUi::Input::KM_ALT;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_NUM, XKB_STATE_MODS_EFFECTIVE))
		result |= OnlyWayUi::Input::KM_NUMLOCK;

	return result;
}

int ConvertMouseButton(uint32_t button)
{
	switch (button)
	{
	case BTN_LEFT: return 0;
	case BTN_RIGHT: return 1;
	case BTN_MIDDLE: return 2;
	default: return -1;
	}
}

OnlyWayUi::Input::KeyIdentifier ConvertKeySym(xkb_keysym_t sym)
{
	// clang-format off
	switch (sym)
	{
	case XKB_KEY_BackSpace: return OnlyWayUi::Input::KI_BACK;
	case XKB_KEY_Tab: return OnlyWayUi::Input::KI_TAB;
	case XKB_KEY_Clear: return OnlyWayUi::Input::KI_CLEAR;
	case XKB_KEY_Return: return OnlyWayUi::Input::KI_RETURN;
	case XKB_KEY_Pause: return OnlyWayUi::Input::KI_PAUSE;
	case XKB_KEY_Scroll_Lock: return OnlyWayUi::Input::KI_SCROLL;
	case XKB_KEY_Escape: return OnlyWayUi::Input::KI_ESCAPE;
	case XKB_KEY_Delete: return OnlyWayUi::Input::KI_DELETE;
	case XKB_KEY_Home: return OnlyWayUi::Input::KI_HOME;
	case XKB_KEY_Left: return OnlyWayUi::Input::KI_LEFT;
	case XKB_KEY_Up: return OnlyWayUi::Input::KI_UP;
	case XKB_KEY_Right: return OnlyWayUi::Input::KI_RIGHT;
	case XKB_KEY_Down: return OnlyWayUi::Input::KI_DOWN;
	case XKB_KEY_Page_Up: return OnlyWayUi::Input::KI_PRIOR;
	case XKB_KEY_Page_Down: return OnlyWayUi::Input::KI_NEXT;
	case XKB_KEY_End: return OnlyWayUi::Input::KI_END;
	case XKB_KEY_Insert: return OnlyWayUi::Input::KI_INSERT;
	case XKB_KEY_Num_Lock: return OnlyWayUi::Input::KI_NUMLOCK;

	case XKB_KEY_KP_Space: return OnlyWayUi::Input::KI_SPACE;
	case XKB_KEY_KP_Tab: return OnlyWayUi::Input::KI_TAB;
	case XKB_KEY_KP_Enter: return OnlyWayUi::Input::KI_NUMPADENTER;
	case XKB_KEY_KP_Home: return OnlyWayUi::Input::KI_NUMPAD7;
	case XKB_KEY_KP_Left: return OnlyWayUi::Input::KI_NUMPAD4;
	case XKB_KEY_KP_Up: return OnlyWayUi::Input::KI_NUMPAD8;
	case XKB_KEY_KP_Right: return OnlyWayUi::Input::KI_NUMPAD6;
	case XKB_KEY_KP_Down: return OnlyWayUi::Input::KI_NUMPAD2;
	case XKB_KEY_KP_Page_Up: return OnlyWayUi::Input::KI_NUMPAD9;
	case XKB_KEY_KP_Page_Down: return OnlyWayUi::Input::KI_NUMPAD3;
	case XKB_KEY_KP_End: return OnlyWayUi::Input::KI_NUMPAD1;
	case XKB_KEY_KP_Begin: return OnlyWayUi::Input::KI_NUMPAD5;
	case XKB_KEY_KP_Insert: return OnlyWayUi::Input::KI_NUMPAD0;
	case XKB_KEY_KP_Delete: return OnlyWayUi::Input::KI_DECIMAL;
	case XKB_KEY_KP_Multiply: return OnlyWayUi::Input::KI_MULTIPLY;
	case XKB_KEY_KP_Add: return OnlyWayUi::Input::KI_ADD;
	case XKB_KEY_KP_Separator: return OnlyWayUi::Input::KI_SEPARATOR;
	case XKB_KEY_KP_Subtract: return OnlyWayUi::Input::KI_SUBTRACT;
	case XKB_KEY_KP_Decimal: return OnlyWayUi::Input::KI_DECIMAL;
	case XKB_KEY_KP_Divide: return OnlyWayUi::Input::KI_DIVIDE;

	case XKB_KEY_F1: return OnlyWayUi::Input::KI_F1;
	case XKB_KEY_F2: return OnlyWayUi::Input::KI_F2;
	case XKB_KEY_F3: return OnlyWayUi::Input::KI_F3;
	case XKB_KEY_F4: return OnlyWayUi::Input::KI_F4;
	case XKB_KEY_F5: return OnlyWayUi::Input::KI_F5;
	case XKB_KEY_F6: return OnlyWayUi::Input::KI_F6;
	case XKB_KEY_F7: return OnlyWayUi::Input::KI_F7;
	case XKB_KEY_F8: return OnlyWayUi::Input::KI_F8;
	case XKB_KEY_F9: return OnlyWayUi::Input::KI_F9;
	case XKB_KEY_F10: return OnlyWayUi::Input::KI_F10;
	case XKB_KEY_F11: return OnlyWayUi::Input::KI_F11;
	case XKB_KEY_F12: return OnlyWayUi::Input::KI_F12;
	case XKB_KEY_F13: return OnlyWayUi::Input::KI_F13;
	case XKB_KEY_F14: return OnlyWayUi::Input::KI_F14;
	case XKB_KEY_F15: return OnlyWayUi::Input::KI_F15;
	case XKB_KEY_F16: return OnlyWayUi::Input::KI_F16;
	case XKB_KEY_F17: return OnlyWayUi::Input::KI_F17;
	case XKB_KEY_F18: return OnlyWayUi::Input::KI_F18;
	case XKB_KEY_F19: return OnlyWayUi::Input::KI_F19;
	case XKB_KEY_F20: return OnlyWayUi::Input::KI_F20;
	case XKB_KEY_F21: return OnlyWayUi::Input::KI_F21;
	case XKB_KEY_F22: return OnlyWayUi::Input::KI_F22;
	case XKB_KEY_F23: return OnlyWayUi::Input::KI_F23;
	case XKB_KEY_F24: return OnlyWayUi::Input::KI_F24;

	case XKB_KEY_Shift_L: return OnlyWayUi::Input::KI_LSHIFT;
	case XKB_KEY_Shift_R: return OnlyWayUi::Input::KI_RSHIFT;
	case XKB_KEY_Control_L: return OnlyWayUi::Input::KI_LCONTROL;
	case XKB_KEY_Control_R: return OnlyWayUi::Input::KI_RCONTROL;
	case XKB_KEY_Caps_Lock: return OnlyWayUi::Input::KI_CAPITAL;
	case XKB_KEY_Alt_L: return OnlyWayUi::Input::KI_LMENU;
	case XKB_KEY_Alt_R: return OnlyWayUi::Input::KI_RMENU;
	case XKB_KEY_Super_L: return OnlyWayUi::Input::KI_LWIN;
	case XKB_KEY_Super_R: return OnlyWayUi::Input::KI_RWIN;

	case XKB_KEY_space: return OnlyWayUi::Input::KI_SPACE;
	case XKB_KEY_apostrophe: return OnlyWayUi::Input::KI_OEM_7;
	case XKB_KEY_comma: return OnlyWayUi::Input::KI_OEM_COMMA;
	case XKB_KEY_minus: return OnlyWayUi::Input::KI_OEM_MINUS;
	case XKB_KEY_period: return OnlyWayUi::Input::KI_OEM_PERIOD;
	case XKB_KEY_slash: return OnlyWayUi::Input::KI_OEM_2;
	case XKB_KEY_0: return OnlyWayUi::Input::KI_0;
	case XKB_KEY_1: return OnlyWayUi::Input::KI_1;
	case XKB_KEY_2: return OnlyWayUi::Input::KI_2;
	case XKB_KEY_3: return OnlyWayUi::Input::KI_3;
	case XKB_KEY_4: return OnlyWayUi::Input::KI_4;
	case XKB_KEY_5: return OnlyWayUi::Input::KI_5;
	case XKB_KEY_6: return OnlyWayUi::Input::KI_6;
	case XKB_KEY_7: return OnlyWayUi::Input::KI_7;
	case XKB_KEY_8: return OnlyWayUi::Input::KI_8;
	case XKB_KEY_9: return OnlyWayUi::Input::KI_9;
	case XKB_KEY_semicolon: return OnlyWayUi::Input::KI_OEM_1;
	case XKB_KEY_equal: return OnlyWayUi::Input::KI_OEM_PLUS;
	case XKB_KEY_bracketleft: return OnlyWayUi::Input::KI_OEM_4;
	case XKB_KEY_backslash: return OnlyWayUi::Input::KI_OEM_5;
	case XKB_KEY_bracketright: return OnlyWayUi::Input::KI_OEM_6;
	case XKB_KEY_grave: return OnlyWayUi::Input::KI_OEM_3;
	case XKB_KEY_a: case XKB_KEY_A: return OnlyWayUi::Input::KI_A;
	case XKB_KEY_b: case XKB_KEY_B: return OnlyWayUi::Input::KI_B;
	case XKB_KEY_c: case XKB_KEY_C: return OnlyWayUi::Input::KI_C;
	case XKB_KEY_d: case XKB_KEY_D: return OnlyWayUi::Input::KI_D;
	case XKB_KEY_e: case XKB_KEY_E: return OnlyWayUi::Input::KI_E;
	case XKB_KEY_f: case XKB_KEY_F: return OnlyWayUi::Input::KI_F;
	case XKB_KEY_g: case XKB_KEY_G: return OnlyWayUi::Input::KI_G;
	case XKB_KEY_h: case XKB_KEY_H: return OnlyWayUi::Input::KI_H;
	case XKB_KEY_i: case XKB_KEY_I: return OnlyWayUi::Input::KI_I;
	case XKB_KEY_j: case XKB_KEY_J: return OnlyWayUi::Input::KI_J;
	case XKB_KEY_k: case XKB_KEY_K: return OnlyWayUi::Input::KI_K;
	case XKB_KEY_l: case XKB_KEY_L: return OnlyWayUi::Input::KI_L;
	case XKB_KEY_m: case XKB_KEY_M: return OnlyWayUi::Input::KI_M;
	case XKB_KEY_n: case XKB_KEY_N: return OnlyWayUi::Input::KI_N;
	case XKB_KEY_o: case XKB_KEY_O: return OnlyWayUi::Input::KI_O;
	case XKB_KEY_p: case XKB_KEY_P: return OnlyWayUi::Input::KI_P;
	case XKB_KEY_q: case XKB_KEY_Q: return OnlyWayUi::Input::KI_Q;
	case XKB_KEY_r: case XKB_KEY_R: return OnlyWayUi::Input::KI_R;
	case XKB_KEY_s: case XKB_KEY_S: return OnlyWayUi::Input::KI_S;
	case XKB_KEY_t: case XKB_KEY_T: return OnlyWayUi::Input::KI_T;
	case XKB_KEY_u: case XKB_KEY_U: return OnlyWayUi::Input::KI_U;
	case XKB_KEY_v: case XKB_KEY_V: return OnlyWayUi::Input::KI_V;
	case XKB_KEY_w: case XKB_KEY_W: return OnlyWayUi::Input::KI_W;
	case XKB_KEY_x: case XKB_KEY_X: return OnlyWayUi::Input::KI_X;
	case XKB_KEY_y: case XKB_KEY_Y: return OnlyWayUi::Input::KI_Y;
	case XKB_KEY_z: case XKB_KEY_Z: return OnlyWayUi::Input::KI_Z;
	default: break;
	}
	// clang-format on

	return OnlyWayUi::Input::KI_UNKNOWN;
}

} // namespace OwuiWayland
