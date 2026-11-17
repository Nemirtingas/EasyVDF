# The examples, built with exceptions disabled: EasyVDF does not need them.
easyvdf_disable_exceptions()

foreach(name IN ITEMS quick_tour achievements vdf_convert vdf_check)
    easyvdf_add_no_exceptions_executable(easyvdf_example_${name} ${name}.cpp ${name})
endforeach()

# The examples must keep compiling and running: smoke test them.
if(EASYVDF_BUILD_TESTS)
    set(EASYVDF_DATA ${PROJECT_SOURCE_DIR}/tests/data)
    add_test(NAME example_quick_tour COMMAND easyvdf_example_quick_tour)
    add_test(NAME example_achievements COMMAND easyvdf_example_achievements ${EASYVDF_DATA}/stats_schema.vdf french)
    add_test(NAME example_vdf_convert_to_text COMMAND easyvdf_example_vdf_convert ${EASYVDF_DATA}/binary.vdf binary_vdf.txt)
    add_test(NAME example_vdf_convert_to_vbkv COMMAND easyvdf_example_vdf_convert binary_vdf.txt binary_vdf.vbkv vbkv)
    add_test(NAME example_vdf_check COMMAND easyvdf_example_vdf_check ${EASYVDF_DATA}/binary.vdf ${EASYVDF_DATA}/stats_schema.vdf)
    add_test(NAME example_vdf_check_error COMMAND easyvdf_example_vdf_check no_such_file.vdf)
    set_tests_properties(example_vdf_convert_to_vbkv PROPERTIES DEPENDS example_vdf_convert_to_text)
    set_tests_properties(example_quick_tour example_achievements example_vdf_convert_to_text example_vdf_convert_to_vbkv
        example_vdf_check example_vdf_check_error
        PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(example_quick_tour PROPERTIES
        PASS_REGULAR_EXPRESSION "parse error \\[UnexpectedEof\\] at line 3.*depot 482: EasyVDF: key \"482\" not found")
    set_tests_properties(example_achievements PROPERTIES PASS_REGULAR_EXPRESSION "Vainqueur.*ACH_SECRET \\(hidden\\).*NumGames")
    set_tests_properties(example_vdf_check PROPERTIES
        PASS_REGULAR_EXPRESSION "binary.vdf: \"RootObject\", 8 keys.*stats_schema.vdf: \"480\".*as expected, Write: ")
    set_tests_properties(example_vdf_check_error PROPERTIES
        PASS_REGULAR_EXPRESSION "no_such_file.vdf: error \\[Io\\]: EasyVDF: can't open")
endif()
