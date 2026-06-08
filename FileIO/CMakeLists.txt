cmake_minimum_required(VERSION 3.20)

set(LIB_NAME "FileIO")

file(GLOB_RECURSE FILEIO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cc"
)

file(GLOB_RECURSE LIB_DXFRW_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/ThirdParty/libdxfrw/src/*.cpp"
)

file(GLOB_RECURSE FILEIO_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)

add_library(${LIB_NAME} SHARED
    ${FILEIO_SOURCES}
    ${FILEIO_HEADERS}
    ${LIB_DXFRW_SOURCES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Header Files" FILES ${FILEIO_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${FILEIO_SOURCES})

target_include_directories(${LIB_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/Include/FileIO"
        "${CMAKE_SOURCE_DIR}/ThirdParty/libdxfrw/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/nanosvg/include"
)

target_compile_features(${LIB_NAME} PUBLIC cxx_std_17)

target_link_libraries(${LIB_NAME}
    PUBLIC
        Engine
)

target_compile_definitions(${LIB_NAME} PRIVATE FILEIO_EXPORTS)

if(MSVC)
    target_compile_options(${LIB_NAME} PRIVATE /utf-8 /FS)
endif()

message(STATUS "[${LIB_NAME}]")
message(STATUS "  Project: ${LIB_NAME}")
message(STATUS "  Version: 1.0.0")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}(${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Output Directory: ${CMAKE_BINARY_DIR}")
message(STATUS "  C++ Standard: 17")
message(STATUS "")
