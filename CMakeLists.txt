# Review-only build entry point; original retained in provenance/.
cmake_minimum_required(VERSION 3.20)
project(FableForge VERSION 0.18.1 LANGUAGES C CXX)

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
set(FORGE_VERSION_SUFFIX "-rc.2")
add_compile_definitions(ALBION_VERSION="${PROJECT_VERSION}${FORGE_VERSION_SUFFIX}")
add_library(fableforge STATIC src/terrainexport.cpp src/foliageexport.cpp src/thingsexport.cpp src/stbterrain.cpp src/effects.cpp src/leveledit.cpp src/vanilla_props.cpp src/worldedit.cpp src/overworld.cpp src/stbrelocate.cpp src/lodbake.cpp src/stitch.cpp src/livelink.cpp src/backups.cpp src/navlines.cpp src/worldtiles.cpp
    src/stbcompact.cpp src/presets.cpp src/gtg.cpp src/texturebrowse.cpp src/textureproject.cpp src/texturereplacements.cpp src/meshimport.cpp src/modpack.cpp)
target_include_directories(fableforge PUBLIC src)
target_link_libraries(fableforge PUBLIC forgecore_slice)
target_sources(fableforge PRIVATE src/particlepreview.cpp)


if(NOT WIN32 OR NOT MINGW)
  message(FATAL_ERROR "Build the standalone app with the 64-bit MinGW-w64 toolchain. Build the game DLLs separately using tools/texture-runtime and MSVC Win32.")
endif()
add_executable(FableTextureExport WIN32 tools/texture-export/main.cpp tools/texture-export/collection.cpp tools/texture-export/import.cpp)
target_link_libraries(FableTextureExport PRIVATE fableforge ole32 shell32 uuid dwmapi)
target_link_options(FableTextureExport PRIVATE -municode)
