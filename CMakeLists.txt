cmake_minimum_required(VERSION 3.28)

project(SentritsCore VERSION 0.2.2 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(DEFINED VIBE_BUILD_TESTS AND NOT DEFINED SENTRITS_BUILD_TESTS)
  set(SENTRITS_BUILD_TESTS "${VIBE_BUILD_TESTS}" CACHE BOOL
      "Build automated tests" FORCE)
endif()

option(SENTRITS_BUILD_TESTS "Build automated tests" ON)
set(SENTRITS_PACKAGED_WEB_ROOT "/${CMAKE_INSTALL_LIBDIR}/sentrits/www" CACHE PATH
    "Default packaged web asset root used by the runtime")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Dependencies)
include(ProjectOptions)
include(Sources)
include(Packaging)

add_library(vibe_core
  ${VIBE_CORE_SOURCES}
)

target_include_directories(vibe_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(vibe_core
  PUBLIC
    SENTRITS_DEFAULT_PACKAGED_WEB_ROOT="${SENTRITS_PACKAGED_WEB_ROOT}"
    SENTRITS_VERSION="${PROJECT_VERSION}"
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

sentrits_configure_packaging()

if(SENTRITS_BUILD_TESTS)
  include(Testing)
endif()
