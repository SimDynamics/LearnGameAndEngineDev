include_guard(GLOBAL)

# Preserve the authentic malformed example while exercising the same full and
# progress loaders and state codec used by the Windows application.
function(lgaed_add_unterminated_source_format_tests source_root)
    find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
    set(_literal_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/unterminated-source-format")
    set(_literal_files "${_literal_dir}/manifest.json"
        "${_literal_dir}/before.json" "${_literal_dir}/after.json")
    foreach(_negative IN ITEMS tail_space tail_newline repaired_quote prefix_token
            line_splice removed_archive altered_archive borrowed_owner)
        list(APPEND _literal_files "${_literal_dir}/negative/${_negative}.json")
    endforeach()
    add_custom_command(OUTPUT ${_literal_files}
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/unterminated_source_format_fixture_source.py"
            --output-dir "${_literal_dir}"
        DEPENDS
            "${source_root}/scripts/curriculum/unterminated_source_format_fixture_source.py"
            "${source_root}/scripts/curriculum/code_format_compat.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
            "${source_root}/scripts/curriculum/identifier_rename_compat.py"
            "${source_root}/scripts/curriculum/text_semantics.py"
            "${source_root}/scripts/curriculum/image_resources.py"
            "${source_root}/scripts/curriculum/simulation.py"
            "${source_root}/scripts/curriculum/bibliography_revision.py"
            "${source_root}/scripts/curriculum/course_edition.py"
            "${source_root}/scripts/curriculum/code_scope_presentations.py"
            "${source_root}/scripts/curriculum/math_matching_text_compat.py"
        VERBATIM)
    add_custom_target(lgaed_unterminated_source_format_fixtures DEPENDS ${_literal_files})
    add_executable(lgaed_unterminated_source_format_tests
        "${source_root}/native/platform/windows/unterminated_source_format_tests.cpp")
    target_link_libraries(lgaed_unterminated_source_format_tests PRIVATE
        lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_unterminated_source_format_tests
        lgaed_unterminated_source_format_fixtures lgaed_cpp_tool_host)
    add_test(NAME native.unterminated_source_format
        COMMAND lgaed_unterminated_source_format_tests "${_literal_dir}/manifest.json")
    set_tests_properties(native.unterminated_source_format
        PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.unterminated_source_format_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${source_root}/scripts/curriculum" -p test_unterminated_source_format.py -v)
    set_tests_properties(native.unterminated_source_format_authoring PROPERTIES TIMEOUT 120)
endfunction()
