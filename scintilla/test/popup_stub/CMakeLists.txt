# Production ListBox/Menu stubs are not linked into editorTest (which supplies
# TestPlatform). This binary owns the production stub contract.

add_executable(popupStubTest
  PopupStubTest.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/AutoComplete.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/CharacterType.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/Geometry.cxx
)

target_include_directories(popupStubTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../unit
)
target_link_libraries(popupStubTest PRIVATE scintilla_platform_popups)
target_compile_options(popupStubTest PRIVATE -Wall -Wextra)

add_test(NAME popup-stub COMMAND popupStubTest)
