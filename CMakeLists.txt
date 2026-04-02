cmake_minimum_required(VERSION 3.28)

project(VibeEverywhere VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(VIBE_BUILD_TESTS "Build automated tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Dependencies)
include(ProjectOptions)
include(Sources)

add_library(vibe_core
  ${VIBE_CORE_SOURCES}
)

target_include_directories(vibe_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vibe_core
  PUBLIC
    Boost::headers
    Boost::json
    OpenSSL::SSL
    OpenSSL::Crypto
    vterm_vendor
    vibe_warnings
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(vibe_core PUBLIC util)
endif()

add_executable(sentrits
  src/main.cpp
)

target_link_libraries(sentrits
  PRIVATE
    vibe_core
)

add_executable(vibe-hostd
  src/main.cpp
)

target_link_libraries(vibe-hostd
  PRIVATE
    vibe_core
)

if(VIBE_BUILD_TESTS)
  include(Testing)
endif()
