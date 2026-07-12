#include "TextEditorEventListener.h"
#include "TextEditorWindow.h"

TextEditorEventListener::TextEditorEventListener(const OnlyWayUi::String& value, OnlyWayUi::Element* element, TextEditorWindow* window) :
	value(value), element(element), window(window)
{}

void TextEditorEventListener::ProcessEvent(OnlyWayUi::Event& event)
{
	if (window)
		window->HandleAction(value, element, event);
}

void TextEditorEventListener::OnDetach(OnlyWayUi::Element* /*element*/)
{
	delete this;
}

TextEditorEventListenerInstancer::TextEditorEventListenerInstancer(TextEditorWindow* window) : window(window) {}

OnlyWayUi::EventListener* TextEditorEventListenerInstancer::InstanceEventListener(const OnlyWayUi::String& value, OnlyWayUi::Element* element)
{
	return new TextEditorEventListener(value, element, window);
}
