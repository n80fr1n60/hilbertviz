set(HV_FUZZ_CORE_SOURCES
  ${PROJECT_SOURCE_DIR}/src/file_io.c
  ${PROJECT_SOURCE_DIR}/src/hilbert.c
  ${PROJECT_SOURCE_DIR}/src/image.c
  ${PROJECT_SOURCE_DIR}/src/palette.c
  ${PROJECT_SOURCE_DIR}/src/png_writer.c
  ${PROJECT_SOURCE_DIR}/src/ppm.c
  ${PROJECT_SOURCE_DIR}/src/render.c
  ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_target.c
)

add_library(hvfuzzcore STATIC ${HV_FUZZ_CORE_SOURCES})
target_include_directories(hvfuzzcore PUBLIC ${PROJECT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hvfuzzcore PUBLIC hv_warnings hv_config hv_coverage)

if(PNG_FOUND)
  target_compile_definitions(hvfuzzcore PRIVATE HV_HAVE_LIBPNG=1)
  target_link_libraries(hvfuzzcore PUBLIC PNG::PNG)
endif()

if(HILBERTVIZ_FUZZ_ENGINE STREQUAL "libfuzzer")
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "libFuzzer build requires clang (set CC=clang).")
  endif()

  set(HV_FUZZ_SANITIZE_COMMON "")
  if(HILBERTVIZ_FUZZ_SANITIZERS)
    set(HV_FUZZ_SANITIZE_COMMON ",address,undefined")
  endif()

  target_compile_options(hvfuzzcore PRIVATE -fsanitize=fuzzer-no-link${HV_FUZZ_SANITIZE_COMMON} -fno-omit-frame-pointer)
  if(HILBERTVIZ_FUZZ_SANITIZERS)
    target_link_options(hvfuzzcore PRIVATE -fsanitize=address,undefined)
  endif()

  add_executable(fuzz_pipeline_libfuzzer libfuzz_pipeline.c)
  target_link_libraries(fuzz_pipeline_libfuzzer PRIVATE hvfuzzcore)
  target_compile_options(fuzz_pipeline_libfuzzer PRIVATE -fsanitize=fuzzer${HV_FUZZ_SANITIZE_COMMON} -fno-omit-frame-pointer)
  target_link_options(fuzz_pipeline_libfuzzer PRIVATE -fsanitize=fuzzer${HV_FUZZ_SANITIZE_COMMON})

  add_executable(fuzz_file_slice_libfuzzer libfuzz_file_slice.c)
  target_link_libraries(fuzz_file_slice_libfuzzer PRIVATE hvfuzzcore)
  target_compile_options(fuzz_file_slice_libfuzzer PRIVATE -fsanitize=fuzzer${HV_FUZZ_SANITIZE_COMMON} -fno-omit-frame-pointer)
  target_link_options(fuzz_file_slice_libfuzzer PRIVATE -fsanitize=fuzzer${HV_FUZZ_SANITIZE_COMMON})
elseif(HILBERTVIZ_FUZZ_ENGINE STREQUAL "afl")
  add_executable(fuzz_pipeline_afl afl_pipeline.c)
  target_link_libraries(fuzz_pipeline_afl PRIVATE hvfuzzcore)

  add_executable(fuzz_file_slice_afl afl_file_slice.c)
  target_link_libraries(fuzz_file_slice_afl PRIVATE hvfuzzcore)

  add_custom_target(afl_tmux
    COMMAND ${CMAKE_COMMAND} -E chdir ${PROJECT_SOURCE_DIR}
      ${CMAKE_COMMAND} -E env
      TARGET=$<TARGET_FILE:fuzz_pipeline_afl>
      SESSION=hilbert-afl
      AUTO_SET_GOVERNOR=0
      ${CMAKE_CURRENT_SOURCE_DIR}/scripts/start_afl_tmux.sh
      --no-attach
    DEPENDS fuzz_pipeline_afl
    USES_TERMINAL
    COMMENT "Launch AFL++ tmux session (default 6 instances; set JOBS to override)"
  )
else()
  message(FATAL_ERROR "Unknown HILBERTVIZ_FUZZ_ENGINE='${HILBERTVIZ_FUZZ_ENGINE}'. Use libfuzzer or afl.")
endif()
