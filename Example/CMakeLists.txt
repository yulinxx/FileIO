add_executable(AprFileIOExample
    main.cpp
)

target_compile_features(AprFileIOExample PRIVATE cxx_std_17)

target_link_libraries(AprFileIOExample PRIVATE
    FileIO
)

if(MSVC)
    target_compile_options(AprFileIOExample PRIVATE /utf-8)
endif()

set_target_properties(AprFileIOExample PROPERTIES FOLDER "Examples")

message(STATUS "  Example: AprFileIOExample")
