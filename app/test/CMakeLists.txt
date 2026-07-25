add_executable(applicationActionTest
  ApplicationActionTest.cxx
  ApplicationTestMain.cxx
)
target_include_directories(applicationActionTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(applicationActionTest PRIVATE scalpel_application)
target_compile_options(applicationActionTest PRIVATE -Wall -Wextra)

add_test(NAME application-action COMMAND applicationActionTest)

add_executable(applicationHostTest
  ApplicationHostTest.cxx
  ApplicationTestMain.cxx
)
target_include_directories(applicationHostTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(applicationHostTest PRIVATE scalpel_application)
target_compile_options(applicationHostTest PRIVATE -Wall -Wextra)

add_test(NAME application-host COMMAND applicationHostTest)

add_executable(documentFileTest
  DocumentFileTest.cxx
  ApplicationTestMain.cxx
)
target_include_directories(documentFileTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(documentFileTest PRIVATE scalpel_application)
target_compile_options(documentFileTest PRIVATE -Wall -Wextra)

add_test(NAME document-file COMMAND documentFileTest)

add_executable(documentWorkspaceTest
  DocumentWorkspaceTest.cxx
  ApplicationTestMain.cxx
)
target_include_directories(documentWorkspaceTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(documentWorkspaceTest PRIVATE scalpel_application)
target_compile_options(documentWorkspaceTest PRIVATE -Wall -Wextra)

add_test(NAME document-workspace COMMAND documentWorkspaceTest)

add_executable(unsavedChangesPromptTest
  UnsavedChangesPromptTest.cxx
  ApplicationTestMain.cxx
)
target_include_directories(unsavedChangesPromptTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(unsavedChangesPromptTest PRIVATE scalpel_application)
target_compile_options(unsavedChangesPromptTest PRIVATE -Wall -Wextra)

add_test(NAME unsaved-changes-prompt COMMAND unsavedChangesPromptTest)

add_executable(unsavedChangesCardTest
  UnsavedChangesCardTest.cxx
  ApplicationTestMain.cxx
)
target_include_directories(unsavedChangesCardTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(unsavedChangesCardTest PRIVATE scalpel_application)
target_compile_options(unsavedChangesCardTest PRIVATE -Wall -Wextra)

add_test(NAME unsaved-changes-card COMMAND unsavedChangesCardTest)

add_executable(tabStripTest
  TabStripTest.cxx
  ApplicationTestMain.cxx
)
target_include_directories(tabStripTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(tabStripTest PRIVATE scalpel_application)
target_compile_options(tabStripTest PRIVATE -Wall -Wextra)

add_test(NAME tab-strip COMMAND tabStripTest)

add_executable(menuBarTest
  MenuBarTest.cxx
  ApplicationTestMain.cxx
)
target_include_directories(menuBarTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(menuBarTest PRIVATE scalpel_application)
target_compile_options(menuBarTest PRIVATE -Wall -Wextra)

add_test(NAME menu-bar COMMAND menuBarTest)

add_executable(applicationImeTest
  ApplicationImeTest.cxx
  ApplicationTestMain.cxx
)
target_include_directories(applicationImeTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(applicationImeTest PRIVATE scalpel_application)
target_compile_options(applicationImeTest PRIVATE -Wall -Wextra)

add_test(NAME application-ime COMMAND applicationImeTest)

add_executable(applicationTransferTest
  ApplicationTransferTest.cxx
  ApplicationTestMain.cxx
)
target_include_directories(applicationTransferTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(applicationTransferTest PRIVATE scalpel_application)
target_compile_options(applicationTransferTest PRIVATE -Wall -Wextra)

add_test(NAME application-transfer COMMAND applicationTransferTest)

add_executable(applicationInputTest
  ApplicationInputTest.cxx
  ApplicationTestMain.cxx
)
target_include_directories(applicationInputTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(applicationInputTest PRIVATE scalpel_application)
target_compile_options(applicationInputTest PRIVATE -Wall -Wextra)

add_test(NAME application-input COMMAND applicationInputTest)

add_executable(waylandCursorTest
  WaylandCursorTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandCursorTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandCursorTest PRIVATE scalpel_wayland)
target_compile_options(waylandCursorTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-cursor COMMAND waylandCursorTest)

add_executable(waylandScaleTest
  WaylandScaleTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandScaleTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandScaleTest PRIVATE scalpel_wayland)
target_compile_options(waylandScaleTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-scale COMMAND waylandScaleTest)

add_executable(waylandFrameTest
  WaylandFrameTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandFrameTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandFrameTest PRIVATE scalpel_wayland)
target_compile_options(waylandFrameTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-frame COMMAND waylandFrameTest)

add_executable(waylandWindowTest
  WaylandWindowTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandWindowTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandWindowTest PRIVATE scalpel_wayland)
target_compile_options(waylandWindowTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-window COMMAND waylandWindowTest)

add_executable(waylandRegistryTest
  WaylandRegistryTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandRegistryTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandRegistryTest PRIVATE scalpel_wayland)
target_compile_options(waylandRegistryTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-registry COMMAND waylandRegistryTest)

add_executable(waylandTextInputTest
  WaylandTextInputTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandTextInputTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandTextInputTest PRIVATE scalpel_wayland)
target_compile_options(waylandTextInputTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-text-input COMMAND waylandTextInputTest)

add_executable(waylandKeyboardTest
  WaylandKeyboardTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandKeyboardTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandKeyboardTest PRIVATE scalpel_wayland)
target_compile_options(waylandKeyboardTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-keyboard COMMAND waylandKeyboardTest)

add_executable(waylandPointerTest
  WaylandPointerTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandPointerTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandPointerTest PRIVATE scalpel_wayland)
target_compile_options(waylandPointerTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-pointer COMMAND waylandPointerTest)

add_executable(waylandByteTransferTest
  WaylandByteTransferTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandByteTransferTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandByteTransferTest PRIVATE scalpel_wayland)
target_compile_options(waylandByteTransferTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-byte-transfer COMMAND waylandByteTransferTest)

add_executable(waylandClipboardTest
  WaylandClipboardTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandClipboardTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandClipboardTest PRIVATE scalpel_wayland)
target_compile_options(waylandClipboardTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-clipboard COMMAND waylandClipboardTest)

add_executable(waylandPrimarySelectionTest
  WaylandPrimarySelectionTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandPrimarySelectionTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandPrimarySelectionTest PRIVATE scalpel_wayland)
target_compile_options(waylandPrimarySelectionTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-primary-selection COMMAND waylandPrimarySelectionTest)

add_executable(waylandPollTest
  WaylandPollTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandPollTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandPollTest PRIVATE scalpel_wayland)
target_compile_options(waylandPollTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-poll COMMAND waylandPollTest)

add_executable(waylandDbusTest
  WaylandDbusTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandDbusTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandDbusTest PRIVATE scalpel_wayland)
target_compile_options(waylandDbusTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-dbus COMMAND waylandDbusTest)

add_executable(waylandPortalTest
  PortalUriTest.cxx
  WaylandPortalTest.cxx
  WaylandTestMain.cxx
)
target_include_directories(waylandPortalTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandPortalTest PRIVATE scalpel_wayland)
target_compile_options(waylandPortalTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-portal COMMAND waylandPortalTest)

add_executable(waylandIntegrationTest WaylandIntegrationTest.cxx)
target_include_directories(waylandIntegrationTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandIntegrationTest PRIVATE
  scalpel_application
  scalpel_wayland
)
target_compile_options(waylandIntegrationTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-integration COMMAND waylandIntegrationTest)
