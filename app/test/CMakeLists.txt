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
