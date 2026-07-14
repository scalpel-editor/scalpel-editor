#include "ScintillaOWUI.h"

#include "PlatOWUI.h"

#include <OnlyWayUi/Core/Core.h>
#include <OnlyWayUi/Core/Input.h>
#include <OnlyWayUi/Core/RenderManager.h>
#include <OnlyWayUi/Core/SystemInterface.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <forward_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Scintilla's internal headers depend on this order and are not self-contained.
// clang-format off
#include "ScintillaTypes.h"
#include "ScintillaMessages.h"
#include "ScintillaStructures.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "CharacterType.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "PerLine.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "AutoComplete.h"
#include "CallTip.h"
#include "ScintillaBase.h"
// clang-format on

namespace Scintilla::Internal {

namespace {

	class NullListBox final : public ListBox {
	public:
		void SetFont(const Font*) override {}
		void Create(Window&, int, Point, int, bool, Scintilla::Technology) override {}
		void SetAverageCharWidth(int) override {}
		void SetVisibleRows(int rows) override { visible_rows = rows; }
		int GetVisibleRows() const override { return visible_rows; }
		PRectangle GetDesiredRect() override { return PRectangle(); }
		int CaretFromEdge() override { return 0; }
		void Clear() noexcept override { values.clear(); }
		void Append(char* value, int = -1) override { values.emplace_back(value ? value : ""); }
		int Length() override { return static_cast<int>(values.size()); }
		void Select(int selection) override { selected = selection; }
		int GetSelection() override { return selected; }
		int Find(const char* prefix) override
		{
			const std::string_view match = prefix ? prefix : "";
			for (size_t i = 0; i < values.size(); ++i)
				if (values[i].compare(0, match.size(), match) == 0)
					return static_cast<int>(i);
			return -1;
		}
		std::string GetValue(int index) override { return index >= 0 && static_cast<size_t>(index) < values.size() ? values[index] : std::string(); }
		void RegisterImage(int, const char*) override {}
		void RegisterRGBAImage(int, int, int, const unsigned char*) override {}
		void ClearRegisteredImages() override {}
		void SetDelegate(IListBoxDelegate*) override {}
		void SetList(const char*, char, char) override {}
		void SetOptions(ListOptions) override {}

	private:
		int visible_rows = 5;
		int selected = -1;
		std::vector<std::string> values;
	};

	OnlyWayUi::Editor::SurfaceTarget* Target(WindowID wid) noexcept
	{
		return static_cast<OnlyWayUi::Editor::SurfaceTarget*>(wid);
	}

} // namespace

Window::~Window() noexcept = default;

void Window::Destroy() noexcept
{
	wid = nullptr;
}

PRectangle Window::GetPosition() const
{
	const auto* target = Target(wid);
	return target ? PRectangle(0, 0, target->size.x, target->size.y) : PRectangle();
}

void Window::SetPosition(PRectangle rectangle)
{
	if (auto* target = Target(wid))
		target->size = {static_cast<float>(rectangle.Width()), static_cast<float>(rectangle.Height())};
}

void Window::SetPositionRelative(PRectangle rectangle, const Window*)
{
	SetPosition(rectangle);
}

PRectangle Window::GetClientPosition() const
{
	return GetPosition();
}

void Window::Show(bool) {}

void Window::InvalidateAll()
{
	if (auto* target = Target(wid); target && target->invalidate_rectangle)
		target->invalidate_rectangle(0, 0, target->size.x, target->size.y);
}

void Window::InvalidateRectangle(PRectangle rectangle)
{
	if (auto* target = Target(wid); target && target->invalidate_rectangle)
		target->invalidate_rectangle(static_cast<float>(rectangle.left), static_cast<float>(rectangle.top), static_cast<float>(rectangle.right),
			static_cast<float>(rectangle.bottom));
}

void Window::SetCursor(Cursor cursor)
{
	cursorLast = cursor;
}

PRectangle Window::GetMonitorRect(Point)
{
	return GetPosition();
}

ListBox::ListBox() noexcept = default;
ListBox::~ListBox() noexcept = default;
std::unique_ptr<ListBox> ListBox::Allocate()
{
	return std::make_unique<NullListBox>();
}

Menu::Menu() noexcept : mid(nullptr) {}
void Menu::CreatePopUp() {}
void Menu::Destroy() noexcept
{
	mid = nullptr;
}
void Menu::Show(Point, const Window&) {}

ColourRGBA Platform::Chrome()
{
	return ColourRGBA(0xe0, 0xe0, 0xe0);
}
ColourRGBA Platform::ChromeHighlight()
{
	return white;
}
const char* Platform::DefaultFont()
{
	return "LatoLatin";
}
int Platform::DefaultFontSize()
{
	return 16;
}
unsigned int Platform::DoubleClickTime()
{
	return 500;
}
void Platform::DebugDisplay(const char*) noexcept {}
void Platform::DebugPrintf(const char*, ...) noexcept {}
bool Platform::ShowAssertionPopUps(bool assertion_popups) noexcept
{
	static bool enabled = true;
	return std::exchange(enabled, assertion_popups);
}
void Platform::Assert(const char*, const char*, int) noexcept
{
	std::abort();
}

} // namespace Scintilla::Internal

namespace OnlyWayUi::Editor {

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

	unsigned int ElapsedMilliseconds()
	{
		if (SystemInterface* system = GetSystemInterface())
			return static_cast<unsigned int>(std::max(0.0, system->GetElapsedTime()) * 1000.0);
		return 0;
	}

	double ElapsedSeconds()
	{
		if (SystemInterface* system = GetSystemInterface())
			return std::max(0.0, system->GetElapsedTime());
		return 0.0;
	}

	KeyMod ToKeyMod(int key_modifier_state)
	{
		return ModifierFlags((key_modifier_state & Input::KM_SHIFT) != 0, (key_modifier_state & Input::KM_CTRL) != 0,
			(key_modifier_state & Input::KM_ALT) != 0, (key_modifier_state & Input::KM_META) != 0, false);
	}

	Point ToPoint(Vector2f position)
	{
		return Point(static_cast<XYPOSITION>(std::floor(position.x)), static_cast<XYPOSITION>(std::floor(position.y)));
	}

	// Map OnlyWayUi key identifiers onto Scintilla Keys / ASCII codes used by KeyMap.
	bool MapKey(Input::KeyIdentifier key_identifier, bool num_lock, Keys& out_key)
	{
		switch (key_identifier)
		{
		case Input::KI_DOWN: out_key = Keys::Down; return true;
		case Input::KI_UP: out_key = Keys::Up; return true;
		case Input::KI_LEFT: out_key = Keys::Left; return true;
		case Input::KI_RIGHT: out_key = Keys::Right; return true;
		case Input::KI_HOME: out_key = Keys::Home; return true;
		case Input::KI_END: out_key = Keys::End; return true;
		case Input::KI_PRIOR: out_key = Keys::Prior; return true;
		case Input::KI_NEXT: out_key = Keys::Next; return true;
		case Input::KI_DELETE: out_key = Keys::Delete; return true;
		case Input::KI_INSERT: out_key = Keys::Insert; return true;
		case Input::KI_ESCAPE: out_key = Keys::Escape; return true;
		case Input::KI_BACK: out_key = Keys::Back; return true;
		case Input::KI_TAB: out_key = Keys::Tab; return true;
		case Input::KI_RETURN:
		case Input::KI_NUMPADENTER: out_key = Keys::Return; return true;
		case Input::KI_ADD: out_key = Keys::Add; return true;
		case Input::KI_SUBTRACT: out_key = Keys::Subtract; return true;
		case Input::KI_DIVIDE: out_key = Keys::Divide; return true;
		case Input::KI_LWIN: out_key = Keys::Win; return true;
		case Input::KI_RWIN: out_key = Keys::RWin; return true;
		case Input::KI_APPS: out_key = Keys::Menu; return true;

		// Numpad arrows / navigation when Num Lock is off.
		case Input::KI_NUMPAD2:
			if (!num_lock)
			{
				out_key = Keys::Down;
				return true;
			}
			return false;
		case Input::KI_NUMPAD4:
			if (!num_lock)
			{
				out_key = Keys::Left;
				return true;
			}
			return false;
		case Input::KI_NUMPAD6:
			if (!num_lock)
			{
				out_key = Keys::Right;
				return true;
			}
			return false;
		case Input::KI_NUMPAD8:
			if (!num_lock)
			{
				out_key = Keys::Up;
				return true;
			}
			return false;
		case Input::KI_NUMPAD1:
			if (!num_lock)
			{
				out_key = Keys::End;
				return true;
			}
			return false;
		case Input::KI_NUMPAD3:
			if (!num_lock)
			{
				out_key = Keys::Next;
				return true;
			}
			return false;
		case Input::KI_NUMPAD7:
			if (!num_lock)
			{
				out_key = Keys::Home;
				return true;
			}
			return false;
		case Input::KI_NUMPAD9:
			if (!num_lock)
			{
				out_key = Keys::Prior;
				return true;
			}
			return false;
		case Input::KI_DECIMAL:
			if (!num_lock)
			{
				out_key = Keys::Delete;
				return true;
			}
			return false;

		// Ctrl+letter chords used by Scintilla's default KeyMap (A, C, V, X, Y, Z, ...).
		case Input::KI_A: out_key = static_cast<Keys>('A'); return true;
		case Input::KI_C: out_key = static_cast<Keys>('C'); return true;
		case Input::KI_D: out_key = static_cast<Keys>('D'); return true;
		case Input::KI_L: out_key = static_cast<Keys>('L'); return true;
		case Input::KI_T: out_key = static_cast<Keys>('T'); return true;
		case Input::KI_U: out_key = static_cast<Keys>('U'); return true;
		case Input::KI_V: out_key = static_cast<Keys>('V'); return true;
		case Input::KI_X: out_key = static_cast<Keys>('X'); return true;
		case Input::KI_Y: out_key = static_cast<Keys>('Y'); return true;
		case Input::KI_Z: out_key = static_cast<Keys>('Z'); return true;
		case Input::KI_OEM_4: out_key = static_cast<Keys>('['); return true;
		case Input::KI_OEM_6: out_key = static_cast<Keys>(']'); return true;
		case Input::KI_OEM_2: out_key = static_cast<Keys>('/'); return true;
		case Input::KI_OEM_5: out_key = static_cast<Keys>('\\'); return true;
		default: return false;
		}
	}

	// Matches Editor::TickReason { caret, scroll, widen, dwell, platform }.
	constexpr size_t TickerCount = 5;

} // namespace

class ScintillaOWUI::Impl final : public ScintillaBase {
public:
	Impl(InvalidateCallback invalidate_callback, NotifyCallback notify_callback, void* user_data) :
		notify_callback(notify_callback), notify_user_data(user_data)
	{
		target.invalidate_rectangle = [invalidate_callback, user_data](float left, float top, float right, float bottom) {
			if (invalidate_callback)
				invalidate_callback(user_data, left, top, right, bottom);
		};
		wMain = static_cast<WindowID>(&target);

		WndProc(Message::SetBufferedDraw, 0, 0);
		// These flags also control Scintilla's width tracking. This port creates no native
		// scrollbars, so leave them enabled and mirror their state through ElementScroll.
		WndProc(Message::SetScrollWidth, 1, 0);
		WndProc(Message::SetScrollWidthTracking, 1, 0);
		// Default soft-wrap; apps override through ElementScintilla::SetWordWrap.
		SetWordWrapValue(true);

		// Margin 0 is already Number; hide the default empty symbol margins so only line numbers show.
		WndProc(Message::SetMarginTypeN, 0, static_cast<sptr_t>(MarginType::Number));
		WndProc(Message::SetMarginWidthN, 1, 0);
		WndProc(Message::SetMarginWidthN, 2, 0);
		WndProc(Message::SetMarginLeft, 0, 24);
		ApplyLineNumberStyle();
		UpdateLineNumberWidth();
	}

	~Impl() override { Finalise(); }

	void SetClientSize(Vector2i size)
	{
		const Vector2f new_size(static_cast<float>(std::max(0, size.x)), static_cast<float>(std::max(0, size.y)));
		if (target.size != new_size)
		{
			target.size = new_size;
			ChangeSize();
		}
	}

	bool PaintFull(RenderManager& render_manager, Vector2f origin)
	{
		bool success = false;
		try
		{
			target.render_manager = &render_manager;
			// Origin is the absolute content-box position so scissor and draw space match the
			// window (and any active frame-damage region). Scintilla still uses local coords;
			// PlatOWUI adds origin when drawing and clipping.
			target.origin = origin;
			paintState = PaintState::painting;
			rcPaint = GetClientRectangle();
			paintingAllText = true;

			std::unique_ptr<Surface> surface = Surface::Allocate(Technology::Default);
			surface->Init(static_cast<SurfaceID>(&target), static_cast<WindowID>(&target));
			Paint(surface.get(), rcPaint);
			surface->Release();
			success = true;
		}
		catch (...)
		{
			errorStatus = Status::Failure;
		}

		const bool repaint = success && paintState == PaintState::abandoned;
		paintState = PaintState::notPainting;
		paintingAllText = false;
		target.render_manager = nullptr;
		target.origin = {};
		if (repaint)
			wMain.InvalidateAll();
		return success;
	}

	void SetTextValue(const String& text)
	{
		// Load through the message path so undo history and the save point stay consistent.
		// The buffer stays writable; the fresh content is the new unmodified save point.
		WndProc(Message::ClearAll, 0, 0);
		if (!text.empty())
			WndProc(Message::AddText, text.size(), reinterpret_cast<sptr_t>(text.data()));
		WndProc(Message::EmptyUndoBuffer, 0, 0);
		WndProc(Message::SetSavePoint, 0, 0);
		UpdateLineNumberWidth();
	}

	String GetTextValue()
	{
		const sptr_t length = WndProc(Message::GetTextLength, 0, 0);
		const size_t text_length = static_cast<size_t>(std::max<sptr_t>(0, length));
		String result(text_length + 1, '\0');
		WndProc(Message::GetText, static_cast<uptr_t>(result.size()), reinterpret_cast<sptr_t>(result.data()));
		result.resize(text_length);
		return result;
	}

	DocumentHandle CreateDocumentValue()
	{
		return reinterpret_cast<DocumentHandle>(WndProc(Message::CreateDocument, 0, static_cast<sptr_t>(DocumentOption::Default)));
	}

	void SetDocumentValue(DocumentHandle document)
	{
		// Clipboard reads may finish later from the Wayland event loop. Changing documents
		// invalidates a paste started in the previous document so it cannot edit this one.
		++document_generation;
		WndProc(Message::SetDocPointer, 0, reinterpret_cast<sptr_t>(document));
		UpdateLineNumberWidth();
	}

	void ReleaseDocumentValue(DocumentHandle document)
	{
		if (document)
			WndProc(Message::ReleaseDocument, 0, reinterpret_cast<sptr_t>(document));
	}

	void SetDocumentSavePointValue(DocumentHandle document)
	{
		if (document)
			static_cast<Document*>(static_cast<IDocumentEditable*>(document))->SetSavePoint();
	}

	void SetPixelsPerInchValue(int pixels_per_inch)
	{
		const int new_value = std::max(1, pixels_per_inch);
		if (target.pixels_per_inch != new_value)
		{
			target.pixels_per_inch = new_value;
			InvalidateStyleRedraw();
			UpdateLineNumberWidth();
		}
	}

	void SetFontValue(const String& family, int size)
	{
		constexpr uptr_t default_style = static_cast<uptr_t>(StylesCommon::Default);
		WndProc(Message::StyleSetFont, default_style, reinterpret_cast<sptr_t>(family.c_str()));
		WndProc(Message::StyleSetSize, default_style, std::max(1, size));
		// StyleClearAll copies the default style onto every style, then forces StyleLineNumber.back
		// to Platform::Chrome(); re-apply line-number colours and remeasure the margin.
		WndProc(Message::StyleClearAll, 0, 0);
		ApplyLineNumberStyle();
		UpdateLineNumberWidth();
	}

	void SetDarkThemeValue(bool dark)
	{
		dark_theme = dark;
		constexpr uptr_t default_style = static_cast<uptr_t>(StylesCommon::Default);
		const sptr_t foreground = dark ? 0x00f4f4f4 : 0x00221c1a;
		const sptr_t background = dark ? 0x001e1815 : 0x00f5f5f5;
		WndProc(Message::StyleSetFore, default_style, foreground);
		WndProc(Message::StyleSetBack, default_style, background);
		WndProc(Message::StyleClearAll, 0, 0);
		WndProc(Message::SetCaretFore, 0, dark ? 0x00ffffff : 0x00000000);
		ApplyLineNumberStyle();
		UpdateLineNumberWidth();
	}

	// SCI_SETWRAPMODE / Message::SetWrapMode: Word soft-wraps; None keeps one display line per doc line.
	void SetWordWrapValue(bool word_wrap)
	{
		const Wrap mode = word_wrap ? Wrap::Word : Wrap::None;
		WndProc(Message::SetWrapMode, static_cast<uptr_t>(mode), 0);
		PublishScrollState(true);
	}

	bool GetWordWrapValue()
	{
		return static_cast<Wrap>(WndProc(Message::GetWrapMode, 0, 0)) != Wrap::None;
	}

	int GetLineCountValue()
	{
		const sptr_t length = WndProc(Message::GetLength, 0, 0);
		if (length <= 0)
			return 0;
		sptr_t lines = WndProc(Message::GetLineCount, 0, 0);
		// Scintilla counts the empty line after a trailing newline as its own line; the status area
		// has always reported one fewer, matching the line the text ends on.
		if (WndProc(Message::GetCharAt, length - 1, 0) == '\n')
			lines -= 1;
		return static_cast<int>(std::max<sptr_t>(0, lines));
	}

	int GetLineNumberMarginWidthValue()
	{
		return vs.fixedColumnWidth;
	}

	int GetCharacterCountValue()
	{
		const sptr_t length = WndProc(Message::GetLength, 0, 0);
		return static_cast<int>(std::max<sptr_t>(0, WndProc(Message::CountCharacters, 0, length)));
	}

	void ButtonDown(int button, Vector2f position, int key_modifier_state)
	{
		const Point pt = ToPoint(position);
		const unsigned int time = ElapsedMilliseconds();
		const KeyMod modifiers = ToKeyMod(key_modifier_state);
		if (button == 0)
			ButtonDownWithModifiers(pt, time, modifiers);
		else if (button == 1)
			RightButtonDownWithModifiers(pt, time, modifiers);
	}

	void ButtonMove(Vector2f position, int key_modifier_state)
	{
		ButtonMoveWithModifiers(ToPoint(position), ElapsedMilliseconds(), ToKeyMod(key_modifier_state));
	}

	void ButtonUp(int button, Vector2f position, int key_modifier_state)
	{
		if (button != 0)
			return;
		if (!HaveMouseCapture())
			return;
		ButtonUpWithModifiers(ToPoint(position), ElapsedMilliseconds(), ToKeyMod(key_modifier_state));
	}

	bool KeyDown(int key_identifier, int key_modifier_state)
	{
		Keys key{};
		const bool num_lock = (key_modifier_state & Input::KM_NUMLOCK) != 0;
		if (!MapKey(static_cast<Input::KeyIdentifier>(key_identifier), num_lock, key))
			return false;

		bool consumed = false;
		const int result = KeyDownWithModifiers(key, ToKeyMod(key_modifier_state), &consumed);
		return consumed || result != 0;
	}

	void TextInput(const String& text)
	{
		if (text.empty())
			return;
		// Enter is handled by KeyDown → Keys::Return → NewLine(), which inserts the document
		// EOL mode string once. The Wayland backend also emits textinput '\n' for Return (and
		// the same pair on key-repeat); ignore bare line endings here so they are not inserted
		// a second time. Multi-character text that happens to contain newlines is left alone.
		if (text == "\n" || text == "\r" || text == "\r\n")
			return;
		InsertCharacter(std::string_view(text.data(), text.size()), CharacterSource::DirectInput);
	}

	void MouseWheel(Vector2f wheel_delta, int key_modifier_state)
	{
		const KeyMod modifiers = ToKeyMod(key_modifier_state);
		// Positive wheel_delta is right/down in OnlyWayUi, matching Scintilla's topLine/xOffset growth.
		if (FlagSet(modifiers, KeyMod::Ctrl))
		{
			if (wheel_delta.y < 0 || wheel_delta.x < 0)
				KeyCommand(Message::ZoomIn);
			else if (wheel_delta.y > 0 || wheel_delta.x > 0)
				KeyCommand(Message::ZoomOut);
			return;
		}

		constexpr float lines_per_unit = 3.f;
		constexpr float pixels_per_unit = 40.f;
		if (FlagSet(modifiers, KeyMod::Shift) || std::abs(wheel_delta.x) > std::abs(wheel_delta.y))
		{
			const float amount = std::abs(wheel_delta.x) > std::abs(wheel_delta.y) ? wheel_delta.x : wheel_delta.y;
			HorizontalScrollTo(xOffset + static_cast<int>(std::lround(amount * pixels_per_unit)));
		}
		else
		{
			ScrollTo(topLine + static_cast<Sci::Line>(std::lround(wheel_delta.y * lines_per_unit)));
		}
	}

	void SetFocusValue(bool focused) { SetFocusState(focused); }

	void TickTimers()
	{
		const double now = ElapsedSeconds();
		for (size_t i = 0; i < TickerCount; ++i)
		{
			auto& ticker = tickers[i];
			// Cap catches-up so a long pause cannot flood TickFor.
			int fires = 0;
			while (ticker.running && now >= ticker.next_fire && fires < 8)
			{
				ticker.next_fire += static_cast<double>(ticker.period_ms) / 1000.0;
				++fires;
				TickFor(static_cast<TickReason>(i));
			}
			if (ticker.running && now >= ticker.next_fire)
				ticker.next_fire = now + static_cast<double>(ticker.period_ms) / 1000.0;
		}
	}

	double SecondsUntilNextTimer() const
	{
		const double now = ElapsedSeconds();
		double soonest = -1.0;
		for (const auto& ticker : tickers)
		{
			if (!ticker.running)
				continue;
			const double remaining = std::max(0.0, ticker.next_fire - now);
			if (soonest < 0.0 || remaining < soonest)
				soonest = remaining;
		}
		return soonest;
	}

	bool HasMouseCaptureValue() const { return mouse_capture; }

	void SetScrollCallbackValue(ScrollCallback callback, void* user_data)
	{
		scroll_callback = callback;
		scroll_user_data = user_data;
		PublishScrollState(true);
	}

	void SetScrollPositionValue(Vector2f position)
	{
		// Round to the nearest line without forcing a move when the request lands on the
		// current line. Scrollbar thumb drags repeatedly request in-between pixel positions,
		// and forcing a one-line move on each request makes the view bounce up and down.
		const int line_height = std::max(1, vs.lineHeight);
		const Sci::Line requested_line = static_cast<Sci::Line>(std::llround(position.y / line_height));

		const bool was_setting_scroll_position = setting_scroll_position;
		setting_scroll_position = true;
		ScrollTo(requested_line, false);
		const float horizontal_max = CurrentScrollState().horizontal_max;
		HorizontalScrollTo(static_cast<int>(std::lround(std::clamp(position.x, 0.f, horizontal_max))));
		setting_scroll_position = was_setting_scroll_position;
		if (!setting_scroll_position)
			PublishScrollState(true);
	}

	SurfaceTarget target;

private:
	// Colours are Scintilla RGB integers: R | (G << 8) | (B << 16).
	void ApplyLineNumberStyle()
	{
		constexpr uptr_t line_number = static_cast<uptr_t>(StylesCommon::LineNumber);
		if (dark_theme)
		{
			// Muted digits on a gutter slightly lighter than the dark editor background.
			WndProc(Message::StyleSetFore, line_number, 0x009a9590);
			WndProc(Message::StyleSetBack, line_number, 0x002a2420);
		}
		else
		{
			// Match the sample chrome (#f4f5f7 wrap) with subdued digit colour.
			WndProc(Message::StyleSetFore, line_number, 0x008a7f6b);
			WndProc(Message::StyleSetBack, line_number, 0x00f7f5f4);
		}
	}

	void UpdateLineNumberWidth()
	{
		const sptr_t line_count = std::max<sptr_t>(1, WndProc(Message::GetLineCount, 0, 0));
		int digits = 1;
		for (sptr_t n = line_count; n >= 10; n /= 10)
			++digits;
		// At least two digits so single-digit files do not look cramped.
		digits = std::max(2, digits);

		const std::string probe(static_cast<size_t>(digits), '9');
		const sptr_t text_width = WndProc(Message::TextWidth, static_cast<uptr_t>(StylesCommon::LineNumber),
			reinterpret_cast<sptr_t>(probe.c_str()));
		// Scintilla already right-pads by marginNumberPadding; add a little left breathing room.
		constexpr int extra_padding = 8;
		const int width = static_cast<int>(std::max<sptr_t>(1, text_width)) + extra_padding;
		WndProc(Message::SetMarginWidthN, 0, width);
		// SetMarginWidthN invalidates style data, but line-number placement uses the derived
		// fixedColumnWidth. Refresh it now so a cached-pane repaint cannot use the old width.
		RefreshStyleData();
	}

	ScrollState CurrentScrollState() const
	{
		const int line_height = std::max(1, vs.lineHeight);
		const float client_width = std::max(0.f, target.size.x);
		ScrollState state;
		state.word_wrap = Wrapping();
		state.horizontal_max = state.word_wrap ? 0.f : std::max(0.f, static_cast<float>(scrollWidth) - client_width);
		state.vertical_max = static_cast<float>(MaxScrollPos() * line_height);
		state.left = state.word_wrap ? 0.f : static_cast<float>(xOffset);
		state.top = static_cast<float>(topLine * line_height);
		return state;
	}

	void PublishScrollState(bool force = false)
	{
		if (!scroll_callback || setting_scroll_position)
			return;
		const ScrollState state = CurrentScrollState();
		if (!force && last_published_scroll_state && state.horizontal_max == last_published_scroll_state->horizontal_max &&
			state.vertical_max == last_published_scroll_state->vertical_max && state.left == last_published_scroll_state->left &&
			state.top == last_published_scroll_state->top && state.word_wrap == last_published_scroll_state->word_wrap)
			return;
		last_published_scroll_state = state;
		scroll_callback(scroll_user_data, state);
	}

	void Initialise() override {}
	sptr_t DefWndProc(Message, uptr_t, sptr_t) override { return 0; }
	void SetVerticalScrollPos() override
	{
		Editor::SetVerticalScrollPos();
		PublishScrollState();
	}
	void SetHorizontalScrollPos() override { PublishScrollState(); }
	bool ModifyScrollBars(Sci::Line, Sci::Line) override
	{
		const ScrollState state = CurrentScrollState();
		const bool range_changed = !last_scroll_range || state.horizontal_max != last_scroll_range->x || state.vertical_max != last_scroll_range->y;
		last_scroll_range = Vector2f(state.horizontal_max, state.vertical_max);

		if (!state.word_wrap && state.left > state.horizontal_max)
		{
			const bool was_setting_scroll_position = setting_scroll_position;
			setting_scroll_position = true;
			HorizontalScrollTo(static_cast<int>(std::lround(state.horizontal_max)));
			setting_scroll_position = was_setting_scroll_position;
		}
		PublishScrollState();
		return range_changed;
	}
	void Copy() override
	{
		if (sel.Empty())
			return;
		SelectionText selected;
		CopySelectionRange(&selected);
		CopyToClipboard(selected);
	}
	void Paste() override
	{
		SystemInterface* system = GetSystemInterface();
		if (!system)
			return;
		// OnlyWayUi's clipboard read is asynchronous on Wayland, so the insert runs in the
		// callback rather than inline. The clipboard contract guarantees the callback fires
		// exactly once; empty text means an empty clipboard, a failed read, a superseded
		// request, or backend teardown, none of which should touch the document. The weak
		// token guards against the control being destroyed before an async read completes.
		std::weak_ptr<int> alive = paste_lifetime;
		const uint64_t paste_document_generation = document_generation;
		system->RequestClipboardText([this, alive, paste_document_generation](String text) {
			if (alive.expired() || paste_document_generation != document_generation || text.empty())
				return;
			UndoGroup ug(pdoc);
			ClearSelection(multiPasteMode == MultiPaste::Each);
			InsertPasteShape(std::string_view(text.data(), text.size()), PasteShape::stream);
			EnsureCaretVisible();
			Redraw();
		});
	}
	void ClaimSelection() override {}
	void NotifyChange() override {}
	void NotifyParent(NotificationData scn) override
	{
		switch (scn.nmhdr.code)
		{
		case Notification::SavePointReached: EmitNotice(ScintillaOWUI::Notice::SavePointReached); break;
		case Notification::SavePointLeft: EmitNotice(ScintillaOWUI::Notice::SavePointLeft); break;
		case Notification::Modified:
			if (FlagSet(scn.modificationType, ModificationFlags::InsertText) || FlagSet(scn.modificationType, ModificationFlags::DeleteText))
			{
				if (scn.linesAdded != 0)
					UpdateLineNumberWidth();
				EmitNotice(ScintillaOWUI::Notice::TextChanged);
			}
			break;
		default: break;
		}
	}
	void CopyToClipboard(const SelectionText& selected_text) override
	{
		if (SystemInterface* system = GetSystemInterface())
		{
			const std::string_view text = selected_text.AsView();
			system->SetClipboardText(String(text.data(), text.size()));
		}
	}
	void SetMouseCapture(bool capture) override { mouse_capture = capture; }
	bool HaveMouseCapture() override { return mouse_capture; }
	void CreateCallTipWindow(PRectangle) override {}
	void AddToPopUp(const char*, int, bool) override {}

	bool FineTickerRunning(TickReason reason) override
	{
		const size_t index = static_cast<size_t>(reason);
		return index < TickerCount && tickers[index].running;
	}

	void FineTickerStart(TickReason reason, int millis, int /*tolerance*/) override
	{
		FineTickerCancel(reason);
		const size_t index = static_cast<size_t>(reason);
		if (index >= TickerCount)
			return;
		const int period = std::max(1, millis);
		tickers[index].running = true;
		tickers[index].period_ms = period;
		tickers[index].next_fire = ElapsedSeconds() + static_cast<double>(period) / 1000.0;
	}

	void FineTickerCancel(TickReason reason) override
	{
		const size_t index = static_cast<size_t>(reason);
		if (index < TickerCount)
			tickers[index].running = false;
	}

	void EmitNotice(ScintillaOWUI::Notice notice)
	{
		if (notify_callback)
			notify_callback(notify_user_data, notice);
	}

	struct FineTickerState {
		bool running = false;
		int period_ms = 0;
		double next_fire = 0.0;
	};

	NotifyCallback notify_callback = nullptr;
	void* notify_user_data = nullptr;
	ScrollCallback scroll_callback = nullptr;
	void* scroll_user_data = nullptr;
	std::optional<ScrollState> last_published_scroll_state;
	std::optional<Vector2f> last_scroll_range;
	bool setting_scroll_position = false;
	bool mouse_capture = false;
	bool dark_theme = false;
	std::array<FineTickerState, TickerCount> tickers{};
	uint64_t document_generation = 0;
	// Marks this control alive for asynchronous clipboard-paste callbacks; expires on destroy.
	std::shared_ptr<int> paste_lifetime = std::make_shared<int>(0);
};

ScintillaOWUI::ScintillaOWUI(InvalidateCallback invalidate_callback, NotifyCallback notify_callback, void* user_data) :
	impl(std::make_unique<Impl>(invalidate_callback, notify_callback, user_data))
{}

ScintillaOWUI::~ScintillaOWUI() = default;

void ScintillaOWUI::SetClientSize(Vector2i size)
{
	impl->SetClientSize(size);
}

void ScintillaOWUI::SetScrollCallback(ScrollCallback scroll_callback, void* user_data)
{
	impl->SetScrollCallbackValue(scroll_callback, user_data);
}

void ScintillaOWUI::SetScrollPosition(Vector2f position)
{
	impl->SetScrollPositionValue(position);
}

void ScintillaOWUI::SetPixelsPerInch(int pixels_per_inch)
{
	impl->SetPixelsPerInchValue(pixels_per_inch);
}

bool ScintillaOWUI::Paint(RenderManager& render_manager, Vector2f origin)
{
	return impl->PaintFull(render_manager, origin);
}
void ScintillaOWUI::InvalidateAll()
{
	impl->target.invalidate_rectangle(0, 0, impl->target.size.x, impl->target.size.y);
}
void ScintillaOWUI::SetText(const String& text)
{
	impl->SetTextValue(text);
}
String ScintillaOWUI::GetText()
{
	return impl->GetTextValue();
}
ScintillaOWUI::DocumentHandle ScintillaOWUI::CreateDocument()
{
	return impl->CreateDocumentValue();
}
void ScintillaOWUI::SetDocument(DocumentHandle document)
{
	impl->SetDocumentValue(document);
}
void ScintillaOWUI::ReleaseDocument(DocumentHandle document)
{
	impl->ReleaseDocumentValue(document);
}
void ScintillaOWUI::SetDocumentSavePoint(DocumentHandle document)
{
	impl->SetDocumentSavePointValue(document);
}
void ScintillaOWUI::SetFont(const String& family, int size)
{
	impl->SetFontValue(family, size);
}
void ScintillaOWUI::SetDarkTheme(bool dark)
{
	impl->SetDarkThemeValue(dark);
}
void ScintillaOWUI::SetWordWrap(bool word_wrap)
{
	impl->SetWordWrapValue(word_wrap);
}
bool ScintillaOWUI::GetWordWrap()
{
	return impl->GetWordWrapValue();
}
int ScintillaOWUI::GetLineCount()
{
	return impl->GetLineCountValue();
}
int ScintillaOWUI::GetLineNumberMarginWidth()
{
	return impl->GetLineNumberMarginWidthValue();
}
int ScintillaOWUI::GetCharacterCount()
{
	return impl->GetCharacterCountValue();
}

void ScintillaOWUI::ButtonDown(int button, Vector2f position, int key_modifier_state)
{
	impl->ButtonDown(button, position, key_modifier_state);
}
void ScintillaOWUI::ButtonMove(Vector2f position, int key_modifier_state)
{
	impl->ButtonMove(position, key_modifier_state);
}
void ScintillaOWUI::ButtonUp(int button, Vector2f position, int key_modifier_state)
{
	impl->ButtonUp(button, position, key_modifier_state);
}
bool ScintillaOWUI::KeyDown(int key_identifier, int key_modifier_state)
{
	return impl->KeyDown(key_identifier, key_modifier_state);
}
void ScintillaOWUI::TextInput(const String& text)
{
	impl->TextInput(text);
}
void ScintillaOWUI::MouseWheel(Vector2f wheel_delta, int key_modifier_state)
{
	impl->MouseWheel(wheel_delta, key_modifier_state);
}
void ScintillaOWUI::SetFocus(bool focused)
{
	impl->SetFocusValue(focused);
}
void ScintillaOWUI::TickTimers()
{
	impl->TickTimers();
}
double ScintillaOWUI::SecondsUntilNextTimer() const
{
	return impl->SecondsUntilNextTimer();
}
bool ScintillaOWUI::HasMouseCapture() const
{
	return impl->HasMouseCaptureValue();
}

} // namespace OnlyWayUi::Editor
