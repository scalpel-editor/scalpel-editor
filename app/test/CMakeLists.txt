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

add_executable(waylandLifecycleTest WaylandLifecycleTest.cxx)
target_include_directories(waylandLifecycleTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandLifecycleTest PRIVATE scalpel_wayland)
target_compile_options(waylandLifecycleTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-lifecycle COMMAND waylandLifecycleTest)

add_executable(waylandInputTest WaylandInputTest.cxx)
target_include_directories(waylandInputTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(waylandInputTest PRIVATE scalpel_wayland)
target_compile_options(waylandInputTest PRIVATE -Wall -Wextra)

add_test(NAME wayland-input COMMAND waylandInputTest)

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
