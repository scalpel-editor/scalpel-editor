add_executable(applicationTest ApplicationEditorTest.cxx)
target_include_directories(applicationTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(applicationTest PRIVATE scalpel_application)
target_compile_options(applicationTest PRIVATE -Wall -Wextra)

add_test(NAME application COMMAND applicationTest)

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
