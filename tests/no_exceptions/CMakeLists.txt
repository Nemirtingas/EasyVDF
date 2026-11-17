# A test built with exceptions disabled, without Catch2 (which needs them).
easyvdf_disable_exceptions()

easyvdf_add_no_exceptions_executable(easyvdf_test_no_exceptions test_no_exceptions.cpp test_no_exceptions)

add_test(NAME no_exceptions COMMAND easyvdf_test_no_exceptions)
set_tests_properties(no_exceptions PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/tests/data)

# The mistakes that have no error value print the error and call std::abort(); the
# test catches SIGABRT and prints a marker, so the output tells which one happened.
add_test(NAME no_exceptions_value_aborts COMMAND easyvdf_test_no_exceptions value-on-error)
add_test(NAME no_exceptions_scalar_aborts COMMAND easyvdf_test_no_exceptions scalar-object)
add_test(NAME no_exceptions_error_aborts COMMAND easyvdf_test_no_exceptions error-of-value)
set_tests_properties(no_exceptions_value_aborts PROPERTIES
    PASS_REGULAR_EXPRESSION "Unhandled EasyVDF error \\(Key\\): EasyVDF: key \"missing\" not found.*SIGABRT handler reached")
set_tests_properties(no_exceptions_scalar_aborts PROPERTIES
    PASS_REGULAR_EXPRESSION "Unhandled EasyVDF error \\(Type\\): EasyVDF: a Int32 value has no children.*SIGABRT handler reached")
set_tests_properties(no_exceptions_error_aborts PROPERTIES
    PASS_REGULAR_EXPRESSION "EasyVDF: error\\(\\) called on a Result that holds a value.*SIGABRT handler reached")
