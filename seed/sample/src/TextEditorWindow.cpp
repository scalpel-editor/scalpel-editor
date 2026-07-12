#include "TextEditorWindow.h"
#include "ElementScintilla.h"
#include <OnlyWayUi/Core/Context.h>
#include <OnlyWayUi/Core/Element.h>
#include <OnlyWayUi/Core/Input.h>
#include <OnlyWayUi/Core/Math.h>
#include <OnlyWayUi/Core/StringUtilities.h>
#include <OnlyWayUi/Core/VariantConvert.h>
#include <OnlyWayUi_Backend.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace OnlyWayUi;

namespace {

bool StartsWith(const String& value, const char* prefix)
{
	return value.rfind(prefix, 0) == 0;
}

int ParseIdAfter(const String& value, const char* prefix)
{
	if (!StartsWith(value, prefix))
		return 0;

	return std::atoi(value.c_str() + std::strlen(prefix));
}

String PathToString(const std::filesystem::path& path)
{
	return path.empty() ? String() : path.string();
}

// Filters offered in the open/save dialog. The first is the default selection; "All files" lets the user reach anything.
Vector<Backend::FileDialogFilter> DialogFilters()
{
	return {
		{"Text files", {"*.txt", "*.md", "*.rml", "*.rcss", "*.css", "*.xml", "*.json"}},
		{"All files", {"*"}},
	};
}

constexpr int DefaultEditorFontSize = 16;
constexpr int MinEditorFontSize = 10;
constexpr int MaxEditorFontSize = 32;
constexpr int EditorFontSizeStep = 2;

} // namespace

bool TextEditorWindow::Initialize(Context* context, const char* initial_path)
{
	this->context = context;
	document = context->LoadDocument("basic/text_editor/data/text_editor.rml");
	if (!document)
		return false;

	document->Show();

	// Watch the editor so Scintilla's own change tracking drives the dirty flag and status.
	if (Editor::ElementScintilla* editor = GetEditorControl())
		editor->SetObserver(this);

	// Sync the palette class and the View-menu marks with the defaults.
	ApplyTheme(current_theme);
	ApplyWordWrap(current_word_wrap);
	ApplyFontFamily(current_font_family);
	ApplyEditorFontSize(current_editor_font_size);

	if (initial_path && initial_path[0] != '\0')
	{
		OpenDocumentFromPath(initial_path);
		if (documents.empty())
			CreateDocument("", {}, "Ready");
	}
	else
	{
		CreateDocument("", {}, "Ready");
	}

	FocusEditor();
	return true;
}

void TextEditorWindow::Shutdown()
{
	// Stop editor callbacks before the control is torn down with the document.
	if (Editor::ElementScintilla* editor = GetEditorControl())
	{
		editor->SetObserver(nullptr);
		for (Document& open_document : documents)
		{
			editor->ReleaseDocument(open_document.editor_document);
			open_document.editor_document = nullptr;
		}
	}
	if (document)
		document->Close();
	document = nullptr;
	context = nullptr;
	documents.clear();
}

ElementDocument* TextEditorWindow::GetDocument() const
{
	return document;
}

void TextEditorWindow::HandleAction(const String& action, Element* /*element*/, Event& event)
{
	if (action == "new_document")
	{
		StoreEditorText();
		CreateDocument("", {}, "Started a new document");
		event.StopPropagation();
	}
	else if (action == "open_document")
	{
		RequestOpenDialog();
		event.StopPropagation();
	}
	else if (action == "save_document")
	{
		SaveActiveDocument();
		event.StopPropagation();
	}
	else if (action == "save_as_document")
	{
		RequestSaveAsDialog();
		event.StopPropagation();
	}
	else if (action == "close_document")
	{
		CloseActiveDocument(false);
		event.StopPropagation();
	}
	else if (action == "discard_document")
	{
		CloseActiveDocument(true);
		event.StopPropagation();
	}
	else if (StartsWith(action, "activate_tab:"))
	{
		ActivateDocumentById(ParseIdAfter(action, "activate_tab:"));
		event.StopPropagation();
	}
	else if (StartsWith(action, "close_tab:"))
	{
		CloseDocumentById(ParseIdAfter(action, "close_tab:"), false);
		event.StopPropagation();
	}
	else if (action == "confirm_close_save")
	{
		// Only exit once every document is on disk; if a document has no path or a write fails, keep the window open so the
		// user can resolve it.
		if (SaveAllDocuments())
			Backend::RequestExit();
		else
			HideCloseConfirmation();
		event.StopPropagation();
	}
	else if (action == "confirm_close_discard")
	{
		Backend::RequestExit();
		event.StopPropagation();
	}
	else if (action == "confirm_close_cancel")
	{
		HideCloseConfirmation();
		event.StopPropagation();
	}
	else if (action == "theme_light")
	{
		ApplyTheme(Theme::Light);
		event.StopPropagation();
	}
	else if (action == "theme_dark")
	{
		ApplyTheme(Theme::Dark);
		event.StopPropagation();
	}
	else if (action == "toggle_word_wrap")
	{
		ApplyWordWrap(!current_word_wrap);
		event.StopPropagation();
	}
	else if (action == "font_system_ui")
	{
		ApplyFontFamily(FontFamily::SystemUi);
		event.StopPropagation();
	}
	else if (action == "font_sans_serif")
	{
		ApplyFontFamily(FontFamily::SansSerif);
		event.StopPropagation();
	}
	else if (action == "font_serif")
	{
		ApplyFontFamily(FontFamily::Serif);
		event.StopPropagation();
	}
	else if (action == "font_monospace")
	{
		ApplyFontFamily(FontFamily::Monospace);
		event.StopPropagation();
	}
}

void TextEditorWindow::ProcessEvent(Event& event)
{
	if (!(event == EventId::Keydown))
		return;

	const bool ctrl = event.GetParameter("ctrl_key", 0) != 0;
	const bool shift = event.GetParameter("shift_key", 0) != 0;
	if (!ctrl)
		return;

	const Input::KeyIdentifier key_identifier = (Input::KeyIdentifier)event.GetParameter<int>("key_identifier", 0);
	switch (key_identifier)
	{
	case Input::KI_OEM_PLUS:
	case Input::KI_ADD:
		AdjustEditorFontSize(EditorFontSizeStep);
		event.StopPropagation();
		break;
	case Input::KI_OEM_MINUS:
	case Input::KI_SUBTRACT:
		AdjustEditorFontSize(-EditorFontSizeStep);
		event.StopPropagation();
		break;
	case Input::KI_0:
	case Input::KI_NUMPAD0:
		ResetEditorFontSize();
		event.StopPropagation();
		break;
	case Input::KI_N:
		StoreEditorText();
		CreateDocument("", {}, "Started a new document");
		event.StopPropagation();
		break;
	case Input::KI_S:
		if (shift)
			RequestSaveAsDialog();
		else
			SaveActiveDocument();
		event.StopPropagation();
		break;
	case Input::KI_W:
		CloseActiveDocument(false);
		event.StopPropagation();
		break;
	default: break;
	}
}

void TextEditorWindow::CreateDocument(String text, std::filesystem::path path, String status)
{
	Document new_document;
	new_document.id = next_document_id++;
	new_document.text = std::move(text);
	new_document.path = std::move(path);
	new_document.dirty = false;
	if (Editor::ElementScintilla* editor = GetEditorControl())
		new_document.editor_document = editor->CreateDocument();

	documents.push_back(std::move(new_document));
	active_document_id = documents.back().id;

	ApplyActiveDocumentToView();
	RefreshStatus(status);
}

void TextEditorWindow::OpenDocumentFromPath(const std::filesystem::path& requested_path)
{
	if (requested_path.empty())
	{
		RefreshStatus("No file selected");
		return;
	}

	const std::filesystem::path path = NormalizePath(requested_path);
	if (Document* existing = FindDocumentByPath(path))
	{
		StoreEditorText();
		active_document_id = existing->id;
		ApplyActiveDocumentToView();
		RefreshStatus("Switched to " + PathToString(path));
		return;
	}

	bool ok = false;
	String error;
	String text = ReadTextFile(path, ok, error);
	if (!ok)
	{
		RefreshStatus(error);
		return;
	}

	StoreEditorText();
	CreateDocument(std::move(text), path, "Opened " + PathToString(path));
}

void TextEditorWindow::RequestOpenDialog()
{
	StoreEditorText();

	Backend::FileDialogRequest request;
	request.mode = Backend::FileDialogMode::Open;
	request.title = "Open File";
	request.current_folder = ActiveDocumentFolder();
	request.filters = DialogFilters();

	// The dialog runs out of process; the callback fires later, during a ProcessEvents call, on this same thread. Capturing
	// this is safe because the window outlives the event loop, and the backend drops pending dialogs without calling back
	// once it shuts down.
	const bool started = Backend::ShowFileDialog(request, [this](const Backend::FileDialogResult& result) {
		if (result.accepted && !result.paths.empty())
			OpenDocumentFromPath(result.paths.front());
		else
			RefreshStatus("Open cancelled");
	});

	if (!started)
		RefreshStatus("File dialog unavailable; no desktop portal on this session");
}

void TextEditorWindow::RequestSaveAsDialog()
{
	StoreEditorText();
	Document* active = ActiveDocument();
	if (!active)
		return;

	Backend::FileDialogRequest request;
	request.mode = Backend::FileDialogMode::Save;
	request.title = "Save File As";
	request.suggested_name = active->path.empty() ? String("untitled.txt") : active->path.filename().string();
	request.current_folder = ActiveDocumentFolder();
	request.filters = DialogFilters();

	// Save the document the user chose "Save As" on, not whatever is active when the async result arrives; tabs can change
	// in between, so find it again by id.
	const int target_id = active->id;
	const bool started = Backend::ShowFileDialog(request, [this, target_id](const Backend::FileDialogResult& result) {
		if (result.accepted && !result.paths.empty())
			SaveDocumentIdAs(target_id, result.paths.front());
		else
			RefreshStatus("Save cancelled");
	});

	if (!started)
		RefreshStatus("File dialog unavailable; no desktop portal on this session");
}

void TextEditorWindow::SaveActiveDocument()
{
	StoreEditorText();
	Document* active = ActiveDocument();
	if (!active)
		return;

	// An untitled document has no path yet, so saving it needs the dialog to pick one.
	if (active->path.empty())
	{
		RequestSaveAsDialog();
		return;
	}

	SaveDocumentIdAs(active->id, active->path);
}

void TextEditorWindow::SaveDocumentIdAs(int id, const std::filesystem::path& requested_path)
{
	StoreEditorText();
	Document* target = FindDocumentById(id);
	if (!target)
	{
		RefreshStatus("The document was closed before it could be saved");
		return;
	}

	if (requested_path.empty())
	{
		RefreshStatus("No file selected");
		return;
	}

	const std::filesystem::path path = NormalizePath(requested_path);
	String error;
	if (!WriteTextFile(path, target->text, error))
	{
		RefreshStatus(error);
		return;
	}

	target->path = path;
	target->dirty = false;
	if (Editor::ElementScintilla* editor = GetEditorControl())
		editor->SetDocumentSavePoint(target->editor_document);
	RefreshTabs();
	RefreshStatus("Saved " + PathToString(path));
}

bool TextEditorWindow::SaveAllDocuments()
{
	StoreEditorText();

	bool all_saved = true;
	for (Document& document : documents)
	{
		if (!document.dirty)
			continue;

		if (document.path.empty())
		{
			RefreshStatus(DocumentTitle(document) + " has no path. Select its tab, use Save As, then close again.");
			all_saved = false;
			continue;
		}

		const std::filesystem::path path = NormalizePath(document.path);
		String error;
		if (!WriteTextFile(path, document.text, error))
		{
			RefreshStatus(error);
			all_saved = false;
			continue;
		}

		document.path = path;
		document.dirty = false;
		if (Editor::ElementScintilla* editor = GetEditorControl())
			editor->SetDocumentSavePoint(document.editor_document);
	}

	RefreshTabs();
	return all_saved;
}

void TextEditorWindow::CloseActiveDocument(bool discard)
{
	if (const Document* active = ActiveDocument())
		CloseDocumentById(active->id, discard);
}

void TextEditorWindow::CloseDocumentById(int id, bool discard)
{
	StoreEditorText();

	for (auto it = documents.begin(); it != documents.end(); ++it)
	{
		if (it->id != id)
			continue;

		if (it->dirty && !discard)
		{
			RefreshStatus(DocumentTitle(*it) + " has unsaved changes. Use Discard to close it without saving.");
			return;
		}

		const String title = DocumentTitle(*it);
		const int removed_index = int(it - documents.begin());
		if (Editor::ElementScintilla* editor = GetEditorControl())
			editor->ReleaseDocument(it->editor_document);
		documents.erase(it);

		if (documents.empty())
		{
			CreateDocument("", {}, "Closed " + title);
			return;
		}

		if (active_document_id == id)
		{
			const int next_index = Math::Min(removed_index, int(documents.size()) - 1);
			active_document_id = documents[next_index].id;
		}

		ApplyActiveDocumentToView();
		RefreshStatus("Closed " + title);
		return;
	}
}

void TextEditorWindow::ActivateDocumentById(int id)
{
	if (!FindDocumentById(id))
		return;

	StoreEditorText();
	active_document_id = id;
	ApplyActiveDocumentToView();
	RefreshEditorStatus();
}

void TextEditorWindow::StoreEditorText()
{
	Document* active = ActiveDocument();
	Editor::ElementScintilla* editor = GetEditorControl();
	if (!active || !editor)
		return;

	// Capture the buffer for saving, tab switches, and close. Dirty is tracked live from Scintilla's
	// save point (see OnEditorDirtyChanged), so this no longer re-scans the whole document to decide.
	active->text = editor->GetText();
}

void TextEditorWindow::ApplyActiveDocumentToView()
{
	Document* active = ActiveDocument();
	Editor::ElementScintilla* editor = GetEditorControl();
	if (!active || !editor)
		return;

	applying_editor_value = true;
	editor->SetDocument(active->editor_document);
	if (!active->editor_document_initialized)
	{
		editor->SetText(active->text);
		active->editor_document_initialized = true;
	}
	applying_editor_value = false;

	RefreshTabs();
	FocusEditor();
}

void TextEditorWindow::OnEditorDirtyChanged(bool dirty)
{
	// Ignore the save-point moves caused by a programmatic load; the stored dirty flag stands.
	if (applying_editor_value)
		return;
	Document* active = ActiveDocument();
	if (!active || active->dirty == dirty)
		return;
	active->dirty = dirty;
	RefreshTabs();
	RefreshEditorStatus();
}

void TextEditorWindow::OnEditorTextChanged()
{
	if (applying_editor_value)
		return;
	RefreshEditorStatus();
}

void TextEditorWindow::RefreshEditorStatus()
{
	const Document* active = ActiveDocument();
	Editor::ElementScintilla* editor = GetEditorControl();
	if (!active || !editor)
		return;

	const int lines = editor->GetLineCount();
	const int characters = editor->GetCharacterCount();
	RefreshStatus(String(active->dirty ? "modified, " : "") + std::to_string(lines) + " lines, " + std::to_string(characters) + " chars");
}

void TextEditorWindow::RefreshTabs()
{
	Element* tabs = GetElement("tabs");
	if (!tabs)
		return;

	String rml;
	for (const Document& document : documents)
	{
		String classes = "tab-item";
		if (document.id == active_document_id)
			classes += " selected";
		if (document.dirty)
			classes += " dirty";

		const String label = StringUtilities::EncodeRml(DocumentTitle(document) + (document.dirty ? " *" : ""));
		rml += "<div class=\"" + classes + "\">";
		rml += "<button class=\"tab-button\" onclick=\"activate_tab:" + std::to_string(document.id) + "\">" + label + "</button>";
		rml += "<button class=\"tab-close\" onclick=\"close_tab:" + std::to_string(document.id) + "\">x</button>";
		rml += "</div>";
	}

	tabs->SetInnerRML(rml);
}

void TextEditorWindow::RefreshStatus(const String& status)
{
	if (Element* status_element = GetElement("status"))
		status_element->SetInnerRML(StringUtilities::EncodeRml(status));
}

bool TextEditorWindow::HasUnsavedChanges()
{
	// Capture the active buffer before deciding. Dirty flags are already current from Scintilla's
	// change tracking; this just keeps each document's stored text in step before a close.
	StoreEditorText();
	for (const Document& document : documents)
	{
		if (document.dirty)
			return true;
	}
	return false;
}

void TextEditorWindow::ShowCloseConfirmation()
{
	if (Element* dialog = GetElement("close_confirm"))
		dialog->SetClass("visible", true);
}

void TextEditorWindow::HideCloseConfirmation()
{
	if (Element* dialog = GetElement("close_confirm"))
		dialog->SetClass("visible", false);
}

void TextEditorWindow::ApplyTheme(Theme theme)
{
	current_theme = theme;
	if (!document)
		return;

	// Light is the default palette (no class); Dark is opt-in via the class.
	document->SetClass("theme-dark", theme == Theme::Dark);
	if (Editor::ElementScintilla* editor = GetEditorControl())
		editor->SetDarkTheme(theme == Theme::Dark);

	if (Element* light_item = GetElement("theme_light_item"))
		light_item->SetClass("active", theme == Theme::Light);
	if (Element* dark_item = GetElement("theme_dark_item"))
		dark_item->SetClass("active", theme == Theme::Dark);
}

void TextEditorWindow::ApplyWordWrap(bool word_wrap)
{
	current_word_wrap = word_wrap;
	if (Editor::ElementScintilla* editor = GetEditorControl())
		editor->SetWordWrap(word_wrap);
	if (Element* item = GetElement("word_wrap_item"))
		item->SetClass("active", word_wrap);
}

void TextEditorWindow::ApplyFontFamily(FontFamily font_family)
{
	current_font_family = font_family;
	if (!document)
		return;

	const char* css_family = "monospace";
	switch (font_family)
	{
	case FontFamily::SystemUi: css_family = "system-ui"; break;
	case FontFamily::SansSerif: css_family = "sans-serif"; break;
	case FontFamily::Serif: css_family = "serif"; break;
	case FontFamily::Monospace: css_family = "monospace"; break;
	}

	if (Editor::ElementScintilla* editor = GetEditorControl())
		editor->SetEditorFont(css_family, current_editor_font_size);

	if (Element* item = GetElement("font_system_ui_item"))
		item->SetClass("active", font_family == FontFamily::SystemUi);
	if (Element* item = GetElement("font_sans_serif_item"))
		item->SetClass("active", font_family == FontFamily::SansSerif);
	if (Element* item = GetElement("font_serif_item"))
		item->SetClass("active", font_family == FontFamily::Serif);
	if (Element* item = GetElement("font_monospace_item"))
		item->SetClass("active", font_family == FontFamily::Monospace);
}

void TextEditorWindow::AdjustEditorFontSize(int delta)
{
	ApplyEditorFontSize(current_editor_font_size + delta);
}

void TextEditorWindow::ResetEditorFontSize()
{
	ApplyEditorFontSize(DefaultEditorFontSize);
}

void TextEditorWindow::ApplyEditorFontSize(int font_size)
{
	current_editor_font_size = Math::Clamp(font_size, MinEditorFontSize, MaxEditorFontSize);

	const char* css_family = "monospace";
	switch (current_font_family)
	{
	case FontFamily::SystemUi: css_family = "system-ui"; break;
	case FontFamily::SansSerif: css_family = "sans-serif"; break;
	case FontFamily::Serif: css_family = "serif"; break;
	case FontFamily::Monospace: css_family = "monospace"; break;
	}
	if (Editor::ElementScintilla* editor = GetEditorControl())
		editor->SetEditorFont(css_family, current_editor_font_size);

	RefreshStatus(CreateString("Font size %d px", current_editor_font_size));
}

OnlyWayUi::String TextEditorWindow::ActiveDocumentFolder() const
{
	const Document* active = ActiveDocument();
	if (!active || active->path.empty())
		return {};
	return PathToString(active->path.parent_path());
}

void TextEditorWindow::FocusEditor()
{
	if (Editor::ElementScintilla* editor = GetEditorControl())
		editor->Focus();
}

TextEditorWindow::Document* TextEditorWindow::ActiveDocument()
{
	return FindDocumentById(active_document_id);
}

const TextEditorWindow::Document* TextEditorWindow::ActiveDocument() const
{
	for (const Document& document : documents)
	{
		if (document.id == active_document_id)
			return &document;
	}
	return nullptr;
}

TextEditorWindow::Document* TextEditorWindow::FindDocumentById(int id)
{
	for (Document& document : documents)
	{
		if (document.id == id)
			return &document;
	}
	return nullptr;
}

TextEditorWindow::Document* TextEditorWindow::FindDocumentByPath(const std::filesystem::path& path)
{
	for (Document& document : documents)
	{
		if (!document.path.empty() && document.path == path)
			return &document;
	}
	return nullptr;
}

Element* TextEditorWindow::GetElement(const String& id) const
{
	return document ? document->GetElementById(id) : nullptr;
}

Editor::ElementScintilla* TextEditorWindow::GetEditorControl() const
{
	return owui_dynamic_cast<Editor::ElementScintilla*>(GetElement("editor"));
}

std::filesystem::path TextEditorWindow::NormalizePath(const std::filesystem::path& path)
{
	std::error_code error;
	std::filesystem::path result = std::filesystem::weakly_canonical(path, error);
	if (!error)
		return result;

	result = std::filesystem::absolute(path, error);
	if (!error)
		return result;

	return path;
}

String TextEditorWindow::ReadTextFile(const std::filesystem::path& path, bool& ok, String& error)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		ok = false;
		error = "Failed to open " + PathToString(path);
		return {};
	}

	std::ostringstream contents;
	contents << file.rdbuf();
	if (!file.good() && !file.eof())
	{
		ok = false;
		error = "Failed to read " + PathToString(path);
		return {};
	}

	ok = true;
	return contents.str();
}

bool TextEditorWindow::WriteTextFile(const std::filesystem::path& path, const String& text, String& error)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file)
	{
		error = "Failed to open " + PathToString(path) + " for writing";
		return false;
	}

	file.write(text.data(), std::streamsize(text.size()));
	if (!file)
	{
		error = "Failed to write " + PathToString(path);
		return false;
	}

	return true;
}

String TextEditorWindow::DocumentTitle(const Document& document)
{
	if (!document.path.empty() && document.path.filename() != "")
		return document.path.filename().string();

	return "Untitled";
}
