cmake_minimum_required(VERSION 3.15)

project(EasyVDF LANGUAGES CXX)

# Header-only library: link against EasyVDF::EasyVDF to get the include path and C++17.
add_library(EasyVDF INTERFACE)
add_library(EasyVDF::EasyVDF ALIAS EasyVDF)
target_include_directories(EasyVDF INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_compile_features(EasyVDF INTERFACE cxx_std_17)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(EASYVDF_IS_TOP_LEVEL ON)
else()
    set(EASYVDF_IS_TOP_LEVEL OFF)
endif()

option(EASYVDF_BUILD_TESTS "Build the EasyVDF tests" ${EASYVDF_IS_TOP_LEVEL})
option(EASYVDF_BUILD_EXAMPLES "Build the EasyVDF examples" ${EASYVDF_IS_TOP_LEVEL})

if(EASYVDF_BUILD_EXAMPLES)
    foreach(name IN ITEMS quick_tour achievements vdf_convert)
        add_executable(easyvdf_example_${name} examples/${name}.cpp)
        target_link_libraries(easyvdf_example_${name} PRIVATE EasyVDF::EasyVDF)
        set_target_properties(easyvdf_example_${name} PROPERTIES OUTPUT_NAME ${name} CXX_EXTENSIONS OFF)
        if(MSVC)
            target_compile_options(easyvdf_example_${name} PRIVATE /W4 /utf-8)
        else()
            target_compile_options(easyvdf_example_${name} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endforeach()
endif()

if(EASYVDF_BUILD_TESTS)
    enable_testing()

    set(CMAKE_CXX_EXTENSIONS OFF)

    include(CheckCXXCompilerFlag)
    if(NOT MSVC)
        # Checked with the positive form: unknown -Wno-* options are silently accepted by GCC.
        check_cxx_compiler_flag(-Wc2y-extensions EASYVDF_HAS_WNO_C2Y_EXTENSIONS)
    endif()

    # Catch2 main, compiled once and shared by every test executable.
    add_library(easyvdf_catch_main STATIC tests/catch_main.cpp)
    target_compile_features(easyvdf_catch_main PUBLIC cxx_std_17)

    # One executable and one CTest test per tests/test_<name>.cpp file.
    set(EASYVDF_TESTS
        files
        text_parser
        binary_parser
        object_model
        api
        readme
        result
    )

    foreach(name IN LISTS EASYVDF_TESTS)
        add_executable(easyvdf_test_${name} tests/test_${name}.cpp)
        target_link_libraries(easyvdf_test_${name} PRIVATE EasyVDF::EasyVDF easyvdf_catch_main)
        if(MSVC)
            target_compile_options(easyvdf_test_${name} PRIVATE /W4 /utf-8)
        else()
            target_compile_options(easyvdf_test_${name} PRIVATE -Wall -Wextra -Wpedantic)
            if(EASYVDF_HAS_WNO_C2Y_EXTENSIONS)
                # Catch2 v2 uses __COUNTER__, which recent clang versions report with -Wpedantic.
                target_compile_options(easyvdf_test_${name} PRIVATE -Wno-c2y-extensions)
            endif()
        endif()
        add_test(NAME ${name} COMMAND easyvdf_test_${name})
        # Tests open their data files (*.vdf) by relative path.
        set_tests_properties(${name} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/data
        )
    endforeach()

    # The examples must keep compiling and running: smoke test them.
    if(EASYVDF_BUILD_EXAMPLES)
        set(EASYVDF_DATA ${CMAKE_CURRENT_SOURCE_DIR}/tests/data)
        add_test(NAME example_quick_tour COMMAND easyvdf_example_quick_tour)
        add_test(NAME example_achievements COMMAND easyvdf_example_achievements ${EASYVDF_DATA}/stats_schema.vdf french)
        add_test(NAME example_vdf_convert_to_text COMMAND easyvdf_example_vdf_convert ${EASYVDF_DATA}/binary.vdf binary_vdf.txt)
        add_test(NAME example_vdf_convert_to_vbkv COMMAND easyvdf_example_vdf_convert binary_vdf.txt binary_vdf.vbkv vbkv)
        set_tests_properties(example_vdf_convert_to_vbkv PROPERTIES DEPENDS example_vdf_convert_to_text)
        set_tests_properties(example_quick_tour example_achievements example_vdf_convert_to_text example_vdf_convert_to_vbkv
            PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(example_achievements PROPERTIES PASS_REGULAR_EXPRESSION "Vainqueur.*ACH_SECRET \\(hidden\\).*NumGames")
    endif()
endif()

# The test and the example built with exceptions disabled.
if(EASYVDF_BUILD_TESTS OR EASYVDF_BUILD_EXAMPLES)
    add_subdirectory(tests/no_exceptions)
endif()
