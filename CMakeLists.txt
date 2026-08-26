cmake_minimum_required(VERSION 4.3)

# ============================================================================
# FileIO —— 独立构建入口
# ============================================================================
# 集成进 CAD 主工程时走的是仓库根 CMakeLists.txt 的 add_subdirectory(FileIO/FileIO)，
# 本文件只服务于「把 FileIO 单独拉出来开发 / 跑测试」这一场景。
#
# 三条设计约束同时成立：
#
#   1) 复用仓库统一配置，不自己手搓一套，避免与主工程漂移：
#        ../Config.cmake               —— vcpkg / Qt 路径、MSVC 运行库、模块开关
#        ../CMake/StandaloneInit.cmake —— 工具链、C++17、输出目录、编译选项、GTest 查找
#      与 Utility / Log / Network / GeoModelCore / Nesting / UI 的独立入口保持同一套写法。
#
#   2) 可脱离主程序单独编译：需要的兄弟 DLL 由本工程按需 add_subdirectory 拉进来构建，
#      Main / UI / Renderx 一概不参与。
#
#   3) 兄弟 DLL 的测试目标默认关闭：独立构建 FileIO 时只关心 FileIO 自己的测试，
#      不该顺带编出 Engine2D / Engine3D 的整套测试。
#
# 当前依赖闭包（自下而上）：
#     Utility / Log ──> EngineCommon ──> Engine2D ──> Engine3D ──> FileIO
#   其中 Engine2D / Engine3D 只被 FileIO 的 Writers 用来 dynamic_cast 具体图元子类，
#   属于导出侧的历史依赖；导入侧已走中立 IR（FioTypes.h）。等导出侧也切到 IR 之后，
#   这两个依赖可以整体摘掉，届时依赖闭包会缩小到 Utility + Log。
#
# 用法：
#   cmake -S FileIO -B FileIO/build
#   cmake --build FileIO/build --config Release
#   ctest --test-dir FileIO/build -C Release
#   cmake -S FileIO -B FileIO/build -DBUILD_FILEIO_TESTS=OFF   # 只编库、不编测试
# ============================================================================

include(../Config.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../CMake/StandaloneInit.cmake)

project(SanYiFileIO
    VERSION 1.0.0
    DESCRIPTION "SanYi CAD File I/O Library (Import / Export / Save)"
    LANGUAGES CXX
)

# MSVC 的 /W4 等选项必须在 project() 之后才能正确判定编译器
sanyi_setup_compiler_options()

option(BUILD_FILEIO_TESTS "Build FileIO unit tests" ON)

# --- 兄弟 DLL ---------------------------------------------------------------
# 独立构建时这些 target 都不存在，需要按依赖顺序逐个拉进本次构建。
# 集成进主工程时它们已由根 CMakeLists.txt 创建，NOT TARGET 守卫会整体跳过。
#
# 注意顺序：被依赖者必须先 add_subdirectory，否则 target_link_libraries 拿不到
# INTERFACE 包含目录，会退化成「把 target 名当裸库名传给链接器」——configure 阶段
# 不报错，编译阶段才以 "cannot open include file" 的形式暴露。
if(NOT TARGET FileIO)
    # 兄弟模块的测试目标在独立构建 FileIO 时一律关闭，避免连带编译大量无关用例。
    # 用 CACHE ... FORCE 是因为这些 option() 定义在各自子目录里，普通 set 会被覆盖。
    set(BUILD_UTILITY_TESTS  OFF CACHE BOOL "" FORCE)
    set(BUILD_LOG_TESTS      OFF CACHE BOOL "" FORCE)
    set(BUILD_LOG_EXAMPLE    OFF CACHE BOOL "" FORCE)
    set(BUILD_ENGINE2D_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_ENGINE3D_TESTS OFF CACHE BOOL "" FORCE)

    # 按需拉入一个兄弟模块：target 已存在则跳过，源码目录缺失则直接失败。
    # FileIO 对这几个模块是硬依赖（编译期就要它们的头），拿不到没有降级空间，
    # 所以这里用 FATAL_ERROR 而不是 WARNING。
    macro(_fileio_add_sibling _tgt _mod_dir)
        if(NOT TARGET ${_tgt})
            if(EXISTS "${_mod_dir}/CMakeLists.txt")
                message(STATUS "[FileIO] Sibling module ${_tgt}: building from ${_mod_dir}")
                add_subdirectory("${_mod_dir}" "${CMAKE_BINARY_DIR}/${_tgt}")
            else()
                message(FATAL_ERROR
                    "[FileIO] Required sibling module ${_tgt} was not found at ${_mod_dir}. "
                    "FileIO cannot be built standalone without it.")
            endif()
        endif()
    endmacro()

    # 严格按依赖顺序：Utility / Log 是叶子，Engine2D 依赖 EngineCommon，Engine3D 依赖 Engine2D。
    _fileio_add_sibling(Utility      "${SANYI_UTILITY_DIR}")
    _fileio_add_sibling(Log          "${SANYI_LOG_DIR}")
    _fileio_add_sibling(EngineCommon "${SANYI_ENGINE_COMMON_DIR}")
    _fileio_add_sibling(Engine2D     "${SANYI_ENGINE_2D_DIR}")
    _fileio_add_sibling(Engine3D     "${SANYI_ENGINE_3D_DIR}")
endif()

if(BUILD_FILEIO_TESTS)
    sanyi_find_gtest()
    # sanyi_find_gtest() 找不到 GTest 时只打印提示、不改开关，而内层 FileIO/CMakeLists.txt
    # 用的是 find_package(GTest REQUIRED)，会让 configure 直接失败——提示说 tests disabled、
    # 实际却编不过。这里补上真正的关闭动作，保证没装 GTest 的环境仍然可以只编库。
    if(NOT TARGET GTest::gtest)
        message(STATUS "[FileIO] GTest not found, disabling FileIO tests (library build is unaffected)")
        set(BUILD_FILEIO_TESTS OFF CACHE BOOL "Build FileIO unit tests" FORCE)
    endif()
endif()

# 测试子目录由 FileIO/CMakeLists.txt 在 BUILD_FILEIO_TESTS 为真时自行 add_subdirectory(Test)，
# 这里不再重复添加 —— 两处添加同一目录会导致 target 重定义。
add_subdirectory(FileIO)

message(STATUS "")
message(STATUS "========================================")
message(STATUS "  [Standalone: ${PROJECT_NAME}]")
message(STATUS "========================================")
message(STATUS "  Version:     ${PROJECT_VERSION}")
message(STATUS "  Build Tests: ${BUILD_FILEIO_TESTS}")
message(STATUS "  Output:      ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "========================================")
message(STATUS "")
