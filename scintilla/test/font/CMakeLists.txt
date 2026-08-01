add_executable(fontTest
  FontEmojiTest.cxx
  FontFaceTest.cxx
  FontLayoutTest.cxx
  FontMeasureTest.cxx
  FontRasterTest.cxx
  FontTestMain.cxx
)

target_include_directories(fontTest PRIVATE ../../src ../unit)
target_link_libraries(fontTest PRIVATE scintilla_font)
target_compile_definitions(fontTest PRIVATE
  SCALPEL_TEST_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fonts"
)
target_compile_options(fontTest PRIVATE -Wall -Wextra)

add_test(NAME font COMMAND fontTest)
