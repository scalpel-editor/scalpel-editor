#pragma once

#include <OnlyWayUi/Config/Config.h>
#include <OnlyWayUi/Core/BasicTypes.h>
#include <OnlyWayUi/Core/Vector2.h>
#include <memory>

namespace OnlyWayUi {

class RenderManager;

namespace Editor {

	class ScintillaOWUI {
	public:
		using DocumentHandle = void*;
		using InvalidateCallback = void (*)(void* user_data, float left, float top, float right, float bottom);
		struct ScrollState {
			float horizontal_max = 0;
			float vertical_max = 0;
			float left = 0;
			float top = 0;
			bool word_wrap = true;
		};
		using ScrollCallback = void (*)(void* user_data, const ScrollState& state);

		// Notices Scintilla raises that the host chrome cares about: save-point moves drive the
		// dirty flag, text changes drive the line/char status.
		enum class Notice {
			SavePointReached, // buffer matches its last saved state (clean)
			SavePointLeft,    // buffer edited away from its saved state (dirty)
			TextChanged,      // text inserted or deleted
		};
		using NotifyCallback = void (*)(void* user_data, Notice notice);

		ScintillaOWUI(InvalidateCallback invalidate_callback, NotifyCallback notify_callback, void* user_data);
		~ScintillaOWUI();

		ScintillaOWUI(const ScintillaOWUI&) = delete;
		ScintillaOWUI& operator=(const ScintillaOWUI&) = delete;

		void SetClientSize(Vector2i size);
		// Registration is separate from construction because Scintilla updates its scrollbar state
		// while its platform object is still being initialized.
		void SetScrollCallback(ScrollCallback scroll_callback, void* user_data);
		void SetScrollPosition(Vector2f position);
		void SetPixelsPerInch(int pixels_per_inch);
		// Paint into the active render target. `origin` is the absolute top-left of the
		// editor content box in the same space as RenderManager scissors (window pixels).
		bool Paint(RenderManager& render_manager, Vector2f origin);
		void InvalidateAll();

		void SetText(const String& text);
		String GetText();
		// Each tab retains one Scintilla document so its text, save point, and undo history survive tab switches.
		DocumentHandle CreateDocument();
		void SetDocument(DocumentHandle document);
		void ReleaseDocument(DocumentHandle document);
		void SetDocumentSavePoint(DocumentHandle document);
		void SetFont(const String& family, int size);
		void SetDarkTheme(bool dark);
		// Soft-wrap at word boundaries when true (default); no wrap when false. SCI_SETWRAPMODE Word/None.
		void SetWordWrap(bool word_wrap);
		bool GetWordWrap();
		// Lines for the status area. A trailing newline is not counted as an extra empty line.
		int GetLineCount();
		// Effective pixel width used to lay out and paint the line-number margin.
		int GetLineNumberMarginWidth();
		// UTF-8 code points in the buffer.
		int GetCharacterCount();

		void ButtonDown(int button, Vector2f position, int key_modifier_state);
		void ButtonMove(Vector2f position, int key_modifier_state);
		void ButtonUp(int button, Vector2f position, int key_modifier_state);
		// Returns true when Scintilla handled the key.
		bool KeyDown(int key_identifier, int key_modifier_state);
		void TextInput(const String& text);
		void MouseWheel(Vector2f wheel_delta, int key_modifier_state);
		void SetFocus(bool focused);

		// Drive caret blink, auto-scroll, and other Scintilla timers.
		void TickTimers();
		// Seconds until the next timer fires, or a negative value when none are running.
		double SecondsUntilNextTimer() const;
		bool HasMouseCapture() const;

	private:
		class Impl;
		std::unique_ptr<Impl> impl;
	};

} // namespace Editor
} // namespace OnlyWayUi
