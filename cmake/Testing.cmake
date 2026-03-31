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

target_compile_definitions(vibe_tests
  PRIVATE
    VIBE_HOSTD_PATH="$<TARGET_FILE:sentrits>"
)

include(GoogleTest)
gtest_discover_tests(vibe_tests TEST_LIST VIBE_DISCOVERED_TESTS)
set_tests_properties(${VIBE_DISCOVERED_TESTS} PROPERTIES RUN_SERIAL TRUE)
