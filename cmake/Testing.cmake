include(CTest)
include(${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake)

add_executable(vibe_tests
  ${VIBE_TEST_SOURCES}
)

target_link_libraries(vibe_tests
  PRIVATE
    vibe_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(vibe_tests)
