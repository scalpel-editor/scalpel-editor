#pragma once

#include <OnlyWayUi/Core/Element.h>
#include <OnlyWayUi/Core/Traits.h>
#include <memory>

namespace OnlyWayUi::Editor {

// Document-editing surface for OnlyWayUi: a Scintilla control rendered and driven through this
// toolkit (fonts, input, paint, Wayland). Use this element for multi-document editors and large
// buffers; keep ordinary form fields on <textarea>. App chrome talks only to this facade
// (text, documents, dirty/status notices, font, theme, wrap). Scintilla's message API stays inside
// the Editor library; RCSS styles the host box, not the buffer text (that uses Scintilla styles).
class ElementScintilla final : public Element {
public:
	OWUI_RTTI_DefineWithParent(ElementScintilla, Element)
	using DocumentHandle = void*;

	// Receives editor changes the host chrome reacts to: save-point moves drive its dirty flag and
	// tab title, text changes drive the line/char status.
	struct Observer {
		virtual ~Observer() = default;
		virtual void OnEditorDirtyChanged(bool /*dirty*/) {}
		virtual void OnEditorTextChanged() {}
	};

	explicit ElementScintilla(const String& tag);
	~ElementScintilla() override;

	void SetObserver(Observer* observer);

	void SetText(const String& text);
	String GetText() const;
	DocumentHandle CreateDocument();
	void SetDocument(DocumentHandle document);
	void ReleaseDocument(DocumentHandle document);
	void SetDocumentSavePoint(DocumentHandle document);
	void SetEditorFont(const String& family, int size);
	void SetDarkTheme(bool dark);
	// Soft-wrap long lines at word boundaries when true; one long line (horizontal overflow) when false.
	// Default is on. Maps to Scintilla SetWrapMode (SCI_SETWRAPMODE): Word vs None.
	void SetWordWrap(bool word_wrap);
	bool GetWordWrap() const;
	// Status counts backed by Scintilla's own bookkeeping.
	int GetLineCount() const;
	int GetCharacterCount() const;

protected:
	void OnUpdate() override;
	void OnRender() override;
	void OnResize() override;
	void OnDpRatioChange() override;

private:
	class Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace OnlyWayUi::Editor
