cmake_minimum_required(VERSION 3.20)
project(AlbionTerrain VERSION 0.1.0 LANGUAGES C CXX)

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
  vendor/third_party/miniz/miniz.c
  vendor/third_party/minilzo/minilzo.c
)
target_include_directories(forgecore_slice PUBLIC
  vendor/forgecore/include
  vendor/third_party
)

add_library(albionterrain STATIC src/terrainexport.cpp)
target_include_directories(albionterrain PUBLIC src)
target_link_libraries(albionterrain PUBLIC forgecore_slice)

add_executable(AlbionTerrain src/main.cpp)
target_link_libraries(AlbionTerrain PRIVATE albionterrain)

enable_testing()
add_executable(albionterrain_tests tests/test_export.cpp)
target_link_libraries(albionterrain_tests PRIVATE albionterrain)
add_test(NAME albionterrain_tests COMMAND albionterrain_tests)
