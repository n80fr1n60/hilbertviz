cmake_minimum_required(VERSION 3.20)
project(hilbertviz VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(HILBERTVIZ_WARNINGS_AS_ERRORS "Treat warnings as errors." OFF)
option(HILBERTVIZ_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer." OFF)
option(HILBERTVIZ_WITH_PNG "Enable PNG output when libpng is available." ON)
option(HILBERTVIZ_COVERAGE "Enable coverage instrumentation." OFF)
option(HILBERTVIZ_FUZZ "Build fuzz targets." OFF)
set(HILBERTVIZ_FUZZ_ENGINE "libfuzzer" CACHE STRING "Fuzz engine: libfuzzer or afl")
set_property(CACHE HILBERTVIZ_FUZZ_ENGINE PROPERTY STRINGS libfuzzer afl)
option(HILBERTVIZ_FUZZ_SANITIZERS "Enable ASan/UBSan for fuzz targets." ON)

add_library(hv_warnings INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(hv_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wformat=2
    -Wstrict-prototypes
    -Wundef
    -Wnull-dereference
    -Werror=implicit-function-declaration
  )
  if(HILBERTVIZ_WARNINGS_AS_ERRORS)
    target_compile_options(hv_warnings INTERFACE -Werror)
  endif()
endif()

add_library(hv_config INTERFACE)
target_compile_definitions(hv_config INTERFACE _FILE_OFFSET_BITS=64 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)

add_library(hv_hardening INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(hv_hardening INTERFACE
    $<$<CONFIG:Release>:-fstack-protector-strong>
    $<$<CONFIG:Release>:-fPIE>
  )
  target_compile_definitions(hv_hardening INTERFACE
    $<$<CONFIG:Release>:_FORTIFY_SOURCE=2>
  )
  target_link_options(hv_hardening INTERFACE
    $<$<CONFIG:Release>:-pie>
  )
endif()

add_library(hv_sanitizers INTERFACE)
if(HILBERTVIZ_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(hv_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(hv_sanitizers INTERFACE -fsanitize=address,undefined)
endif()

add_library(hv_coverage INTERFACE)
if(HILBERTVIZ_COVERAGE)
  get_filename_component(HV_C_COMPILER_BASENAME "${CMAKE_C_COMPILER}" NAME)
  set(HV_ENABLE_COVERAGE_FLAGS ON)

  if(HV_C_COMPILER_BASENAME MATCHES "^afl-")
    set(HV_ENABLE_COVERAGE_FLAGS OFF)
    message(WARNING "Coverage instrumentation is skipped for AFL compiler wrapper '${HV_C_COMPILER_BASENAME}'.")
  elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    execute_process(
      COMMAND ${CMAKE_C_COMPILER} --print-file-name=libclang_rt.profile.a
      OUTPUT_VARIABLE HV_CLANG_PROFILE_RT
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if((HV_CLANG_PROFILE_RT STREQUAL "libclang_rt.profile.a") OR (NOT EXISTS "${HV_CLANG_PROFILE_RT}"))
      set(HV_ENABLE_COVERAGE_FLAGS OFF)
      message(WARNING "Coverage requested but clang profile runtime was not found. Use GCC/cc or install clang profile runtime.")
    endif()
  endif()

  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    if(HV_ENABLE_COVERAGE_FLAGS)
      target_compile_options(hv_coverage INTERFACE -O0 -g --coverage)
      target_link_options(hv_coverage INTERFACE --coverage)
    endif()
  else()
    message(WARNING "Coverage requested but compiler '${CMAKE_C_COMPILER_ID}' is not configured for --coverage flags.")
  endif()
endif()

if(HILBERTVIZ_WITH_PNG)
  find_package(PNG)
endif()

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

if(HILBERTVIZ_FUZZ)
  add_subdirectory(fuzz)
endif()
