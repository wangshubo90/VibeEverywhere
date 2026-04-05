find_package(Boost 1.87 REQUIRED CONFIG COMPONENTS json)
find_package(OpenSSL REQUIRED)

include(FetchContent)

if(NOT TARGET vterm_vendor)
  FetchContent_Declare(
    libvterm
    URL https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz
  )
  FetchContent_MakeAvailable(libvterm)

  add_library(vterm_vendor STATIC
    ${libvterm_SOURCE_DIR}/src/encoding.c
    ${libvterm_SOURCE_DIR}/src/keyboard.c
    ${libvterm_SOURCE_DIR}/src/mouse.c
    ${libvterm_SOURCE_DIR}/src/parser.c
    ${libvterm_SOURCE_DIR}/src/pen.c
    ${libvterm_SOURCE_DIR}/src/screen.c
    ${libvterm_SOURCE_DIR}/src/state.c
    ${libvterm_SOURCE_DIR}/src/unicode.c
    ${libvterm_SOURCE_DIR}/src/vterm.c
  )

  target_include_directories(vterm_vendor
    PUBLIC
      ${libvterm_SOURCE_DIR}/include
    PRIVATE
      ${libvterm_SOURCE_DIR}/src
  )
endif()

if(VIBE_BUILD_TESTS)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
  )
  FetchContent_MakeAvailable(googletest)
endif()
