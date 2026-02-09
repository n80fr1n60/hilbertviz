add_executable(test_hv test_unit.c)
target_include_directories(test_hv PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(test_hv PRIVATE hvcore hv_warnings hv_config hv_sanitizers hv_coverage)
if(PNG_FOUND)
  target_compile_definitions(test_hv PRIVATE HV_TEST_HAVE_PNG=1)
endif()

add_test(NAME hv_unit COMMAND test_hv)

add_test(
  NAME hv_cli_reject_negative_offset
  COMMAND $<TARGET_FILE:hilbertviz> ${PROJECT_SOURCE_DIR}/README.md -o ${CMAKE_BINARY_DIR}/cli_neg_offset.ppm --offset -1
)
set_tests_properties(
  hv_cli_reject_negative_offset
  PROPERTIES
    WILL_FAIL TRUE
)

add_test(
  NAME hv_cli_reject_plus_length
  COMMAND $<TARGET_FILE:hilbertviz> ${PROJECT_SOURCE_DIR}/README.md -o ${CMAKE_BINARY_DIR}/cli_plus_length.ppm --length +1
)
set_tests_properties(
  hv_cli_reject_plus_length
  PROPERTIES
    WILL_FAIL TRUE
)

add_test(
  NAME hv_cli_rect_requires_dimensions
  COMMAND $<TARGET_FILE:hilbertviz> ${PROJECT_SOURCE_DIR}/README.md -o ${CMAKE_BINARY_DIR}/cli_rect_missing_dims.ppm --layout rect-hilbert
)
set_tests_properties(
  hv_cli_rect_requires_dimensions
  PROPERTIES
    WILL_FAIL TRUE
)

add_test(
  NAME hv_cli_rect_dry_run
  COMMAND
    ${CMAKE_COMMAND} -E env
      bash
      -lc
      "out=\"$($<TARGET_FILE:hilbertviz> ${PROJECT_SOURCE_DIR}/README.md -o ${CMAKE_BINARY_DIR}/cli_rect_dry_run.ppm --layout rect-hilbert --dimensions 5x4 --dry-run 2>&1)\"; rc=$?; echo \"$out\"; [ \"$rc\" -eq 0 ] && printf '%s' \"$out\" | grep -q 'layout: rect-hilbert'"
)

add_test(
  NAME hv_cli_default_legend_path
  COMMAND
    ${CMAKE_COMMAND} -E env
      bash
      -lc
      "out_path='${CMAKE_BINARY_DIR}/cli_default_legend.ppm'; legend_path=\"${CMAKE_BINARY_DIR}/cli_default_legend.ppm.legend.txt\"; rm -f \"$out_path\" \"$legend_path\"; out=\"$($<TARGET_FILE:hilbertviz> ${PROJECT_SOURCE_DIR}/README.md -o \"$out_path\" --legend 2>&1)\"; rc=$?; echo \"$out\"; [ \"$rc\" -eq 0 ] && [ -f \"$legend_path\" ] && printf '%s' \"$out\" | grep -Fq \"Wrote legend $legend_path\""
)

add_test(
  NAME hv_cli_hilbert_dry_run_manual_order
  COMMAND
    ${CMAKE_COMMAND} -E env
      bash
      -lc
      "out=\"$($<TARGET_FILE:hilbertviz> ${PROJECT_SOURCE_DIR}/README.md -o ${CMAKE_BINARY_DIR}/cli_hilbert_dry_run_manual.ppm --order 3 --dry-run 2>&1)\"; rc=$?; echo \"$out\"; [ \"$rc\" -eq 0 ] && printf '%s' \"$out\" | grep -q 'layout: hilbert' && printf '%s' \"$out\" | grep -q 'order: 3' && printf '%s' \"$out\" | grep -q 'dimensions: 8x8'"
)

add_test(
  NAME hv_cli_hilbert_dry_run_paginate_large
  COMMAND
    ${CMAKE_COMMAND} -E env
      bash
      -lc
      "input='${CMAKE_BINARY_DIR}/cli_hilbert_dry_run_large.bin'; rm -f \"$input\"; truncate -s 17000000 \"$input\"; out=\"$($<TARGET_FILE:hilbertviz> \"$input\" -o ${CMAKE_BINARY_DIR}/cli_hilbert_dry_run_large.ppm --paginate --dry-run 2>&1)\"; rc=$?; echo \"$out\"; rm -f \"$input\"; [ \"$rc\" -eq 0 ] && printf '%s' \"$out\" | grep -q 'order: 12' && printf '%s' \"$out\" | grep -q 'page_count: 2'"
)

add_test(
  NAME hv_cov_reject_build_dir_parent_escape
  COMMAND
    ${CMAKE_COMMAND} -E env
      BUILD_DIR=../hv-coverage-escape
      GENERATE_HTML=0
      USE_AFL_CORPUS=0
      JOBS=1
      CC_BIN=cc
      bash
      -lc
      "out=\"$(${PROJECT_SOURCE_DIR}/fuzz/scripts/run_coverage.sh 2>&1)\"; rc=$?; echo \"$out\"; [ \"$rc\" -ne 0 ] && printf '%s' \"$out\" | grep -q 'Refusing unsafe BUILD_DIR'"
)

add_test(
  NAME hv_cov_reject_build_dir_absolute
  COMMAND
    ${CMAKE_COMMAND} -E env
      BUILD_DIR=/tmp/hv-coverage-absolute
      GENERATE_HTML=0
      USE_AFL_CORPUS=0
      JOBS=1
      CC_BIN=cc
      bash
      -lc
      "out=\"$(${PROJECT_SOURCE_DIR}/fuzz/scripts/run_coverage.sh 2>&1)\"; rc=$?; echo \"$out\"; [ \"$rc\" -ne 0 ] && printf '%s' \"$out\" | grep -q 'Refusing unsafe BUILD_DIR'"
)

if(HILBERTVIZ_COVERAGE)
  find_program(LCOV_EXECUTABLE lcov)
  find_program(GENHTML_EXECUTABLE genhtml)

  if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE)
    set(HV_COVERAGE_DIR "${CMAKE_BINARY_DIR}/coverage")
    set(HV_COVERAGE_INFO "${HV_COVERAGE_DIR}/coverage.info")
    set(HV_COVERAGE_FILTERED "${HV_COVERAGE_DIR}/coverage.filtered.info")
    set(HV_COVERAGE_HTML_DIR "${HV_COVERAGE_DIR}/html")

    add_custom_target(coverage_html
      COMMAND ${CMAKE_COMMAND} -E make_directory ${HV_COVERAGE_DIR}
      COMMAND ${LCOV_EXECUTABLE} --directory ${CMAKE_BINARY_DIR} --zerocounters
      COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
      COMMAND ${LCOV_EXECUTABLE} --capture --directory ${CMAKE_BINARY_DIR} --output-file ${HV_COVERAGE_INFO}
      COMMAND ${LCOV_EXECUTABLE} --remove ${HV_COVERAGE_INFO}
              "/usr/*" "*/tests/*" "*/fuzz/*" "*/build*/*"
              --output-file ${HV_COVERAGE_FILTERED}
      COMMAND ${GENHTML_EXECUTABLE} ${HV_COVERAGE_FILTERED} --output-directory ${HV_COVERAGE_HTML_DIR}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      DEPENDS test_hv
      USES_TERMINAL
      COMMENT "Generate HTML coverage report at ${HV_COVERAGE_HTML_DIR}/index.html"
    )
  else()
    message(WARNING "Coverage enabled but lcov/genhtml not found. Install lcov to use target 'coverage_html'.")
  endif()
endif()
