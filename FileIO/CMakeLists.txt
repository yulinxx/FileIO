cmake_minimum_required(VERSION 3.20)

set(LIB_NAME "FileIO")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../CMake/Utils.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../CMake/Utils.cmake")
endif()

# 版本号定义
set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)
set(PROJECT_VERSION_MAJOR ${VERSION_MAJOR})
set(PROJECT_VERSION_MINOR ${VERSION_MINOR})
set(PROJECT_VERSION_PATCH ${VERSION_PATCH})
set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

# ============================================================
# 源文件收集（先于 protobuf_generate，因为需要先创建 target）

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

# ============================================================
# 第三方库配置
# ============================================================
find_package(protobuf CONFIG REQUIRED)
find_package(NanoSVG CONFIG REQUIRED)
find_package(Stb REQUIRED)

get_target_property(NANOSVG_INCLUDE_DIR NanoSVG::nanosvg INTERFACE_INCLUDE_DIRECTORIES)

# zlib 可选：用于 SVGZ (gzip 压缩 SVG) 支持
find_package(ZLIB QUIET)

set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Generated/Proto")

if(NOT EXISTS "${PROTO_GEN_DIR}")
    set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated/Proto")
    file(MAKE_DIRECTORY ${PROTO_GEN_DIR})
endif()

# ============================================================
# 创建目标（必须先于 protobuf_generate）
add_library(${LIB_NAME} SHARED
    ${FILEIO_SOURCES}
    ${FILEIO_HEADERS}
    ${LIB_DXFRW_SOURCES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${FILEIO_SOURCES} ${FILEIO_HEADERS})

set(PROTO_FILE "${PROTO_DIR}/SanYiDocument.proto")
protobuf_generate(
    TARGET ${LIB_NAME}
    PROTO_PATH "${PROTO_DIR}"
    PROTOC_OUT_DIR "${PROTO_GEN_DIR}"
    PROTOS "${PROTO_FILE}"
)

# ============================================================
# Include 目录
# ============================================================

target_include_directories(${LIB_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/Include/FileIO"
        "${CMAKE_SOURCE_DIR}/ThirdParty/libdxfrw/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/libdxfrw/src"
        "${CMAKE_SOURCE_DIR}/ThirdParty/libdxfrw/src/intern"
        "${PROTO_GEN_DIR}"
        "${Stb_INCLUDE_DIR}"
        "${NANOSVG_INCLUDE_DIR}"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/Engine/3D/Include"
)

# ============================================================
# 编译选项
# ============================================================

target_compile_features(${LIB_NAME} PUBLIC cxx_std_17)

target_link_libraries(${LIB_NAME}
    PUBLIC
        EngineCommon
        Engine2D
        protobuf::libprotobuf-lite
        NanoSVG::nanosvg
    PRIVATE
        Engine3D
)

# 如果找到 zlib，链接并启用 SVGZ 支持
if(ZLIB_FOUND)
    target_link_libraries(${LIB_NAME} PUBLIC ZLIB::ZLIB)
    target_compile_definitions(${LIB_NAME} PRIVATE FILEIO_HAS_ZLIB)
endif()

target_compile_definitions(${LIB_NAME} PRIVATE FILEIO_EXPORTS)

if(MSVC)
    target_compile_options(${LIB_NAME} PRIVATE /utf-8 /FS)
endif()

option(BUILD_FILEIO_TESTS "Build FileIO unit tests" OFF)

if(BUILD_FILEIO_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(Test)
endif()

sanyi_add_version_info(${LIB_NAME} "${LIB_NAME}" "File IO Library")

message(STATUS "[${LIB_NAME}]")
message(STATUS "  Library: ${LIB_NAME}")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}(${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
if(BUILD_FILEIO_TESTS)
    message(STATUS "  Tests: ENABLED")
else()
    message(STATUS "  Tests: DISABLED")
endif()
message(STATUS "  C++ Standard: 17")
message(STATUS "  Protobuf: Enabled (lite runtime)")
message(STATUS "")
message(STATUS "----------------------------------------")