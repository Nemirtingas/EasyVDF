# Targets compiled with exceptions disabled: the no-exceptions test and example.
# They have their own directory because MSVC's /EHsc comes from CMAKE_CXX_FLAGS,
# which can only be changed for a whole directory.
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

# EASYVDF_NO_EXCEPTIONS is not defined here: EasyVDF.h must detect it.
function(easyvdf_add_no_exceptions_executable target source output_name)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE EasyVDF::EasyVDF)
    target_compile_options(${target} PRIVATE ${EASYVDF_NO_EXCEPTIONS_OPTIONS})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${output_name} CXX_EXTENSIONS OFF)
endfunction()

if(EASYVDF_BUILD_EXAMPLES)
    easyvdf_add_no_exceptions_executable(easyvdf_example_no_exceptions ${PROJECT_SOURCE_DIR}/examples/no_exceptions.cpp no_exceptions)
endif()

if(EASYVDF_BUILD_TESTS)
    easyvdf_add_no_exceptions_executable(easyvdf_test_no_exceptions test_no_exceptions.cpp test_no_exceptions)

    set(EASYVDF_DATA ${PROJECT_SOURCE_DIR}/tests/data)
    add_test(NAME no_exceptions COMMAND easyvdf_test_no_exceptions)
    set_tests_properties(no_exceptions PROPERTIES WORKING_DIRECTORY ${EASYVDF_DATA})

    # An unhandled error prints it and calls std::abort(); the test catches SIGABRT
    # and prints a marker, so the output tells which one happened.
    add_test(NAME no_exceptions_value_aborts COMMAND easyvdf_test_no_exceptions value-on-error)
    add_test(NAME no_exceptions_scalar_aborts COMMAND easyvdf_test_no_exceptions scalar-object)
    set_tests_properties(no_exceptions_value_aborts PROPERTIES
        PASS_REGULAR_EXPRESSION "Unhandled EasyVDF error \\(Key\\): EasyVDF: key \"missing\" not found.*SIGABRT handler reached")
    set_tests_properties(no_exceptions_scalar_aborts PROPERTIES
        PASS_REGULAR_EXPRESSION "Unhandled EasyVDF error \\(Type\\): EasyVDF: a Int32 value has no children.*SIGABRT handler reached")

    if(EASYVDF_BUILD_EXAMPLES)
        add_test(NAME example_no_exceptions COMMAND easyvdf_example_no_exceptions ${EASYVDF_DATA}/binary.vdf ${EASYVDF_DATA}/stats_schema.vdf)
        set_tests_properties(example_no_exceptions PROPERTIES
            PASS_REGULAR_EXPRESSION "binary.vdf: \"RootObject\", 8 keys.*stats_schema.vdf: \"480\".*as expected, Write: ")
        add_test(NAME example_no_exceptions_error COMMAND easyvdf_example_no_exceptions no_such_file.vdf)
        set_tests_properties(example_no_exceptions_error PROPERTIES
            PASS_REGULAR_EXPRESSION "no_such_file.vdf: error \\[Io\\]: EasyVDF: can't open")
        set_tests_properties(example_no_exceptions example_no_exceptions_error PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif()
