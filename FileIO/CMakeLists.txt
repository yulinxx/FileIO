cmake_minimum_required(VERSION 4.3)

# --- 模块元信息 ---
set(LIB_NAME "FileIO")
set(LIB_FOLDER "Modules")
set(LIB_DESCRIPTION "File IO Library")
set(LIB_EXPORT_MACRO "FILEIO_EXPORTS")

# --- 版本（按模块独立）---
set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)
set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

# --- 路径 ---
if(NOT SANYI_ROOT)
    get_filename_component(SANYI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
include("${SANYI_ROOT}/CMake/SanYiPaths.cmake")
include("${SANYI_ROOT}/CMake/Utils.cmake")

# --- 源文件 ---
# 先于 protobuf_generate 收集,因为需要先创建 target
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cc"
)



file(GLOB_RECURSE LIB_DXFRW_SOURCES CONFIGURE_DEPENDS
    "${SANYI_THIRDPARTY_DIR}/libdxfrw/src/*.cpp"
)

file(GLOB_RECURSE LIB_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)
list(FILTER LIB_SOURCES EXCLUDE REGEX "${SANYI_SOURCE_EXCLUDE_REGEX}")
list(FILTER LIB_HEADERS EXCLUDE REGEX "${SANYI_HEADER_EXCLUDE_REGEX}")
list(LENGTH LIB_SOURCES LIB_SOURCES_COUNT)
list(LENGTH LIB_HEADERS LIB_HEADERS_COUNT)

# --- 第三方依赖 ---
find_package(protobuf CONFIG REQUIRED)
find_package(NanoSVG CONFIG REQUIRED)
find_package(Stb REQUIRED)

get_target_property(NANOSVG_INCLUDE_DIR NanoSVG::nanosvg INTERFACE_INCLUDE_DIRECTORIES)

# zlib 可选:用于 SVGZ (gzip 压缩 SVG) 支持
find_package(ZLIB QUIET)

# --- 目标定义 (必须先于 protobuf_generate) ---
add_library(${LIB_NAME} SHARED
    ${LIB_SOURCES}
    ${LIB_HEADERS}
    ${LIB_DXFRW_SOURCES}
)
set(LIB_SOURCES_FOR_SOURCE_GROUP ${LIB_SOURCES})
if(VERSION_RESOURCE)
    list(REMOVE_ITEM LIB_SOURCES_FOR_SOURCE_GROUP "${VERSION_RESOURCE}")
endif()
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${LIB_SOURCES_FOR_SOURCE_GROUP} ${LIB_HEADERS})
if(VERSION_RESOURCE)
    source_group("Resources" FILES "${VERSION_RESOURCE}")
endif()

# --- Protobuf 代码生成 ---
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Generated/Proto")

if(NOT EXISTS "${PROTO_GEN_DIR}")
    set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated/Proto")
    file(MAKE_DIRECTORY ${PROTO_GEN_DIR})
endif()

set(PROTO_FILE "${PROTO_DIR}/SanYiDocument.proto")
protobuf_generate(
    TARGET ${LIB_NAME}
    PROTO_PATH "${PROTO_DIR}"
    PROTOC_OUT_DIR "${PROTO_GEN_DIR}"
    PROTOS "${PROTO_FILE}"
)

# --- 包含目录 ---
target_include_directories(${LIB_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/Include/FileIO"
        "${SANYI_THIRDPARTY_DIR}/libdxfrw/include"
        "${SANYI_THIRDPARTY_DIR}/libdxfrw/src"
        "${SANYI_THIRDPARTY_DIR}/libdxfrw/src/intern"
        "${PROTO_GEN_DIR}"
        "${Stb_INCLUDE_DIR}"
        "${NANOSVG_INCLUDE_DIR}"
        "${SANYI_ENGINE_COMMON_DIR}/Include"
        "${SANYI_ENGINE_COMMON_DIR}/Include/Engine"
    PRIVATE
        "${SANYI_ENGINE_2D_DIR}/Include"
        "${SANYI_ENGINE_2D_DIR}/Include/Engine2D"
        "${SANYI_ENGINE_3D_DIR}/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/Src/Internal"
)

target_compile_features(${LIB_NAME} PUBLIC cxx_std_17)

# --- 目标属性 ---
set_target_properties(${LIB_NAME} PROPERTIES
    FOLDER "${LIB_FOLDER}"
    DEBUG_POSTFIX "_d"
)

# --- 编译选项 ---
if(MSVC)
    target_compile_options(${LIB_NAME} PRIVATE /utf-8 /FS)
else()
    target_compile_options(${LIB_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- 链接 ---
target_link_libraries(${LIB_NAME}
    PUBLIC
        EngineCommon
        protobuf::libprotobuf-lite
        NanoSVG::nanosvg
    PRIVATE
        Engine2D
        Engine3D
        Log
)

# --- 可选模块条件块 ---
if(BUILD_GEOMODELCORE)
    if(NOT TARGET GeoModelCore)
        message(FATAL_ERROR "[${LIB_NAME}] BUILD_GEOMODELCORE=ON but GeoModelCore target is missing.")
    endif()
    target_link_libraries(${LIB_NAME} PRIVATE GeoModelCore)
    target_compile_definitions(${LIB_NAME} PRIVATE FILEIO_HAS_GEOMODELCORE=1)
    message(STATUS "  GeoModelCore (STEP import): ENABLED")
else()
    message(STATUS "  GeoModelCore (STEP import): DISABLED")
endif()

# 如果找到 zlib,链接并启用 SVGZ 支持
if(ZLIB_FOUND)
    target_link_libraries(${LIB_NAME} PUBLIC ZLIB::ZLIB)
    target_compile_definitions(${LIB_NAME} PRIVATE FILEIO_HAS_ZLIB)
endif()

target_compile_definitions(${LIB_NAME} PRIVATE ${LIB_EXPORT_MACRO})

# --- 单元测试 ---
option(BUILD_FILEIO_TESTS "Build FileIO unit tests" ON)

if(BUILD_FILEIO_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(Test)
endif()

# --- 版本信息 ---
sanyi_add_version_info(${LIB_NAME} "${LIB_NAME}" "${LIB_DESCRIPTION}")

# --- 构建信息 ---
message(STATUS "[${LIB_NAME}]")
message(STATUS "  Version: ${PROJECT_VERSION}")
# message(STATUS "  Sources: ${LIB_SOURCES_COUNT}  Headers: ${LIB_HEADERS_COUNT}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}(${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
if(BUILD_FILEIO_TESTS)
    message(STATUS "  Tests: ENABLED")
else()
    message(STATUS "  Tests: DISABLED")
endif()
message(STATUS "  C++ Standard: 17")
message(STATUS "  Protobuf: Enabled (lite runtime)")
message(STATUS "----------------------------------------")
