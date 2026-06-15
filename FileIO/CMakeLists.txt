cmake_minimum_required(VERSION 3.20)

set(LIB_NAME "FileIO")

# ============================================================
# 源文件收集（先于 protobuf_generate，因为需要先创建 target）
# ============================================================

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
if(ZLIB_FOUND)
    message(STATUS "ZLIB found: SVGZ support enabled")
else()
    message(STATUS "ZLIB not found: SVGZ support disabled (SVG only)")
endif()

# 预先生成的 Protobuf 目录（或由 CMake 生成）
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Generated/Proto")

# 如果预先生成的目录不存在，使用构建目录
if(NOT EXISTS "${PROTO_GEN_DIR}")
    set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated/Proto")
    file(MAKE_DIRECTORY ${PROTO_GEN_DIR})
endif()

# ============================================================
# 创建目标（必须先于 protobuf_generate）
# ============================================================
add_library(${LIB_NAME} SHARED
    ${FILEIO_SOURCES}
    ${FILEIO_HEADERS}
    ${LIB_DXFRW_SOURCES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Header Files" FILES ${FILEIO_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${FILEIO_SOURCES})

# 使用 protobuf_generate 的 TARGET 模式（需要在 add_library 之后调用）
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
        "${PROTO_GEN_DIR}"              # 生成的 protobuf 头文件
        "${Stb_INCLUDE_DIR}"            # stb 头文件目录
        "${NANOSVG_INCLUDE_DIR}"        # nanosvg 头文件目录
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

message(STATUS "[${LIB_NAME}]")
message(STATUS "  Project: ${LIB_NAME}")
message(STATUS "  Version: 1.0.0")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}(${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Output Directory: ${CMAKE_BINARY_DIR}")
message(STATUS "  C++ Standard: 17")
message(STATUS "  Protobuf: Enabled (lite runtime)")
message(STATUS "  ZLIB: $<IF:$<BOOL:${ZLIB_FOUND}>,Enabled,Disabled>")
message(STATUS "")
