cmake_minimum_required(VERSION 3.20)
project(AlbionAtlas VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# One self-contained exe: no compiler runtime DLLs next to it.
if(MINGW)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# Vendored, pinned snapshot of FableForge's forgecore parsers (see
# vendor/VENDORED.md). Only the modules the exporter needs.
add_library(forgecore_slice STATIC
  vendor/forgecore/src/lev.cpp
  vendor/forgecore/src/terrain.cpp
  vendor/forgecore/src/terraintex.cpp
  vendor/forgecore/src/big.cpp
  vendor/forgecore/src/bin.cpp
  vendor/forgecore/src/defschema.cpp
  vendor/forgecore/src/defdecode.cpp
  vendor/forgecore/src/lzo.cpp
  vendor/forgecore/src/env.cpp
  vendor/forgecore/src/wad.cpp
  vendor/forgecore/src/foliage.cpp
  vendor/forgecore/src/meshpreview.cpp
  vendor/forgecore/src/stb.cpp
  vendor/forgecore/src/stbinfo.cpp
  vendor/forgecore/src/rangecodec.cpp
  vendor/forgecore/src/tng.cpp
  vendor/forgecore/src/wld.cpp
  vendor/third_party/miniz/miniz.c
  vendor/third_party/minilzo/minilzo.c
)
target_include_directories(forgecore_slice PUBLIC
  vendor/forgecore/include
  vendor/third_party
)

add_library(albionatlas STATIC src/terrainexport.cpp src/foliageexport.cpp src/thingsexport.cpp)
target_include_directories(albionatlas PUBLIC src)
target_link_libraries(albionatlas PUBLIC forgecore_slice)

add_executable(AlbionAtlas src/main.cpp)
target_link_libraries(AlbionAtlas PRIVATE albionatlas)

enable_testing()
add_executable(albionatlas_tests tests/test_export.cpp)
target_link_libraries(albionatlas_tests PRIVATE albionatlas)
add_test(NAME albionatlas_tests COMMAND albionatlas_tests)

# ---------------------------------------------------------------- GUI (Windows)
if(WIN32)
  set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui)
  add_library(albion_imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/imgui_impl_dx11.cpp
  )
  target_include_directories(albion_imgui PUBLIC ${IMGUI_DIR})
  target_link_libraries(albion_imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi ole32 shell32 uuid)

  add_executable(AlbionAtlasGUI WIN32
    gui/main.cpp
    gui/app.cpp
    gui/theme.cpp
    gui/renderer.cpp
  )
  target_include_directories(AlbionAtlasGUI PRIVATE gui)
  target_link_libraries(AlbionAtlasGUI PRIVATE albionatlas albion_imgui)
  if(MINGW)
    target_compile_options(AlbionAtlasGUI PRIVATE -municode)
    target_link_options(AlbionAtlasGUI PRIVATE -municode -mwindows -static -static-libgcc -static-libstdc++)
  endif()
endif()
