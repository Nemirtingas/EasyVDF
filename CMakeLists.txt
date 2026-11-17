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

# EasyVDF does not use exceptions: the examples and one test are built with them
# disabled, to show it. Call it at the top of a directory: MSVC's /EHsc comes from
# CMAKE_CXX_FLAGS, which applies to a whole directory.
macro(easyvdf_disable_exceptions)
    if(MSVC)
        string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        set(EASYVDF_NO_EXCEPTIONS_OPTIONS /EHs-c- /D_HAS_EXCEPTIONS=0 /W4 /utf-8)
    else()
        set(EASYVDF_NO_EXCEPTIONS_OPTIONS -fno-exceptions -Wall -Wextra -Wpedantic)
        if(WIN32)
            # Clang with the Microsoft standard library: disable its exceptions too.
            list(APPEND EASYVDF_NO_EXCEPTIONS_OPTIONS -D_HAS_EXCEPTIONS=0)
        endif()
    endif()
endmacro()

function(easyvdf_add_no_exceptions_executable target source output_name)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE EasyVDF::EasyVDF)
    target_compile_options(${target} PRIVATE ${EASYVDF_NO_EXCEPTIONS_OPTIONS})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${output_name} CXX_EXTENSIONS OFF)
endfunction()

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
endif()

if(EASYVDF_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(EASYVDF_BUILD_TESTS)
    add_subdirectory(tests/no_exceptions)
endif()
