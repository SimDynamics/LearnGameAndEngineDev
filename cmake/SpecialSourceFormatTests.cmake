include_guard(GLOBAL)

# The product and hosted QA use the same original loader, runner and state
# implementation. Authenticated publications remain isolated test data.
function(lgaed_add_special_source_format_tests source_root)
    find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
    set(_special_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/special-source-format")
    set(_special_files "${_special_dir}/manifest.json")
    foreach(_lesson IN ITEMS crt exit_stream exit_guard)
        foreach(_version IN ITEMS before after)
            list(APPEND _special_files "${_special_dir}/${_lesson}/${_version}.json")
        endforeach()
        foreach(_negative IN ITEMS missing_archive missing_editorial source_token
                removed_output_check changed_stdout changed_exit_code changed_stdin foreign_owner)
            list(APPEND _special_files "${_special_dir}/${_lesson}/negative/${_negative}.json")
        endforeach()
        foreach(_history IN ITEMS format_after_retirement retirement_after_format reformat_after_retirement)
            list(APPEND _special_files "${_special_dir}/${_lesson}/historical/${_history}.json")
        endforeach()
    endforeach()
    foreach(_negative IN ITEMS removed_crlf_variant changed_crlf_variant)
        list(APPEND _special_files "${_special_dir}/crt/negative/${_negative}.json")
    endforeach()
    add_custom_command(OUTPUT ${_special_files}
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/special_source_format_fixture_source.py"
            --output-dir "${_special_dir}"
        DEPENDS
            "${source_root}/scripts/curriculum/special_source_format_fixture_source.py"
            "${source_root}/scripts/curriculum/special_source_format_cases.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
            "${source_root}/scripts/curriculum/code_format_compat.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
        VERBATIM)
    add_custom_target(lgaed_special_source_format_fixtures DEPENDS ${_special_files})
    add_executable(lgaed_special_source_format_tests
        "${source_root}/native/platform/windows/special_source_format_tests.cpp")
    target_link_libraries(lgaed_special_source_format_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_special_source_format_tests lgaed_special_source_format_fixtures lgaed_cpp_tool_host)
    add_test(NAME native.special_source_format
        COMMAND lgaed_special_source_format_tests "${_special_dir}/manifest.json")
    set_tests_properties(native.special_source_format PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    add_test(NAME native.special_source_format_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${source_root}/scripts/curriculum" -p test_special_source_format.py -v)
    set_tests_properties(native.special_source_format_authoring PROPERTIES TIMEOUT 180)
    add_test(NAME native.special_source_format_legacy
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest
            test_crt_output_variant test_output_exit_code_contract -v)
    set_tests_properties(native.special_source_format_legacy PROPERTIES TIMEOUT 180
        WORKING_DIRECTORY "${source_root}/scripts/curriculum")
endfunction()
