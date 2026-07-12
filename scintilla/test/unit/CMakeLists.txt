# Upstream unit tests (Catch2 v2 single header, vendored as catch.hpp).
# The test sources are the test*.cxx files in this directory; the code under
# test comes from scintilla_core. UnitTester.cxx is deliberately excluded: it
# is an alternate main() for the Visual Studio build.

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test*.cxx)

add_executable(unitTest
  unitTest.cxx
  ${TEST_SOURCES}
)

target_link_libraries(unitTest PRIVATE scintilla_core)

target_compile_options(unitTest PRIVATE -Wall -Wextra)

add_test(NAME unit COMMAND unitTest)
