#pragma once

#include <OnlyWayUi/Core/Element.h>
#include <OnlyWayUi/Core/EventListener.h>
#include <OnlyWayUi/Core/EventListenerInstancer.h>

class TextEditorWindow;

class TextEditorEventListener : public OnlyWayUi::EventListener {
public:
	TextEditorEventListener(const OnlyWayUi::String& value, OnlyWayUi::Element* element, TextEditorWindow* window);

	void ProcessEvent(OnlyWayUi::Event& event) override;
	void OnDetach(OnlyWayUi::Element* element) override;

private:
	OnlyWayUi::String value;
	OnlyWayUi::Element* element = nullptr;
	TextEditorWindow* window = nullptr;
};

class TextEditorEventListenerInstancer : public OnlyWayUi::EventListenerInstancer {
public:
	explicit TextEditorEventListenerInstancer(TextEditorWindow* window);

	OnlyWayUi::EventListener* InstanceEventListener(const OnlyWayUi::String& value, OnlyWayUi::Element* element) override;

private:
	TextEditorWindow* window = nullptr;
};
