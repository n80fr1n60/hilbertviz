add_library(hvcore STATIC
  file_io.c
  hilbert.c
  image.c
  palette.c
  png_writer.c
  ppm.c
  render.c
)

target_include_directories(hvcore PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(hvcore PUBLIC hv_warnings hv_config hv_hardening hv_sanitizers hv_coverage)
if(PNG_FOUND)
  target_compile_definitions(hvcore PRIVATE HV_HAVE_LIBPNG=1)
  target_link_libraries(hvcore PUBLIC PNG::PNG)
endif()

add_executable(hilbertviz main.c)
target_link_libraries(hilbertviz PRIVATE hvcore)
