add_executable(SyFileIOExample
    main.cpp
)

target_compile_features(SyFileIOExample PRIVATE cxx_std_17)

target_link_libraries(SyFileIOExample PRIVATE
    FileIO
)

if(MSVC)
    target_compile_options(SyFileIOExample PRIVATE /utf-8)
endif()

set_target_properties(SyFileIOExample PROPERTIES FOLDER "Examples")

message(STATUS "  Example: SyFileIOExample")
