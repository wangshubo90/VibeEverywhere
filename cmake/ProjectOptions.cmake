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

  if(SENTRITS_STRICT_CI_WARNINGS)
    target_compile_options(vibe_warnings INTERFACE
      -Wmissing-field-initializers
    )
  endif()
endif()
