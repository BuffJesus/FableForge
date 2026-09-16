cmake_minimum_required(VERSION 3.20)
project(AlbionAtlas VERSION 0.4.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# One self-contained exe: no compiler runtime DLLs next to it.
if(MINGW)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# FableForge's forgecore (parsers + writers), synced by tools/sync_forgecore.py
# (see vendor/VENDORED.md). audio.cpp is skipped; lzo.cpp is the MIT override.
file(GLOB FORGECORE_SOURCES CONFIGURE_DEPENDS vendor/forgecore/src/*.cpp)
list(FILTER FORGECORE_SOURCES EXCLUDE REGEX "audio[.]cpp$")
add_library(forgecore_slice STATIC
  ${FORGECORE_SOURCES}
  src/lzo1x.cpp
  vendor/third_party/miniz/miniz.c
)
target_include_directories(forgecore_slice PUBLIC
  vendor/forgecore/include
  vendor/third_party
  src
)

add_compile_definitions(ALBION_VERSION="${PROJECT_VERSION}")
add_library(albionatlas STATIC src/terrainexport.cpp src/foliageexport.cpp src/thingsexport.cpp src/stbterrain.cpp src/effects.cpp src/leveledit.cpp)
target_include_directories(albionatlas PUBLIC src)
target_link_libraries(albionatlas PUBLIC forgecore_slice)

add_executable(AlbionAtlas src/main.cpp)
target_link_libraries(AlbionAtlas PRIVATE albionatlas)

enable_testing()
add_executable(albionatlas_tests tests/test_export.cpp)
target_link_libraries(albionatlas_tests PRIVATE albionatlas)
add_test(NAME albionatlas_tests COMMAND albionatlas_tests)

# LZO1X decoder verification against minilzo (GPL; TEST BINARY ONLY, never shipped).
add_executable(albionatlas_lzo_tests tests/test_lzo.cpp tests/third_party/minilzo/minilzo.c)
target_include_directories(albionatlas_lzo_tests PRIVATE tests/third_party)
target_link_libraries(albionatlas_lzo_tests PRIVATE albionatlas)
add_test(NAME albionatlas_lzo_tests COMMAND albionatlas_lzo_tests)

# ---------------------------------------------------------------- GUI (Windows)
if(WIN32)
  set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui)
  add_library(albion_imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGuizmo/ImGuizmo.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/imgui_impl_dx11.cpp
  )
  target_include_directories(albion_imgui PUBLIC ${IMGUI_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGuizmo)
  target_link_libraries(albion_imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi ole32 shell32 uuid)

  add_executable(AlbionAtlasGUI WIN32
    gui/main.cpp
    gui/app.cpp
    gui/editor.cpp
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
