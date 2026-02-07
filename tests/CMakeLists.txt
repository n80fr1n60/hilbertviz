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
