// Modal spelling-review card model, layout, input transitions, and painter.

#ifndef SPELLCHECKDIALOG_H
#define SPELLCHECKDIALOG_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "FindBar.h"
#include "Geometry.h"
#include "Platform.h"
#include "Spellcheck.h"
#include "SpellcheckSession.h"
#include "UiStyle.h"

namespace Scalpel {

enum class SpellcheckDialogPhase {
	Loading,
	Decision,
	Complete,
	Failed,
};

enum class SpellcheckDialogControl {
	Dictionary,
	Suggestion,
	Replacement,
	Replace,
	ReplaceAll,
	Ignore,
	IgnoreAll,
	Add,
	Close,
};

enum class SpellcheckDialogHit {
	None,
	Dictionary,
	Suggestion,
	Replacement,
	Replace,
	ReplaceAll,
	Ignore,
	IgnoreAll,
	Add,
	Close,
};

struct SpellcheckDialogModel {
	bool open = false;
	SpellcheckDialogPhase phase = SpellcheckDialogPhase::Loading;
	std::vector<SpellcheckDictionaryInfo> dictionaries;
	std::size_t dictionary = 0;
	std::string word;
	std::string context;
	std::vector<std::string> suggestions;
	std::size_t suggestion = 0;
	FindBarModel replacement;
	std::string message;
	SpellcheckDialogControl focus = SpellcheckDialogControl::Close;
	std::optional<SpellcheckDialogHit> pressOrigin;
};

struct SpellcheckDialogLayout {
	Scintilla::Internal::PRectangle scrim;
	Scintilla::Internal::PRectangle card;
	Scintilla::Internal::PRectangle title;
	Scintilla::Internal::PRectangle dictionary;
	Scintilla::Internal::PRectangle context;
	Scintilla::Internal::PRectangle word;
	Scintilla::Internal::PRectangle suggestion;
	Scintilla::Internal::PRectangle replacement;
	Scintilla::Internal::PRectangle replacementText;
	Scintilla::Internal::PRectangle replaceButton;
	Scintilla::Internal::PRectangle replaceAllButton;
	Scintilla::Internal::PRectangle ignoreButton;
	Scintilla::Internal::PRectangle ignoreAllButton;
	Scintilla::Internal::PRectangle addButton;
	Scintilla::Internal::PRectangle closeButton;
};

[[nodiscard]] SpellcheckDialogLayout LayoutSpellcheckDialog(
	int width, int height) noexcept;
[[nodiscard]] SpellcheckDialogHit HitTestSpellcheckDialog(
	const SpellcheckDialogLayout &layout,
	Scintilla::Internal::Point point) noexcept;
[[nodiscard]] SpellcheckDialogControl CycleSpellcheckDialogFocus(
	SpellcheckDialogControl focus, int delta) noexcept;

class SpellcheckDialogPainter final {
public:
	explicit SpellcheckDialogPainter(const UiStyle &style = DefaultUiStyle());
	~SpellcheckDialogPainter() = default;

	SpellcheckDialogPainter(const SpellcheckDialogPainter &) = delete;
	SpellcheckDialogPainter &operator=(const SpellcheckDialogPainter &) = delete;

	void Paint(Scintilla::Internal::Surface &surface,
		const SpellcheckDialogLayout &layout,
		const SpellcheckDialogModel &model) const;
	[[nodiscard]] const Scintilla::Internal::Font *BodyFont() const noexcept {
		return bodyFont.get();
	}

private:
	UiStyle style;
	std::shared_ptr<Scintilla::Internal::Font> titleFont;
	std::shared_ptr<Scintilla::Internal::Font> bodyFont;
};

}

#endif
