cmake_minimum_required(VERSION 4.3)

# ============================================================================
# FileIO —— 独立构建入口
# ============================================================================
# 集成构建：根 CMakeLists.txt → add_subdirectory(FileIO/FileIO)（内层）。
# 独立构建：本文件提供 project() 与环境配置，把 FileIO 单独拉出来开发 / 跑测试。
#
# 三条设计约束同时成立：
#
#   1) 复用仓库统一配置，不自己手搓一套，避免与主工程漂移：
#        ../Config.cmake               —— vcpkg / Qt 路径、MSVC 运行库、模块开关
#        ../CMake/StandaloneInit.cmake —— SANYI_ROOT、编译选项、GTest 查找、兄弟模块拉入
#      与 Utility / Log / License / Nesting 的独立入口保持同一套写法。
#
#   2) 可脱离主程序单独编译：需要的兄弟 DLL 由 sanyi_add_sibling() 按依赖顺序
#      逐个拉进来构建，Main / UI / Renderx 一概不参与。
#
#   3) 兄弟 DLL 的测试目标在独立构建时关闭（sanyi_only_own_tests）：只关心
#      FileIO 自己的测试，不顺带编出 Engine2D / Engine3D 的整套测试。
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
#   cmake -S FileIO -B FileIO/build -DBUILD_FILEIO_EXAMPLE=OFF # 不编示例
# ============================================================================

include(${CMAKE_CURRENT_LIST_DIR}/../Config.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../CMake/StandaloneInit.cmake)

project(SanYiFileIO
    VERSION 1.0.0
    DESCRIPTION "SanYi CAD File I/O Library (Import / Export / Save)"
    LANGUAGES CXX
)

# MSVC 的 /W4 等选项必须在 project() 之后才能正确判定编译器
sanyi_setup_compiler_options()

# 示例目标仅存在于独立构建（内层不负责 Example）
option(BUILD_FILEIO_EXAMPLE "Build FileIO usage example" ON)

# --- 测试与兄弟模块 ---------------------------------------------------------
# 先关兄弟测试、再探测 GTest（缺失时把测试开关统一降级为 OFF），
# 两步都必须发生在拉入兄弟模块之前，否则兄弟内层的 find_package(GTest REQUIRED)
# 会在降级前硬失败。
sanyi_only_own_tests(BUILD_FILEIO_TESTS)
if(BUILD_FILEIO_TESTS)
    sanyi_find_gtest()
endif()

# 严格按依赖顺序：Utility / Log 是叶子，Engine2D 依赖 EngineCommon，Engine3D 依赖 Engine2D。
# 硬依赖，缺了没有降级空间 —— sanyi_add_sibling 默认 FATAL_ERROR。
sanyi_add_sibling(Utility      "${SANYI_UTILITY_DIR}")
sanyi_add_sibling(Log          "${SANYI_LOG_DIR}")
sanyi_add_sibling(EngineCommon "${SANYI_ENGINE_COMMON_DIR}")
sanyi_add_sibling(Engine2D     "${SANYI_ENGINE_2D_DIR}")
sanyi_add_sibling(Engine3D     "${SANYI_ENGINE_3D_DIR}")

# 内层：创建 FileIO DLL、挂载测试（BUILD_FILEIO_TESTS 时）、输出状态。
# 测试子目录只由内层挂载，这里不再重复添加 —— 两处添加同一目录会导致 target 重定义。
add_subdirectory(FileIO)

# --- 示例（独立构建专属） ---------------------------------------------------
if(BUILD_FILEIO_EXAMPLE)
    add_subdirectory(Example)
endif()

# --- 独立构建摘要 -----------------------------------------------------------
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  [Standalone: ${PROJECT_NAME}]")
message(STATUS "========================================")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build Tests:    ${BUILD_FILEIO_TESTS}")
message(STATUS "  Build Example:  ${BUILD_FILEIO_EXAMPLE}")
message(STATUS "  Output:         ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "========================================")
message(STATUS "")
