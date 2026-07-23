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

add_executable(waylandTransferTest WaylandTransferTest.cxx)
target_include_directories(waylandTransferTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandTransferTest PRIVATE scalpel_wayland)
target_compile_options(waylandTransferTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-transfer COMMAND waylandTransferTest)

add_executable(waylandEventLoopTest WaylandEventLoopTest.cxx)
target_include_directories(waylandEventLoopTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandEventLoopTest PRIVATE scalpel_wayland)
target_compile_options(waylandEventLoopTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-event-loop COMMAND waylandEventLoopTest)

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
