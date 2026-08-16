# FileIO模块单元测试配置

find_package(GTest REQUIRED)

include(GoogleTest)

file(GLOB_RECURSE FILEIO_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_executable(FileIOTests ${FILEIO_TEST_SOURCES})

target_compile_features(FileIOTests PRIVATE cxx_std_17)

target_include_directories(FileIOTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/Internal"
)

target_link_libraries(FileIOTests PRIVATE
    FileIO
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(FileIOTests)

set_property(TARGET FileIOTests PROPERTY FOLDER "Tests")

if(MSVC)
    target_compile_options(FileIOTests PRIVATE /utf-8)
endif()
