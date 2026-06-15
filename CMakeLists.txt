cmake_minimum_required(VERSION 3.20)

include(../Config.txt)

if(DEFINED VCPKG_DIR AND EXISTS "${VCPKG_DIR}")
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake")
    message(STATUS "Using VCPKG toolchain: ${CMAKE_TOOLCHAIN_FILE}")
else()
    message(WARNING "VCPKG directory error. Please set VCPKG_DIR in Config.txt")
endif()

project(SanYiFileIO
    VERSION 1.0.0
    DESCRIPTION "SanYi CAD File I/O Library (Import / Export / Save)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/wd4251)
    add_compile_options(/wd4244)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options(-Wno-conversion)
    add_compile_options(-Wno-float-conversion)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

add_subdirectory(FileIO)