# FileIO模块单元测试配置

find_package(GTest REQUIRED)

include(GoogleTest)

file(GLOB_RECURSE FILEIO_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_executable(FileIOTests ${FILEIO_TEST_SOURCES}
    # 内部实现文件：IrProjector 是 DLL 内部的 IR 投影层，不带 FILEIO_API 导出宏
    # （给内含 std::vector 的内部类加 dllexport 会把布局纳入 ABI 约束，得不偿失）。
    # 因此直接把源文件编进测试可执行文件来测，而不是为了测试去导出它。
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/Internal/IrProjector.cpp"
)

target_compile_features(FileIOTests PRIVATE cxx_std_17)

target_include_directories(FileIOTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Include/FileIO"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/Internal"
)

target_link_libraries(FileIOTests PRIVATE
    FileIO
    Log
    GTest::gtest
    GTest::gtest_main
)

# --- 测试运行期的 DLL 依赖 ---
# FileIO 依赖一批 vcpkg 第三方 DLL（protobuf / tiff / webp / freetype 等），
# Engine2D 还间接需要 Qt6Core.dll。Config.cmake 里的 set(ENV{PATH} ...) 只在 configure
# 期生效，ctest 运行期不继承，结果是测试可执行文件能编出来但一启动就 0xc0000135
# (STATUS_DLL_NOT_FOUND)。
#
# 这里不去改 PATH，而是把依赖 DLL 直接拷到可执行文件旁边——Windows 的 DLL 搜索顺序里
# 可执行文件所在目录优先级最高，这样无论从哪个 shell、用什么环境变量跑 ctest 都能起来。
# $<TARGET_RUNTIME_DLLS:...> 会自动展开该目标传递依赖到的全部 imported SHARED 库，
# 不需要手写第三方库清单，新增依赖时也不用同步维护。
#
# 用 -t <目标目录> 的形式而不是把目录放末尾：全静态配置下（如 BUILD_SHARED_LIBS=OFF）
# 该表达式会展开成空列表，若目录在末尾就只剩一个参数、copy_if_different 直接报错；
# 目标目录在前时空列表天然安全。
if(WIN32)
    add_custom_command(TARGET FileIOTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                -t "$<TARGET_FILE_DIR:FileIOTests>" "$<TARGET_RUNTIME_DLLS:FileIOTests>"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLL dependencies next to FileIOTests"
    )
endif()

gtest_discover_tests(FileIOTests)

set_property(TARGET FileIOTests PROPERTY FOLDER "Tests")

if(MSVC)
    target_compile_options(FileIOTests PRIVATE /utf-8)
endif()
