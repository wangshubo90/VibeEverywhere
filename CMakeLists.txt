cmake_minimum_required(VERSION 3.31)

project(VibeEverywhere VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VIBE_BUILD_TESTS "Build automated tests" ON)

add_library(vibe_warnings INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
  target_compile_options(vibe_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Werror
  )
endif()

add_library(vibe_core
  src/session/launch_spec.cpp
  src/session/provider_config.cpp
  src/session/session_record.cpp
  src/session/session_runtime.cpp
  src/session/session_types.cpp
  src/session/session_lifecycle.cpp
)

target_include_directories(vibe_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vibe_core
  PUBLIC
    vibe_warnings
)

add_executable(vibe-hostd
  src/main.cpp
)

target_link_libraries(vibe-hostd
  PRIVATE
    vibe_core
)

if(VIBE_BUILD_TESTS)
  include(CTest)
  include(FetchContent)

  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
  )
  FetchContent_MakeAvailable(googletest)

  add_executable(vibe_tests
    tests/session/launch_spec_test.cpp
    tests/session/provider_config_test.cpp
    tests/session/session_record_test.cpp
    tests/session/session_runtime_test.cpp
    tests/session/session_snapshot_test.cpp
    tests/session/session_types_test.cpp
    tests/session/session_lifecycle_test.cpp
  )

  target_link_libraries(vibe_tests
    PRIVATE
      vibe_core
      GTest::gtest_main
  )

  include(GoogleTest)
  gtest_discover_tests(vibe_tests)
endif()
