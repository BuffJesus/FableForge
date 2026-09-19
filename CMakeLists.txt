cmake_minimum_required(VERSION 3.20)
project(FableForge VERSION 0.16.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# One self-contained exe: no compiler runtime DLLs next to it.
if(MINGW)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# forgecore (parsers + writers): canonical here since the rename (libs/forgecore)
# (see vendor/VENDORED.md). audio.cpp is skipped; lzo.cpp is the MIT override.
file(GLOB FORGECORE_SOURCES CONFIGURE_DEPENDS libs/forgecore/src/*.cpp)
list(FILTER FORGECORE_SOURCES EXCLUDE REGEX "audio[.]cpp$")
add_library(forgecore_slice STATIC
  ${FORGECORE_SOURCES}
  src/lzo1x.cpp
  vendor/third_party/miniz/miniz.c
)
target_include_directories(forgecore_slice PUBLIC
  libs/forgecore/include
  vendor/third_party
  src
)

add_compile_definitions(ALBION_VERSION="${PROJECT_VERSION}")
add_library(fableforge STATIC src/terrainexport.cpp src/foliageexport.cpp src/thingsexport.cpp src/stbterrain.cpp src/effects.cpp src/leveledit.cpp src/worldedit.cpp src/overworld.cpp src/stbrelocate.cpp src/lodbake.cpp src/stitch.cpp src/livelink.cpp src/backups.cpp src/presets.cpp src/gtg.cpp src/texturebrowse.cpp)
target_include_directories(fableforge PUBLIC src)
target_link_libraries(fableforge PUBLIC forgecore_slice)

add_executable(forge src/main.cpp)
target_link_libraries(forge PRIVATE fableforge)

enable_testing()
add_executable(fableforge_tests tests/test_export.cpp)
target_link_libraries(fableforge_tests PRIVATE fableforge)
add_test(NAME fableforge_tests COMMAND fableforge_tests)

# LZO1X decoder verification against minilzo (GPL; TEST BINARY ONLY, never shipped).
add_executable(fableforge_lzo_tests tests/test_lzo.cpp tests/third_party/minilzo/minilzo.c)
target_include_directories(fableforge_lzo_tests PRIVATE tests/third_party)
target_link_libraries(fableforge_lzo_tests PRIVATE fableforge)
add_test(NAME fableforge_lzo_tests COMMAND fableforge_lzo_tests)

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

  add_executable(FableForge WIN32
    gui/main.cpp
    gui/app.cpp
    gui/editor.cpp
    gui/textures.cpp
    gui/world.cpp
    gui/theme.cpp
    gui/renderer.cpp
  )
  target_include_directories(FableForge PRIVATE gui)
  target_link_libraries(FableForge PRIVATE fableforge albion_imgui)
  if(MINGW)
    target_compile_options(FableForge PRIVATE -municode)
    target_link_options(FableForge PRIVATE -municode -mwindows -static -static-libgcc -static-libstdc++)
  endif()
endif()

add_executable(probe_trailer EXCLUDE_FROM_ALL tools/probe_trailer.cpp)
target_link_libraries(probe_trailer PRIVATE fableforge)

add_executable(probe_bwd EXCLUDE_FROM_ALL tools/probe_bwd.cpp)
target_link_libraries(probe_bwd PRIVATE fableforge)

# the shipped presets live next to the exe (presets/*.preset.tng)
add_custom_command(TARGET FableForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/presets $<TARGET_FILE_DIR:FableForge>/presets)
