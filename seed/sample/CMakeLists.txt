set(SAMPLE_NAME "text_editor")
set(TARGET_NAME "${OWUI_SAMPLE_PREFIX}${SAMPLE_NAME}")

add_executable(${TARGET_NAME}
	src/main.cpp
	src/TextEditorEventListener.cpp
	src/TextEditorEventListener.h
	src/TextEditorWindow.cpp
	src/TextEditorWindow.h
)

set_common_target_options(${TARGET_NAME})

target_link_libraries(${TARGET_NAME} PRIVATE owui_scintilla owui_shell)

install_sample_target(${TARGET_NAME})
