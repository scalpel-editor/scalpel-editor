#pragma once

#include "ElementScintilla.h"
#include <OnlyWayUi/Config/Config.h>
#include <OnlyWayUi/Core/ElementDocument.h>
#include <OnlyWayUi/Core/Event.h>
#include <OnlyWayUi/Core/EventListener.h>
#include <filesystem>

namespace OnlyWayUi {
class Context;
class Element;
} // namespace OnlyWayUi

class TextEditorWindow : public OnlyWayUi::EventListener, public OnlyWayUi::Editor::ElementScintilla::Observer {
public:
	bool Initialize(OnlyWayUi::Context* context, const char* initial_path);
	void Shutdown();

	OnlyWayUi::ElementDocument* GetDocument() const;

	// True when any open document has unsaved changes. Syncs the active editor first, so edits not yet committed by an
	// onchange still count. Used to decide whether a window-close request needs confirmation.
	bool HasUnsavedChanges();
	// Shows the close-confirmation modal. Call when the compositor requests a close and there is unsaved work.
	void ShowCloseConfirmation();

	void HandleAction(const OnlyWayUi::String& action, OnlyWayUi::Element* element, OnlyWayUi::Event& event);
	void ProcessEvent(OnlyWayUi::Event& event) override;

	// Editor observer: Scintilla drives the active document's dirty flag and status counts.
	void OnEditorDirtyChanged(bool dirty) override;
	void OnEditorTextChanged() override;

private:
	enum class Theme { Light, Dark };
	enum class FontFamily { SystemUi, SansSerif, Serif, Monospace };

	struct Document {
		int id = 0;
		OnlyWayUi::String text;
		std::filesystem::path path;
		OnlyWayUi::Editor::ElementScintilla::DocumentHandle editor_document = nullptr;
		bool editor_document_initialized = false;
		bool dirty = false;
	};

	void CreateDocument(OnlyWayUi::String text, std::filesystem::path path, OnlyWayUi::String status);
	void OpenDocumentFromPath(const std::filesystem::path& path);
	// Opens the native file chooser (through the backend) and, when the user picks a file, opens it. Falls back to a status
	// message when no desktop portal is available so the sample still explains why nothing happened.
	void RequestOpenDialog();
	// Opens the native save chooser for the active document; on confirmation saves that document (found again by id, since
	// the async result can arrive after the user switched or closed tabs) to the chosen path.
	void RequestSaveAsDialog();
	void SaveActiveDocument();
	void SaveDocumentIdAs(int id, const std::filesystem::path& path);
	bool SaveAllDocuments();
	void CloseActiveDocument(bool discard);
	void CloseDocumentById(int id, bool discard);
	void ActivateDocumentById(int id);

	void HideCloseConfirmation();

	// Switches the editor palette. Toggles the "theme-dark" class on the body (Light is the default, so it carries no
	// class) and moves the active-theme mark to the matching View menu item.
	void ApplyTheme(Theme theme);
	// Soft-wraps long lines in the editor pane (View → Word Wrap) and updates the menu tick.
	void ApplyWordWrap(bool word_wrap);
	// Switches the editor textarea to one of the supported generic font families and moves the active-font mark to the
	// matching Font menu item.
	void ApplyFontFamily(FontFamily font_family);
	void AdjustEditorFontSize(int delta);
	void ResetEditorFontSize();
	void ApplyEditorFontSize(int font_size);

	void StoreEditorText();
	void ApplyActiveDocumentToView();
	void RefreshTabs();
	void RefreshStatus(const OnlyWayUi::String& status);
	// Rebuilds the line/char status from the editor's own counts, for the active document.
	void RefreshEditorStatus();
	void FocusEditor();
	// Local directory to open the file chooser in: the active document's folder, or empty to let the portal decide.
	OnlyWayUi::String ActiveDocumentFolder() const;

	Document* ActiveDocument();
	const Document* ActiveDocument() const;
	Document* FindDocumentById(int id);
	Document* FindDocumentByPath(const std::filesystem::path& path);

	OnlyWayUi::Element* GetElement(const OnlyWayUi::String& id) const;
	OnlyWayUi::Editor::ElementScintilla* GetEditorControl() const;

	static std::filesystem::path NormalizePath(const std::filesystem::path& path);
	static OnlyWayUi::String ReadTextFile(const std::filesystem::path& path, bool& ok, OnlyWayUi::String& error);
	static bool WriteTextFile(const std::filesystem::path& path, const OnlyWayUi::String& text, OnlyWayUi::String& error);
	static OnlyWayUi::String DocumentTitle(const Document& document);

	OnlyWayUi::Context* context = nullptr;
	OnlyWayUi::ElementDocument* document = nullptr;
	OnlyWayUi::Vector<Document> documents;
	int active_document_id = 0;
	int next_document_id = 1;
	bool applying_editor_value = false;
	Theme current_theme = Theme::Light;
	bool current_word_wrap = true;
	FontFamily current_font_family = FontFamily::Monospace;
	int current_editor_font_size = 16;
};
