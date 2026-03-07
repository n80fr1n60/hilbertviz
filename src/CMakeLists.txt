add_library(hvcore STATIC
  file_io.c
  hilbert.c
  hilbert3d.c
  image.c
  palette.c
  point_cloud3d.c
  png_writer.c
  ppm.c
  render.c
)

target_include_directories(hvcore PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(hvcore PUBLIC hv_warnings hv_config hv_hardening hv_sanitizers hv_coverage)
target_link_libraries(hvcore PUBLIC m)
if(PNG_FOUND)
  target_compile_definitions(hvcore PRIVATE HV_HAVE_LIBPNG=1)
  target_link_libraries(hvcore PUBLIC PNG::PNG)
endif()

add_executable(hilbertviz main.c)
target_link_libraries(hilbertviz PRIVATE hvcore)

add_library(hv3dapp STATIC
  3d_app.c
  3d_camera.c
  3d_platform.c
  3d_renderer.c
)
target_include_directories(hv3dapp PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(hv3dapp PUBLIC hvcore)
target_link_libraries(hv3dapp PRIVATE hv_warnings hv_config hv_hardening hv_sanitizers hv_coverage hv_3d_viewer_deps)
if(HILBERTVIZ_WITH_3D_VIEWER)
  target_compile_definitions(hv3dapp PRIVATE HV_3D_VIEWER_REQUESTED=1)
endif()
if(HILBERTVIZ_3D_VIEWER_AVAILABLE)
  target_compile_definitions(hv3dapp PRIVATE HV_3D_VIEWER_AVAILABLE=1)
endif()

add_executable(hilbertviz3d main_3d.c)
target_link_libraries(hilbertviz3d PRIVATE hv3dapp)
