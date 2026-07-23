// Value types exchanged between the editor host and a platform text input.

#ifndef APPLICATIONTEXTINPUT_H
#define APPLICATIONTEXTINPUT_H

#include <cstdint>
#include <optional>
#include <string>

namespace Scalpel {

struct ApplicationTextInputRectangle {
	int32_t x = 0;
	int32_t y = 0;
	int32_t width = 0;
	int32_t height = 0;
};

enum class ApplicationTextChangeCause {
	InputMethod,
	Other,
};

struct ApplicationTextInputState {
	std::optional<std::string> surroundingText;
	int32_t cursor = 0;
	int32_t anchor = 0;
	ApplicationTextInputRectangle cursorRectangle;
	ApplicationTextChangeCause changeCause = ApplicationTextChangeCause::Other;
};

struct ApplicationTextInputPreedit {
	std::string text;
	int32_t cursorBegin = 0;
	int32_t cursorEnd = 0;
};

struct ApplicationTextInputDelete {
	uint32_t beforeLength = 0;
	uint32_t afterLength = 0;
};

struct ApplicationTextInputBatch {
	std::optional<ApplicationTextInputPreedit> preedit;
	std::optional<std::string> commit;
	std::optional<ApplicationTextInputDelete> deletion;
	bool refreshState = false;
	bool cancel = false;
};

}

#endif
