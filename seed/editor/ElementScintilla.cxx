#include "ElementScintilla.h"
#include "ScintillaOWUI.h"
#include <OnlyWayUi/Core/CallbackTexture.h>
#include <OnlyWayUi/Core/Colour.h>
#include <OnlyWayUi/Core/Context.h>
#include <OnlyWayUi/Core/ElementDocument.h>
#include <OnlyWayUi/Core/ElementScroll.h>
#include <OnlyWayUi/Core/ElementUtilities.h>
#include <OnlyWayUi/Core/Event.h>
#include <OnlyWayUi/Core/EventListener.h>
#include <OnlyWayUi/Core/Geometry.h>
#include <OnlyWayUi/Core/ID.h>
#include <OnlyWayUi/Core/Input.h>
#include <OnlyWayUi/Core/Mesh.h>
#include <OnlyWayUi/Core/MeshUtilities.h>
#include <OnlyWayUi/Core/Property.h>
#include <OnlyWayUi/Core/Rectangle.h>
#include <OnlyWayUi/Core/RenderManager.h>
#include <OnlyWayUi/Core/RenderState.h>
#include <OnlyWayUi/Core/StyleTypes.h>
#include <OnlyWayUi/Core/Texture.h>
#include <algorithm>
#include <cmath>
#include <optional>

namespace OnlyWayUi::Editor {

namespace {

	int ModifierStateFromEvent(const Event& event)
	{
		int state = 0;
		if (event.GetParameter("shift_key", 0) > 0)
			state |= Input::KM_SHIFT;
		if (event.GetParameter("ctrl_key", 0) > 0)
			state |= Input::KM_CTRL;
		if (event.GetParameter("alt_key", 0) > 0)
			state |= Input::KM_ALT;
		if (event.GetParameter("meta_key", 0) > 0)
			state |= Input::KM_META;
		if (event.GetParameter("caps_lock_key", 0) > 0)
			state |= Input::KM_CAPSLOCK;
		if (event.GetParameter("num_lock_key", 0) > 0)
			state |= Input::KM_NUMLOCK;
		if (event.GetParameter("scroll_lock_key", 0) > 0)
			state |= Input::KM_SCROLLLOCK;
		return state;
	}

	Vector2f LocalMousePosition(Element* element, const Event& event)
	{
		Vector2f mouse_position(event.GetParameter("mouse_x", 0.f), event.GetParameter("mouse_y", 0.f));
		mouse_position -= element->GetAbsoluteOffset(BoxArea::Content);
		// Clamp into the content box so padding/border hits still map onto the first/last line
		// rather than negative Scintilla coordinates that hide the caret.
		const Vector2f content_size = element->GetBox().GetSize(BoxArea::Content);
		mouse_position.x = std::clamp(mouse_position.x, 0.f, std::max(0.f, content_size.x - 1.f));
		mouse_position.y = std::clamp(mouse_position.y, 0.f, std::max(0.f, content_size.y - 1.f));
		return mouse_position;
	}

} // namespace

class ElementScintilla::Impl final : public EventListener {
public:
	explicit Impl(ElementScintilla* element) : element(element), control(Invalidate, Notify, this)
	{
		element->SetProperty(PropertyId::Drag, Property(Style::Drag::Drag));

		element->AddEventListener(EventId::Mousedown, this, true);
		element->AddEventListener(EventId::Drag, this, true);
		element->AddEventListener(EventId::Dragend, this, true);
		element->AddEventListener(EventId::Mousescroll, this, true);
		element->AddEventListener(EventId::Keydown, this, true);
		element->AddEventListener(EventId::Textinput, this, true);
		element->AddEventListener(EventId::Focus, this, true);
		element->AddEventListener(EventId::Blur, this, true);
		element->AddEventListener(EventId::Scroll, this, true);
	}

	~Impl() override
	{
		if (scroll_callback_registered)
			control.SetScrollCallback(nullptr, nullptr);
		DetachDocumentMouseUpHook();
		element->RemoveEventListener(EventId::Mousedown, this, true);
		element->RemoveEventListener(EventId::Drag, this, true);
		element->RemoveEventListener(EventId::Dragend, this, true);
		element->RemoveEventListener(EventId::Mousescroll, this, true);
		element->RemoveEventListener(EventId::Keydown, this, true);
		element->RemoveEventListener(EventId::Textinput, this, true);
		element->RemoveEventListener(EventId::Focus, this, true);
		element->RemoveEventListener(EventId::Blur, this, true);
		element->RemoveEventListener(EventId::Scroll, this, true);
	}

	void AttachDocumentMouseUpHook()
	{
		if (document_mouseup_hooked)
			return;
		if (ElementDocument* document = element->GetOwnerDocument())
		{
			// Capture on the document so ButtonUp still runs when the pointer is released outside the editor.
			document->AddEventListener(EventId::Mouseup, this, true);
			document_mouseup_hooked = true;
			mouseup_document = document;
		}
	}

	void DetachDocumentMouseUpHook()
	{
		if (!document_mouseup_hooked)
			return;
		if (mouseup_document)
			mouseup_document->RemoveEventListener(EventId::Mouseup, this, true);
		mouseup_document = nullptr;
		document_mouseup_hooked = false;
	}

	static void Notify(void* user_data, ScintillaOWUI::Notice notice)
	{
		auto* self = static_cast<Impl*>(user_data);
		if (!self->observer)
			return;
		switch (notice)
		{
		case ScintillaOWUI::Notice::SavePointReached: self->observer->OnEditorDirtyChanged(false); break;
		case ScintillaOWUI::Notice::SavePointLeft: self->observer->OnEditorDirtyChanged(true); break;
		case ScintillaOWUI::Notice::TextChanged: self->observer->OnEditorTextChanged(); break;
		}
	}

	static void Invalidate(void* user_data, float left, float top, float right, float bottom)
	{
		auto* self = static_cast<Impl*>(user_data);
		const Rectanglef rectangle = Rectanglef::FromCorners({left, top}, {right, bottom});
		self->dirty_rectangle = self->dirty_rectangle ? self->dirty_rectangle->Join(rectangle) : rectangle;
		// Full-window damage so the GL frame-damage scissor cannot shrink a full-pane capture.
		// Element-only damage is usually enough, but caret blinks and small invalidates must not
		// leave a partial capture accepted as a full-size texture.
		if (Context* context = self->element->GetContext())
			context->DirtyRender();
		else
			self->element->DirtyRender();
	}

	static void Scroll(void* user_data, const ScintillaOWUI::ScrollState& state) { static_cast<Impl*>(user_data)->ApplyScrollState(state); }

	void ApplyScrollState(const ScintillaOWUI::ScrollState& state)
	{
		ElementScroll* scroll = element->GetElementScroll();
		const float containing_width = element->GetBox().GetSize(BoxArea::Padding).x;

		if (!state.word_wrap && state.horizontal_max > 0.f)
			scroll->EnableScrollbar(ElementScroll::HORIZONTAL, containing_width);
		else
			scroll->DisableScrollbar(ElementScroll::HORIZONTAL);

		if (state.vertical_max > 0.f)
			scroll->EnableScrollbar(ElementScroll::VERTICAL, containing_width);
		else
			scroll->DisableScrollbar(ElementScroll::VERTICAL);

		const Vector2f overflow_size(state.horizontal_max + element->GetClientWidth(), state.vertical_max + element->GetClientHeight());
		mirroring_scroll_state = true;
		element->SetScrollableOverflowRectangle(overflow_size, true);
		element->SetScrollLeft(state.left);
		element->SetScrollTop(state.top);
		mirroring_scroll_state = false;
		scroll->FormatScrollbars();
	}

	Vector2i GetViewportSize() const
	{
		const Vector2f content_size = element->GetBox().GetSize(BoxArea::Content);
		ElementScroll* scroll = element->GetElementScroll();
		return {
			std::max(0, static_cast<int>(std::lround(content_size.x - scroll->GetScrollbarSize(ElementScroll::VERTICAL)))),
			std::max(0, static_cast<int>(std::lround(content_size.y - scroll->GetScrollbarSize(ElementScroll::HORIZONTAL)))),
		};
	}

	void StabilizeViewportSize()
	{
		if (!scroll_callback_registered)
		{
			// Give Scintilla a real viewport before its initial range is published. A zero-height
			// viewport makes even an empty document look vertically scrollable.
			UpdateSize(GetViewportSize());
			scroll_callback_registered = true;
			control.SetScrollCallback(Scroll, this);
		}

		// A vertical bar narrows wrapped text, which can change the vertical range again.
		// Bound the retries so an unusual stylesheet cannot hold rendering in a size cycle.
		for (int pass = 0; pass < 3; ++pass)
		{
			const Vector2i viewport_size = GetViewportSize();
			if (viewport_size == size)
				break;
			UpdateSize(viewport_size);
		}
	}

	void UpdateSize(Vector2i new_size)
	{
		new_size.x = std::max(0, new_size.x);
		new_size.y = std::max(0, new_size.y);
		if (size == new_size)
			return;

		size = new_size;
		control.SetClientSize(size);
		texture.Release();
		geometry.Release();
		dirty_rectangle = Rectanglef::FromSize(Vector2f(size));
	}

	void Render(RenderManager& render_manager, Vector2f offset)
	{
		if (size.x <= 0 || size.y <= 0)
			return;

		// Geometry.Render requires a pixel-aligned translation.
		const Vector2f paint_origin = offset.Round();

		if (!geometry)
		{
			Mesh mesh;
			MeshUtilities::GenerateQuad(mesh, {}, Vector2f(size), ColourbPremultiplied(255), {}, {1, 1});
			geometry = render_manager.MakeGeometry(std::move(mesh));
		}

		// Rebuild the cached texture when Scintilla marked itself dirty. Paint in absolute
		// window coordinates so the GL backend's frame-damage scissor intersection stays correct
		// (a (0,0)-based capture is clipped to the wrong region under partial window damage).
		if (dirty_rectangle)
		{
			const Vector2i texture_size = size;
			CallbackTexture new_texture = render_manager.MakeCallbackTexture(
				[this, texture_size, paint_origin](const CallbackTextureInterface& texture_interface) -> bool {
					RenderManager& callback_render_manager = texture_interface.GetRenderManager();
					const RenderState initial_state = callback_render_manager.GetState();
					callback_render_manager.ResetState();

					const Rectanglei capture_region =
						Rectanglei::FromPositionSize(Vector2i(paint_origin), texture_size);

					// Clear the whole layer first. PushLayer's glClear is scissored; clearing only
					// the capture rect leaves uncleared edges that can sample into the top of the
					// saved texture under MSAA resolve.
					callback_render_manager.DisableScissorRegion();
					callback_render_manager.PushLayer();

					// Inflate the paint scissor one pixel so the capture top is not on the GL
					// scissor edge (MSAA + scissor can corrupt that boundary row).
					const Vector2i viewport = callback_render_manager.GetViewport();
					const Rectanglei paint_scissor =
						capture_region.Extend(1).Intersect(Rectanglei::FromSize(viewport));
					callback_render_manager.SetScissorRegion(paint_scissor);

					// Viewport clamp can shrink the region; refuse a partial pane.
					if (callback_render_manager.GetScissorRegion().Width() < texture_size.x ||
						callback_render_manager.GetScissorRegion().Height() < texture_size.y)
					{
						callback_render_manager.PopLayer();
						callback_render_manager.SetState(initial_state);
						return false;
					}

					const bool painted = control.Paint(callback_render_manager, paint_origin);

					// SaveLayerAsTexture uses the active scissor as the texture bounds. Restore the
					// exact capture rect so nested Scintilla line clips and the 1px inflate do not
					// change the saved size.
					callback_render_manager.SetScissorRegion(capture_region);
					if (!painted || callback_render_manager.GetScissorRegion().Size() != texture_size)
					{
						callback_render_manager.PopLayer();
						callback_render_manager.SetState(initial_state);
						return false;
					}

					texture_interface.SaveLayerAsTexture();
					callback_render_manager.PopLayer();
					callback_render_manager.SetState(initial_state);
					return true;
				});

			// Force generation so we know whether the capture succeeded.
			const Texture loaded(new_texture);
			if (loaded && loaded.GetDimensions() == texture_size)
			{
				texture = std::move(new_texture);
				dirty_rectangle.reset();
			}
			// On failure keep the previous texture and leave dirty_rectangle set so a later
			// frame (often with fuller damage) can retry.
			else
				new_texture.Release();
		}

		if (texture)
			geometry.Render(paint_origin, static_cast<OnlyWayUi::Texture>(texture));
	}

	void ProcessEvent(Event& event) override
	{
		switch (event.GetId())
		{
		case EventId::Mousedown:
		{
			if (event.GetTargetElement() != element)
				break;
			const int button = event.GetParameter("button", 0);
			const int modifiers = ModifierStateFromEvent(event);
			control.ButtonDown(button, LocalMousePosition(element, event), modifiers);
			if (control.HasMouseCapture())
				AttachDocumentMouseUpHook();
			// Ask for timer updates so caret blink and capture auto-scroll run.
			RequestTimerUpdate();
			// Do not StopPropagation: Context only arms Style::Drag after mousedown
			// finishes propagating. Stopping leaves drag unset, so Drag events never
			// fire and ButtonMove (live selection) never runs until ButtonUp.
		}
		break;

		case EventId::Drag:
		{
			if (event.GetTargetElement() != element)
				break;
			control.ButtonMove(LocalMousePosition(element, event), ModifierStateFromEvent(event));
			RequestTimerUpdate();
		}
		break;

		case EventId::Mouseup:
		case EventId::Dragend:
		{
			if (!control.HasMouseCapture() && event.GetTargetElement() != element)
				break;
			const int button = event.GetParameter("button", 0);
			control.ButtonUp(button, LocalMousePosition(element, event), ModifierStateFromEvent(event));
			DetachDocumentMouseUpHook();
			RequestTimerUpdate();
			if (event.GetTargetElement() == element)
				event.StopPropagation();
		}
		break;

		case EventId::Mousescroll:
		{
			// Generated scrollbar controls are non-DOM descendants. Treat wheel input over
			// their track and thumb as editor input too.
			const Vector2f wheel_delta(event.GetParameter("wheel_delta_x", 0.f), event.GetParameter("wheel_delta_y", 0.f));
			control.MouseWheel(wheel_delta, ModifierStateFromEvent(event));
			// Stop propagation so Context does not also smooth-scroll an RML ancestor.
			event.StopPropagation();
		}
		break;

		case EventId::Scroll:
		{
			if (event.GetTargetElement() != element || mirroring_scroll_state)
				break;
			control.SetScrollPosition({element->GetScrollLeft(), element->GetScrollTop()});
		}
		break;

		case EventId::Keydown:
		{
			if (event.GetTargetElement() != element)
				break;
			const int key_identifier = event.GetParameter("key_identifier", 0);
			// Leave Tab for focus traversal outside the editor.
			if (key_identifier == static_cast<int>(Input::KI_TAB))
				break;
			if (control.KeyDown(key_identifier, ModifierStateFromEvent(event)))
			{
				RequestTimerUpdate();
				event.StopPropagation();
			}
		}
		break;

		case EventId::Textinput:
		{
			if (event.GetTargetElement() != element)
				break;
			if (event.GetParameter("ctrl_key", 0) == 0 && event.GetParameter("alt_key", 0) == 0 && event.GetParameter("meta_key", 0) == 0)
			{
				const String text = event.GetParameter("text", String{});
				control.TextInput(text);
				RequestTimerUpdate();
			}
			event.StopPropagation();
		}
		break;

		case EventId::Focus:
		{
			if (event.GetTargetElement() != element)
				break;
			control.SetFocus(true);
			element->SetPseudoClass("focus-visible", true);
			RequestTimerUpdate();
		}
		break;

		case EventId::Blur:
		{
			if (event.GetTargetElement() != element)
				break;
			control.SetFocus(false);
			element->SetPseudoClass("focus-visible", false);
		}
		break;

		default: break;
		}
	}

	void OnUpdate()
	{
		control.TickTimers();
		const double next = control.SecondsUntilNextTimer();
		if (next >= 0.0)
			RequestTimerUpdate(next);
	}

	void RequestTimerUpdate(double delay = 0.0)
	{
		if (Context* context = element->GetContext())
			context->RequestNextUpdate(delay);
	}

	ElementScintilla* element;
	ScintillaOWUI control;
	Vector2i size;
	std::optional<Rectanglef> dirty_rectangle;
	CallbackTexture texture;
	Geometry geometry;
	ElementDocument* mouseup_document = nullptr;
	bool document_mouseup_hooked = false;
	bool mirroring_scroll_state = false;
	bool scroll_callback_registered = false;
	ElementScintilla::Observer* observer = nullptr;
};

ElementScintilla::ElementScintilla(const String& tag) : Element(tag), impl(std::make_unique<Impl>(this)) {}
ElementScintilla::~ElementScintilla() = default;

void ElementScintilla::SetObserver(Observer* observer)
{
	impl->observer = observer;
}

void ElementScintilla::SetText(const String& text)
{
	impl->control.SetText(text);
}
String ElementScintilla::GetText() const
{
	return impl->control.GetText();
}
ElementScintilla::DocumentHandle ElementScintilla::CreateDocument()
{
	return impl->control.CreateDocument();
}
void ElementScintilla::SetDocument(DocumentHandle document)
{
	impl->control.SetDocument(document);
}
void ElementScintilla::ReleaseDocument(DocumentHandle document)
{
	impl->control.ReleaseDocument(document);
}
void ElementScintilla::SetDocumentSavePoint(DocumentHandle document)
{
	impl->control.SetDocumentSavePoint(document);
}
void ElementScintilla::SetEditorFont(const String& family, int size)
{
	impl->control.SetFont(family, size);
}
void ElementScintilla::SetDarkTheme(bool dark)
{
	impl->control.SetDarkTheme(dark);
}
void ElementScintilla::SetWordWrap(bool word_wrap)
{
	impl->control.SetWordWrap(word_wrap);
}
bool ElementScintilla::GetWordWrap() const
{
	return impl->control.GetWordWrap();
}
int ElementScintilla::GetLineCount() const
{
	return impl->control.GetLineCount();
}
int ElementScintilla::GetCharacterCount() const
{
	return impl->control.GetCharacterCount();
}

void ElementScintilla::OnUpdate()
{
	impl->OnUpdate();
}

void ElementScintilla::OnRender()
{
	RenderManager* render_manager = GetRenderManager();
	if (!render_manager)
		return;

	const float dp_ratio = ElementUtilities::GetDensityIndependentPixelRatio(this);
	impl->control.SetPixelsPerInch(static_cast<int>(std::lround(96.0f * dp_ratio)));
	impl->StabilizeViewportSize();
	impl->Render(*render_manager, GetAbsoluteOffset(BoxArea::Content));
}

void ElementScintilla::OnResize()
{
	impl->control.InvalidateAll();
}
void ElementScintilla::OnDpRatioChange()
{
	impl->control.InvalidateAll();
}

} // namespace OnlyWayUi::Editor
