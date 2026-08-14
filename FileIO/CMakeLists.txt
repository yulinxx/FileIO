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

# --- 第三方依赖 ---
find_package(protobuf CONFIG REQUIRED)
find_package(NanoSVG CONFIG REQUIRED)
find_package(Stb REQUIRED)

get_target_property(NANOSVG_INCLUDE_DIR NanoSVG::nanosvg INTERFACE_INCLUDE_DIRECTORIES)

# zlib 可选:用于 SVGZ (gzip 压缩 SVG) 支持
find_package(ZLIB QUIET)

# --- Protobuf 代码生成 ---
# 始终输出到构建目录，避免生成的中间文件污染源码树
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated/Proto")
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

set(PROTO_FILE "${PROTO_DIR}/SanYiDocument.proto")

# --- 统一入口创建 DLL ---
# 注意: source_group 已在统一入口内部处理(外部文件自动归入 External 组),
# libdxfrw 第三方源码将被自动归类,无需在此手动分组。
sanyi_add_shared_library(${LIB_NAME}
    FOLDER "${LIB_FOLDER}"
    DESCRIPTION "${LIB_DESCRIPTION}"
    EXPORT_MACRO "${LIB_EXPORT_MACRO}"
    VERSION_MAJOR ${VERSION_MAJOR}
    VERSION_MINOR ${VERSION_MINOR}
    VERSION_PATCH ${VERSION_PATCH}
    SOURCES ${LIB_SOURCES}
    HEADERS ${LIB_HEADERS}
    PUBLIC_INCLUDE_DIRS
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
    PRIVATE_INCLUDE_DIRS
        "${SANYI_ENGINE_2D_DIR}/Include"
        "${SANYI_ENGINE_2D_DIR}/Include/Engine2D"
        "${SANYI_ENGINE_3D_DIR}/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/Src/Internal"
    LINK_LIBRARIES
        PUBLIC
            EngineCommon
            protobuf::libprotobuf-lite
            NanoSVG::nanosvg
        PRIVATE
            Engine2D
            Engine3D
            Log
)

# libdxfrw 第三方源码添加到目标(统一入口将其归入 External 源组)
target_sources(${LIB_NAME} PRIVATE ${LIB_DXFRW_SOURCES})

protobuf_generate(
    TARGET ${LIB_NAME}
    APPEND_PATH
    PROTOC_OUT_DIR "${PROTO_GEN_DIR}"
    PROTOS "${PROTO_FILE}"
)

# 暴露 protobuf 生成目录给下游目标（如 MainTests 需要直接编译 .pb.cc）
set_target_properties(${LIB_NAME} PROPERTIES
    INTERFACE_SANYI_PROTO_GEN_DIR "${PROTO_GEN_DIR}"
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

# --- 单元测试 ---
option(BUILD_FILEIO_TESTS "Build FileIO unit tests" ON)

if(BUILD_FILEIO_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(Test)
    message(STATUS "  Tests: ENABLED")
else()
    message(STATUS "  Tests: DISABLED")
endif()

message(STATUS "  C++ Standard: 17")
message(STATUS "  Protobuf: Enabled (lite runtime)")