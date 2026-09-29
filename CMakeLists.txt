cmake_minimum_required(VERSION 3.20)
project(FableForge VERSION 0.18.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# One self-contained exe: no compiler runtime DLLs next to it.
if(MINGW)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

option(FABLEFORGE_PROFILE "Enable local, on-demand Tracy CPU and D3D11 profiling" OFF)
add_library(fableforge_profile INTERFACE)
target_include_directories(fableforge_profile INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(FABLEFORGE_PROFILE)
  if(MINGW)
    message(FATAL_ERROR "The MinGW profiling build crashes during thread-local destruction under cold world loading. Use tools/build_profile.ps1 (clang-cl/MSVC ABI). Normal MinGW builds remain supported.")
  endif()
  include(FetchContent)
  set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
  set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
  set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
  set(TRACY_NO_BROADCAST ON CACHE BOOL "" FORCE)
  set(TRACY_NO_SYSTEM_TRACING ON CACHE BOOL "" FORCE)
  FetchContent_Declare(tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG 05cceee0df3b8d7c6fa87e9638af311dbabc63cb # v0.13.1
    GIT_PROGRESS FALSE)
  FetchContent_MakeAvailable(tracy)
  target_link_libraries(fableforge_profile INTERFACE Tracy::TracyClient)
  if(WIN32)
    include(cmake/TracyD3D11.cmake)
  endif()
  add_executable(fableforge_profile_threads EXCLUDE_FROM_ALL tests/test_profile_threads.cpp)
  target_link_libraries(fableforge_profile_threads PRIVATE fableforge_profile)
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
target_link_libraries(forgecore_slice PUBLIC fableforge_profile)
target_include_directories(forgecore_slice PUBLIC
  libs/forgecore/include
  vendor/third_party
  src
)

# "-dev" until the release is tagged (docs/RELEASE.md step 5 clears it)
set(FORGE_VERSION_SUFFIX "-rc.1")
add_compile_definitions(ALBION_VERSION="${PROJECT_VERSION}${FORGE_VERSION_SUFFIX}")
add_library(fableforge STATIC src/terrainexport.cpp src/foliageexport.cpp src/thingsexport.cpp src/stbterrain.cpp src/effects.cpp src/leveledit.cpp src/vanilla_props.cpp src/worldedit.cpp src/overworld.cpp src/stbrelocate.cpp src/lodbake.cpp src/stitch.cpp src/livelink.cpp src/backups.cpp src/navlines.cpp src/worldtiles.cpp
    src/stbcompact.cpp src/presets.cpp src/gtg.cpp src/texturebrowse.cpp src/meshimport.cpp src/modpack.cpp)
target_include_directories(fableforge PUBLIC src)
target_link_libraries(fableforge PUBLIC forgecore_slice)

file(GLOB CLI_SOURCES CONFIGURE_DEPENDS src/cli/*.cpp)
add_executable(forge src/main.cpp ${CLI_SOURCES})
target_link_libraries(forge PRIVATE fableforge)

enable_testing()
add_executable(fableforge_tests tests/test_export.cpp)
target_link_libraries(fableforge_tests PRIVATE fableforge)
add_test(NAME fableforge_tests COMMAND fableforge_tests)

find_package(Threads REQUIRED)
add_executable(fableforge_row_tests tests/test_rowexecutor.cpp)
target_link_libraries(fableforge_row_tests PRIVATE fableforge_profile Threads::Threads)
add_test(NAME fableforge_row_tests COMMAND fableforge_row_tests)
set_tests_properties(fableforge_row_tests PROPERTIES TIMEOUT 30)

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
    gui/mods.cpp
    gui/automation.cpp
    gui/world.cpp
    gui/worldview.cpp
    gui/theme.cpp
    gui/renderer.cpp
  )
  target_include_directories(FableForge PRIVATE gui)
  target_link_libraries(FableForge PRIVATE fableforge albion_imgui)
  add_executable(fableforge_geometry_tests tests/test_renderer_prepare.cpp gui/renderer.cpp)
  target_include_directories(fableforge_geometry_tests PRIVATE gui)
  target_link_libraries(fableforge_geometry_tests PRIVATE fableforge albion_imgui)
  add_test(NAME fableforge_geometry_tests COMMAND fableforge_geometry_tests)
  if(FABLEFORGE_PROFILE)
    add_custom_command(TARGET FableForge POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tracy_SOURCE_DIR}/LICENSE $<TARGET_FILE_DIR:FableForge>/TRACY-LICENSE.txt)
  endif()
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

# The legacy forge CLI (quests, mods, saves, defs, scripts, UI, stb tooling...) from the
# old FableForge repo, kept alive here as forge-tools until its command families are folded
# into forge one by one. Same forgecore.
add_executable(forge-tools tools/forge-cli/main.cpp)
# fableforge too: the mod composer re-runs FableForge packs' recipes (src/modpack: model / theme imports)
target_link_libraries(forge-tools PRIVATE fableforge)
