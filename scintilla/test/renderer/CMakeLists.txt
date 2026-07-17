# Offscreen GL renderer tests (no Wayland compositor).
# Links scintilla_render only — does not define Surface::Allocate for editorTest.

add_executable(rendererTest
  RendererTest.cxx
)

target_include_directories(rendererTest PRIVATE ../../src ../unit)
target_link_libraries(rendererTest PRIVATE scintilla_render)
target_compile_options(rendererTest PRIVATE -Wall -Wextra)

add_test(NAME renderer COMMAND rendererTest)
