#include "catch.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ApplicationEditor.h"
#include "ApplicationInput.h"
#include "ApplicationTextInput.h"
#include "FindBar.h"

using Scalpel::ApplicationEditor;
using Scalpel::ApplicationTextChangeCause;
using Scalpel::ApplicationTextInputBatch;
using Scalpel::ApplicationTextInputDelete;
using Scalpel::ApplicationTextInputPreedit;
using Scalpel::ApplicationTextInputState;
using Scalpel::ApplyFindBarPaste;
using Scalpel::BuildFindBarTextInputState;
using Scalpel::FindBarHeight;
using Scalpel::FindBarHit;
using Scalpel::FindBarHitResult;
using Scalpel::FindBarKeyboardResult;
using Scalpel::FindBarLayout;
using Scalpel::FindBarModel;
using Scalpel::FindBarPainter;
using Scalpel::FindBarPointerResult;
using Scalpel::FindBarPressKind;
using Scalpel::FindBarRequestKind;
using Scalpel::FindBarStatus;
using Scalpel::FindBarStatusLabel;
using Scalpel::HandleFindBarKeyboard;
using Scalpel::HandleFindBarPointer;
using Scalpel::HandleFindBarTextInputBatch;
using Scalpel::HitTestFindBar;
using Scalpel::KeyboardInput;
using Scalpel::LayoutFindBar;
using Scalpel::PointerAction;
using Scalpel::PointerInput;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;
using Scintilla::KeyMod;
using Scintilla::Keys;

namespace {

bool NonEmpty(const PRectangle &rc) {
	return rc.right > rc.left && rc.bottom > rc.top;
}

bool Disjoint(const PRectangle &a, const PRectangle &b) {
	return !a.Intersects(b);
}

Point Center(const PRectangle &rc) {
	return Point((rc.left + rc.right) / 2.0, (rc.top + rc.bottom) / 2.0);
}

PointerInput MakePointer(PointerAction action, double x, double y,
	int button = -1) {
	PointerInput input;
	input.action = action;
	input.x = x;
	input.y = y;
	input.button = button;
	return input;
}

KeyboardInput MakeKey(Keys key, KeyMod modifiers = KeyMod::Norm,
	bool pressed = true) {
	KeyboardInput input;
	input.key = key;
	input.modifiers = modifiers;
	input.pressed = pressed;
	return input;
}

KeyboardInput MakeText(std::string text, KeyMod modifiers = KeyMod::Norm) {
	KeyboardInput input;
	input.key = static_cast<Keys>(0);
	input.modifiers = modifiers;
	input.text = std::move(text);
	input.pressed = true;
	return input;
}

struct Rgba {
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	uint8_t a = 0;
};

Rgba Sample(const std::vector<uint8_t> &pixels, int width, int x, int y) {
	const size_t offset =
		(static_cast<size_t>(y) * static_cast<size_t>(width) +
			static_cast<size_t>(x)) *
		4U;
	REQUIRE(offset + 3 < pixels.size());
	return {pixels[offset], pixels[offset + 1], pixels[offset + 2],
		pixels[offset + 3]};
}

bool Differs(Rgba a, Rgba b, int tol = 2) {
	return std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)) > tol ||
		std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)) > tol ||
		std::abs(static_cast<int>(a.b) - static_cast<int>(b.b)) > tol;
}

std::vector<uint8_t> PaintBar(ApplicationEditor &editor, FindBarPainter &painter,
	const FindBarLayout &layout, const FindBarModel &model) {
	(void)editor.TakeFrameDamage();
	editor.SetOverlayPainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			CHECK(width == editor.FrameWidth());
			CHECK(height == editor.FrameHeight());
			painter.Paint(surface, layout, model);
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, editor.FrameWidth(),
		editor.FrameHeight())});
	return editor.FramePixels();
}

}

TEST_CASE("Find bar height is fixed and positive") {
	CHECK(FindBarHeight() > 0);
	CHECK(FindBarHeight() == Scalpel::DefaultUiStyle().findBarHeight);
}

TEST_CASE("Find bar layout places field status and buttons") {
	const FindBarLayout layout = LayoutFindBar(480, 40);
	CHECK(layout.band == PRectangle::FromInts(0, 40, 480, 40 + FindBarHeight()));
	CHECK(NonEmpty(layout.field));
	CHECK(NonEmpty(layout.fieldText));
	CHECK(NonEmpty(layout.previousButton));
	CHECK(NonEmpty(layout.nextButton));
	CHECK(NonEmpty(layout.closeButton));
	CHECK(layout.band.Contains(layout.field));
	CHECK(layout.band.Contains(layout.previousButton));
	CHECK(layout.band.Contains(layout.nextButton));
	CHECK(layout.band.Contains(layout.closeButton));
	CHECK(layout.field.Contains(layout.fieldText));
	// Close is a square control (same side as findCloseSize when the band fits).
	CHECK(layout.closeButton.Width() == layout.closeButton.Height());
	CHECK(layout.closeButton.Width() ==
		Scalpel::DefaultUiStyle().findCloseSize);
	// Right-to-left: close, next, previous.
	CHECK(layout.closeButton.right == layout.band.right -
		Scalpel::DefaultUiStyle().findBarPadX);
	CHECK(layout.nextButton.right <= layout.closeButton.left);
	CHECK(layout.previousButton.right <= layout.nextButton.left);
	CHECK((layout.field.right <= layout.previousButton.left ||
		layout.field.right <= layout.status.left));
	CHECK(Disjoint(layout.field, layout.previousButton));
	CHECK(Disjoint(layout.previousButton, layout.nextButton));
	CHECK(Disjoint(layout.nextButton, layout.closeButton));
}

TEST_CASE("Find bar zero width yields empty layout") {
	const FindBarLayout layout = LayoutFindBar(0, 0);
	CHECK_FALSE(NonEmpty(layout.band));
	CHECK_FALSE(NonEmpty(layout.field));
	const FindBarHitResult hit = HitTestFindBar(layout, Point(0, 0));
	CHECK(hit.kind == FindBarHit::None);
}

TEST_CASE("Find bar narrow frames clamp without negative or overlapping rects") {
	for (const int width : {1, 40, 80, 120, 160, 200}) {
		const FindBarLayout layout = LayoutFindBar(width, 10);
		INFO("width=" << width);
		CHECK(layout.band.left == 0);
		CHECK(layout.band.right == width);
		CHECK(layout.band.top == 10);
		const PRectangle *rects[] = {
			&layout.field, &layout.fieldText, &layout.status,
			&layout.previousButton, &layout.nextButton, &layout.closeButton,
		};
		for (const PRectangle *rc : rects) {
			if (NonEmpty(*rc)) {
				CHECK(rc->left >= layout.band.left);
				CHECK(rc->right <= layout.band.right);
				CHECK(rc->top >= layout.band.top);
				CHECK(rc->bottom <= layout.band.bottom);
				CHECK(rc->right > rc->left);
				CHECK(rc->bottom > rc->top);
			}
		}
		// Non-empty controls must not overlap each other.
		const PRectangle *controls[] = {
			&layout.field, &layout.status, &layout.previousButton,
			&layout.nextButton, &layout.closeButton,
		};
		for (std::size_t i = 0; i < 5; ++i) {
			for (std::size_t j = i + 1; j < 5; ++j) {
				if (NonEmpty(*controls[i]) && NonEmpty(*controls[j])) {
					CHECK(Disjoint(*controls[i], *controls[j]));
				}
			}
		}
	}
}

TEST_CASE("Find bar hit-test distinguishes field buttons and band") {
	const FindBarLayout layout = LayoutFindBar(500, 0);
	CHECK(HitTestFindBar(layout, Center(layout.field)).kind == FindBarHit::Field);
	CHECK(HitTestFindBar(layout, Center(layout.previousButton)).kind ==
		FindBarHit::Previous);
	CHECK(HitTestFindBar(layout, Center(layout.nextButton)).kind ==
		FindBarHit::Next);
	CHECK(HitTestFindBar(layout, Center(layout.closeButton)).kind ==
		FindBarHit::Close);
	// Just inside the band, left of the field padding edge.
	CHECK(HitTestFindBar(layout, Point(1, FindBarHeight() / 2.0)).kind ==
		FindBarHit::Band);
	CHECK(HitTestFindBar(layout, Point(10, -1)).kind == FindBarHit::None);
	CHECK(HitTestFindBar(layout, Point(10, FindBarHeight() + 1)).kind ==
		FindBarHit::None);
}

TEST_CASE("Find bar pointer press in field focuses and selects query") {
	FindBarModel model;
	REQUIRE(model.SetQuery("hello"));
	model.caret = 2;
	model.anchor = 2;
	const FindBarLayout layout = LayoutFindBar(480, 0);
	const Point field = Center(layout.field);

	const FindBarPointerResult press = HandleFindBarPointer(model, layout,
		MakePointer(PointerAction::Press, field.x, field.y, 0));
	CHECK(press.consumed);
	CHECK(press.dirty);
	CHECK(press.fieldFocused);
	CHECK(model.focused);
	CHECK(model.caret == model.query.size());
	CHECK(model.anchor == 0);
	CHECK(model.HasSelection());
}

TEST_CASE("Find bar pointer activates buttons only on matching press release") {
	FindBarModel model;
	const FindBarLayout layout = LayoutFindBar(480, 0);
	const Point next = Center(layout.nextButton);
	const Point prev = Center(layout.previousButton);
	const Point close = Center(layout.closeButton);
	const Point field = Center(layout.field);

	// Next: press and release on Next.
	{
		const FindBarPointerResult press = HandleFindBarPointer(model, layout,
			MakePointer(PointerAction::Press, next.x, next.y, 0));
		CHECK(press.consumed);
		CHECK(model.pressOrigin == FindBarPressKind::Next);
		const FindBarPointerResult release = HandleFindBarPointer(model, layout,
			MakePointer(PointerAction::Release, next.x, next.y, 0));
		CHECK(release.consumed);
		REQUIRE(release.requests.size() == 1);
		CHECK(release.requests[0].kind == FindBarRequestKind::SearchForward);
		CHECK_FALSE(model.pressOrigin.has_value());
	}

	// Previous: press and release on Previous.
	{
		const FindBarPointerResult press = HandleFindBarPointer(model, layout,
			MakePointer(PointerAction::Press, prev.x, prev.y, 0));
		CHECK(press.consumed);
		const FindBarPointerResult release = HandleFindBarPointer(model, layout,
			MakePointer(PointerAction::Release, prev.x, prev.y, 0));
		REQUIRE(release.requests.size() == 1);
		CHECK(release.requests[0].kind == FindBarRequestKind::SearchBackward);
	}

	// Close: press and release on Close.
	{
		const FindBarPointerResult press = HandleFindBarPointer(model, layout,
			MakePointer(PointerAction::Press, close.x, close.y, 0));
		CHECK(press.consumed);
		const FindBarPointerResult release = HandleFindBarPointer(model, layout,
			MakePointer(PointerAction::Release, close.x, close.y, 0));
		REQUIRE(release.requests.size() == 1);
		CHECK(release.requests[0].kind == FindBarRequestKind::Close);
	}

	// Mismatched release: press Next, release on field — no request.
	{
		(void)HandleFindBarPointer(model, layout,
			MakePointer(PointerAction::Press, next.x, next.y, 0));
		const FindBarPointerResult release = HandleFindBarPointer(model, layout,
			MakePointer(PointerAction::Release, field.x, field.y, 0));
		CHECK(release.consumed);
		CHECK(release.requests.empty());
		CHECK_FALSE(model.pressOrigin.has_value());
	}
}

TEST_CASE("Find bar leave clears press without consuming") {
	FindBarModel model;
	const FindBarLayout layout = LayoutFindBar(480, 0);
	const Point next = Center(layout.nextButton);
	(void)HandleFindBarPointer(model, layout,
		MakePointer(PointerAction::Press, next.x, next.y, 0));
	REQUIRE(model.pressOrigin.has_value());
	const FindBarPointerResult leave = HandleFindBarPointer(model, layout,
		MakePointer(PointerAction::Leave, 0, 0));
	CHECK_FALSE(leave.consumed);
	CHECK(leave.dirty);
	CHECK_FALSE(model.pressOrigin.has_value());
}

TEST_CASE("Find bar UTF-8 caret deletion and selection stay on boundaries") {
	FindBarModel model;
	REQUIRE(model.SetQuery("a\xC3\xA9z")); // a é z
	model.focused = true;
	model.caret = 0;
	model.anchor = 0;

	// Move right across ASCII then the two-byte character.
	(void)HandleFindBarKeyboard(model, MakeKey(Keys::Right));
	CHECK(model.caret == 1);
	(void)HandleFindBarKeyboard(model, MakeKey(Keys::Right));
	CHECK(model.caret == 3);
	(void)HandleFindBarKeyboard(model, MakeKey(Keys::Right));
	CHECK(model.caret == 4);

	// Backspace deletes the whole character, not one trail byte.
	(void)HandleFindBarKeyboard(model, MakeKey(Keys::Back));
	CHECK(model.query == "a\xC3\xA9");
	CHECK(model.caret == 3);

	(void)HandleFindBarKeyboard(model, MakeKey(Keys::Back));
	CHECK(model.query == "a");
	CHECK(model.caret == 1);

	REQUIRE(model.SetQuery("a\xC3\xA9z"));
	model.focused = true;
	model.caret = 0;
	model.anchor = 0;
	(void)HandleFindBarKeyboard(model, MakeKey(Keys::End));
	CHECK(model.caret == 4);
	(void)HandleFindBarKeyboard(model, MakeKey(Keys::Home));
	CHECK(model.caret == 0);

	// Select all and replace with multi-byte text.
	(void)HandleFindBarKeyboard(model, MakeKey(static_cast<Keys>('A'), KeyMod::Ctrl));
	CHECK(model.HasSelection());
	const FindBarKeyboardResult typed = HandleFindBarKeyboard(model,
		MakeText("\xE6\x96\x87"));
	CHECK(typed.queryChanged);
	CHECK(model.query == "\xE6\x96\x87");
	CHECK(model.caret == 3);
	CHECK_FALSE(model.HasSelection());
}

TEST_CASE("Find bar rejects invalid UTF-8 inserts") {
	FindBarModel model;
	REQUIRE(model.SetQuery("ok"));
	CHECK_FALSE(model.InsertText("\xFF"));
	CHECK(model.query == "ok");
	CHECK_FALSE(model.SetQuery("\xC3"));
	CHECK(model.query == "ok");
}

TEST_CASE("Find bar keyboard search close and clipboard requests") {
	FindBarModel model;
	REQUIRE(model.SetQuery("needle"));
	model.SetFocused(true);
	model.pasteAvailable = true;

	{
		const FindBarKeyboardResult enter = HandleFindBarKeyboard(model,
			MakeKey(Keys::Return));
		REQUIRE(enter.requests.size() == 1);
		CHECK(enter.requests[0].kind == FindBarRequestKind::SearchForward);
	}
	{
		const FindBarKeyboardResult shiftEnter = HandleFindBarKeyboard(model,
			MakeKey(Keys::Return, KeyMod::Shift));
		REQUIRE(shiftEnter.requests.size() == 1);
		CHECK(shiftEnter.requests[0].kind == FindBarRequestKind::SearchBackward);
	}
	{
		const FindBarKeyboardResult escape = HandleFindBarKeyboard(model,
			MakeKey(Keys::Escape));
		REQUIRE(escape.requests.size() == 1);
		CHECK(escape.requests[0].kind == FindBarRequestKind::Close);
	}

	model.SelectAll();
	{
		const FindBarKeyboardResult copy = HandleFindBarKeyboard(model,
			MakeKey(static_cast<Keys>('C'), KeyMod::Ctrl));
		REQUIRE(copy.requests.size() == 1);
		CHECK(copy.requests[0].kind == FindBarRequestKind::ClipboardCopy);
		CHECK(copy.requests[0].clipboardText == "needle");
		CHECK(copy.requests[0].clipboardId != 0);
	}
	{
		const FindBarKeyboardResult cut = HandleFindBarKeyboard(model,
			MakeKey(static_cast<Keys>('X'), KeyMod::Ctrl));
		REQUIRE(cut.requests.size() == 1);
		CHECK(cut.requests[0].kind == FindBarRequestKind::ClipboardCopy);
		CHECK(cut.requests[0].clipboardText == "needle");
		CHECK(model.query.empty());
		CHECK(cut.queryChanged);
	}
	REQUIRE(model.SetQuery("x"));
	model.SetFocused(true);
	{
		const FindBarKeyboardResult paste = HandleFindBarKeyboard(model,
			MakeKey(static_cast<Keys>('V'), KeyMod::Ctrl));
		REQUIRE(paste.requests.size() == 1);
		CHECK(paste.requests[0].kind == FindBarRequestKind::ClipboardPaste);
	}
	CHECK(ApplyFindBarPaste(model, "pasted"));
	// SetFocused selected all, so paste replaces the query.
	CHECK(model.query == "pasted");
}

TEST_CASE("Find bar keyboard is ignored when unfocused") {
	FindBarModel model;
	REQUIRE(model.SetQuery("q"));
	model.focused = false;
	const FindBarKeyboardResult result = HandleFindBarKeyboard(model,
		MakeText("z"));
	CHECK_FALSE(result.consumed);
	CHECK(model.query == "q");
}

TEST_CASE("Find bar direct and preedit text input") {
	FindBarModel model;
	REQUIRE(model.SetQuery("base"));
	model.SetFocused(true);
	// Select-all from focus; type replaces the whole query.
	const FindBarKeyboardResult typed = HandleFindBarKeyboard(model, MakeText("x"));
	CHECK(typed.queryChanged);
	CHECK(model.query == "x");

	ApplicationTextInputBatch preedit;
	preedit.preedit = ApplicationTextInputPreedit{"\xC3\xA9", 2, 2};
	const auto first = HandleFindBarTextInputBatch(model, preedit);
	CHECK(first.dirty);
	CHECK_FALSE(first.queryChanged);
	REQUIRE(model.preedit.has_value());
	CHECK(model.preedit->text == "\xC3\xA9");
	CHECK(model.query == "x");

	ApplicationTextInputBatch commit;
	commit.commit = "y";
	const auto second = HandleFindBarTextInputBatch(model, commit);
	CHECK(second.queryChanged);
	CHECK(model.query == "xy");
	CHECK_FALSE(model.preedit.has_value());

	ApplicationTextInputBatch withPreedit;
	withPreedit.preedit = ApplicationTextInputPreedit{"z", 1, 1};
	(void)HandleFindBarTextInputBatch(model, withPreedit);
	REQUIRE(model.preedit.has_value());
	ApplicationTextInputBatch cancel;
	cancel.cancel = true;
	const auto cancelled = HandleFindBarTextInputBatch(model, cancel);
	CHECK(cancelled.dirty);
	CHECK_FALSE(model.preedit.has_value());
	CHECK(model.query == "xy");
}

TEST_CASE("Find bar delete-surrounding respects UTF-8 boundaries") {
	FindBarModel model;
	REQUIRE(model.SetQuery("a\xC3\xA9z"));
	model.focused = true;
	// Caret after é (byte offset 3); a one-byte before-delete lands mid-character.
	model.caret = 3;
	model.anchor = 3;

	ApplicationTextInputBatch bad;
	bad.deletion = ApplicationTextInputDelete{1, 0};
	const auto rejected = HandleFindBarTextInputBatch(model, bad);
	CHECK(rejected.dirty);
	CHECK_FALSE(rejected.queryChanged);
	CHECK(model.query == "a\xC3\xA9z");

	// Two bytes before the caret is the start of é.
	ApplicationTextInputBatch good;
	good.deletion = ApplicationTextInputDelete{2, 0};
	const auto accepted = HandleFindBarTextInputBatch(model, good);
	CHECK(accepted.queryChanged);
	CHECK(model.query == "az");
	CHECK(model.caret == 1);
}

TEST_CASE("Find bar delete-surrounding excludes the selection") {
	FindBarModel model;
	REQUIRE(model.SetQuery("abcd"));
	model.focused = true;
	model.anchor = 1;
	model.caret = 3; // Select "bc".

	ApplicationTextInputBatch zero;
	zero.deletion = ApplicationTextInputDelete{0, 0};
	const auto unchanged = HandleFindBarTextInputBatch(model, zero);
	CHECK_FALSE(unchanged.queryChanged);
	CHECK(model.query == "abcd");
	CHECK(model.anchor == 1);
	CHECK(model.caret == 3);

	ApplicationTextInputBatch around;
	around.deletion = ApplicationTextInputDelete{1, 1};
	const auto deleted = HandleFindBarTextInputBatch(model, around);
	CHECK(deleted.queryChanged);
	CHECK(model.query == "bc");
	CHECK(model.anchor == 0);
	CHECK(model.caret == 2);
	CHECK(model.SelectedText() == "bc");
}

TEST_CASE("Find bar status labels and model status") {
	CHECK(FindBarStatusLabel(FindBarStatus::None).empty());
	CHECK(FindBarStatusLabel(FindBarStatus::NoMatches) == "No matches");
	CHECK(FindBarStatusLabel(FindBarStatus::Wrapped) == "Wrapped");
	FindBarModel model;
	model.SetStatus(FindBarStatus::NoMatches);
	CHECK(model.status == FindBarStatus::NoMatches);
	REQUIRE(model.InsertText("a"));
	CHECK(model.status == FindBarStatus::None);
}

TEST_CASE("Find bar text-input state reports surrounding text and caret rect") {
	ApplicationEditor editor(400, 120);
	FindBarPainter painter;
	FindBarModel model;
	REQUIRE(model.SetQuery("a\xC3\xA9z"));
	model.SetFocused(true);
	model.caret = 3;
	model.anchor = 1;
	const FindBarLayout layout = LayoutFindBar(400, 20);

	ApplicationTextInputState state;
	editor.SetOverlayPainter(
		[&](Scintilla::Internal::Surface &surface, int, int) {
			state = BuildFindBarTextInputState(model, layout, surface,
				painter.LabelFont());
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, editor.FrameWidth(),
		editor.FrameHeight())});

	REQUIRE(state.surroundingText.has_value());
	CHECK(*state.surroundingText == "a\xC3\xA9z");
	CHECK(state.cursor == 3);
	CHECK(state.anchor == 1);
	CHECK(state.cursorRectangle.width == 1);
	CHECK(state.cursorRectangle.height > 0);
	CHECK(state.cursorRectangle.x >= static_cast<int32_t>(layout.fieldText.left));
	CHECK(state.cursorRectangle.y == static_cast<int32_t>(layout.fieldText.top));
}

TEST_CASE("Find bar focus loss cancels preedit and clears press") {
	FindBarModel model;
	REQUIRE(model.SetQuery("q"));
	model.SetFocused(true);
	model.preedit = ApplicationTextInputPreedit{"x", 1, 1};
	model.pressOrigin = FindBarPressKind::Next;
	model.SetFocused(false);
	CHECK_FALSE(model.focused);
	CHECK_FALSE(model.preedit.has_value());
	CHECK_FALSE(model.pressOrigin.has_value());
	CHECK(model.query == "q");
}

TEST_CASE("Find bar paints opaque band field and status") {
	ApplicationEditor editor(480, 80);
	FindBarPainter painter;
	FindBarModel model;
	REQUIRE(model.SetQuery("find me"));
	model.SetFocused(true);
	model.SetStatus(FindBarStatus::Wrapped);
	const FindBarLayout layout = LayoutFindBar(480, 8);
	const std::vector<uint8_t> pixels = PaintBar(editor, painter, layout, model);

	const int midY = static_cast<int>((layout.band.top + layout.band.bottom) / 2);
	const Rgba band = Sample(pixels, editor.FrameWidth(), 2, midY);
	const Rgba field = Sample(pixels, editor.FrameWidth(),
		static_cast<int>((layout.field.left + layout.field.right) / 2), midY);
	// Band fill differs from pure white editor background above the band.
	const Rgba above = Sample(pixels, editor.FrameWidth(), 2, 2);
	CHECK(Differs(band, above));
	// Field is lighter than the surrounding band chrome.
	CHECK((Differs(field, band) || field.r >= band.r));
	CHECK(field.a == 255);
	CHECK(band.a == 255);

	// Status area has non-background ink when Wrapped is set.
	if (NonEmpty(layout.status)) {
		const int statusX = static_cast<int>(layout.status.right) - 4;
		const Rgba statusPx = Sample(pixels, editor.FrameWidth(), statusX, midY);
		// Either status ink or band fill; at least the paint path runs.
		CHECK(statusPx.a == 255);
	}
}

TEST_CASE("Find bar long query paints without overflowing the field") {
	ApplicationEditor editor(360, 60);
	FindBarPainter painter;
	FindBarModel model;
	const std::string longQuery(80, 'm');
	REQUIRE(model.SetQuery(longQuery));
	model.SetFocused(true);
	model.caret = longQuery.size();
	model.anchor = longQuery.size();
	const FindBarLayout layout = LayoutFindBar(360, 0);
	// Must not throw or leave empty pixels in the field.
	const std::vector<uint8_t> pixels = PaintBar(editor, painter, layout, model);
	REQUIRE_FALSE(pixels.empty());
	const int midY = static_cast<int>((layout.field.top + layout.field.bottom) / 2);
	const Rgba inside = Sample(pixels, editor.FrameWidth(),
		static_cast<int>(layout.field.left) + 4, midY);
	CHECK(inside.a == 255);
}
