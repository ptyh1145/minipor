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
