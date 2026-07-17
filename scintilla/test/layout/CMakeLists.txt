# Layout tests: measure-only surface and line layout over shaped runs.

add_executable(layoutTest
  LayoutTest.cxx
)

target_include_directories(layoutTest PRIVATE ../../src ../unit)
target_link_libraries(layoutTest PRIVATE scintilla_font)
target_compile_definitions(layoutTest PRIVATE
  SCALPEL_TEST_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fonts"
)
target_compile_options(layoutTest PRIVATE -Wall -Wextra)

add_test(NAME layout COMMAND layoutTest)
