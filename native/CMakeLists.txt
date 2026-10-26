cmake_minimum_required(VERSION 3.16)
project(mw_preview CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(preview preview.cpp)
if (MSVC)
    target_compile_options(preview PRIVATE /O2 /w)
else()
    target_compile_options(preview PRIVATE -O2 -w)
endif()

# host-side validation of the shared terrain code (same sources the app uses)
add_executable(terrain_check terrain_check.cpp
    ../ios/core/src/terrain.cpp
    ../ios/core/src/assets_bin.cpp)
target_include_directories(terrain_check PRIVATE ../ios/core/include)
if (MSVC)
    target_compile_options(terrain_check PRIVATE /O2 /w)
else()
    target_compile_options(terrain_check PRIVATE -O2 -w)
endif()

add_executable(player_check player_check.cpp
    ../ios/core/src/player.cpp
    ../ios/core/src/terrain.cpp
    ../ios/core/src/assets_bin.cpp)
target_include_directories(player_check PRIVATE ../ios/core/include)
if (MSVC)
    target_compile_options(player_check PRIVATE /O2 /w)
else()
    target_compile_options(player_check PRIVATE -O2 -w)
endif()
