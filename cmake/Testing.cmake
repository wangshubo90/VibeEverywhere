include(CTest)
include(${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake)

if(TARGET GTest::gtest_main)
  set(VIBE_GTEST_MAIN_TARGET GTest::gtest_main)
elseif(TARGET gtest_main)
  set(VIBE_GTEST_MAIN_TARGET gtest_main)
else()
  message(FATAL_ERROR "googletest main target was not created")
endif()

add_executable(vibe_tests
  ${VIBE_TEST_SOURCES}
)

target_link_libraries(vibe_tests
  PRIVATE
    vibe_core
    ${VIBE_GTEST_MAIN_TARGET}
)

target_compile_definitions(vibe_tests
  PRIVATE
    VIBE_HOSTD_PATH="$<TARGET_FILE:sentrits>"
)

include(GoogleTest)
gtest_discover_tests(vibe_tests TEST_LIST VIBE_DISCOVERED_TESTS)
set_tests_properties(${VIBE_DISCOVERED_TESTS} PROPERTIES RUN_SERIAL TRUE)
