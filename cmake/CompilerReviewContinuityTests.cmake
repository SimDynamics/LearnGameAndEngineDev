include_guard(GLOBAL)

# Exercise authenticated rename/prose history and observed semantic rejections
# through the same Windows loaders, compiler adapter and state store as the app.
function(lgaed_add_compiler_review_continuity_tests source_root)
    find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
    set(_rename_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/identifier-rename-prose")
    set(_rename_files)
    foreach(_stage IN ITEMS manifest published renamed question interleaved explanation returned)
        list(APPEND _rename_files "${_rename_dir}/${_stage}.json")
    endforeach()
    foreach(_negative IN ITEMS missing_snapshot
            wrong_prefix
            wrong_predecessor
            removed_edge
            reordered_edges
            stale_endpoint
            altered_contract
            altered_source
            source_whitespace
            wrong_origin
            stale_semantics
            unrelated_semantics
            forged_return
            second_rename
            intermediate_contract
            borrowed_owner)
        list(APPEND _rename_files "${_rename_dir}/negative/${_negative}.json")
    endforeach()
    add_custom_command(OUTPUT ${_rename_files}
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/identifier_rename_prose_fixture_source.py"
            --output-dir "${_rename_dir}"
        DEPENDS
            "${source_root}/scripts/curriculum/identifier_rename_prose_fixture_source.py"
            "${source_root}/scripts/curriculum/identifier_rename_archive_preview.py"
            "${source_root}/scripts/curriculum/identifier_rename_compat.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
            "${source_root}/scripts/curriculum/code_format_compat.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
            "${source_root}/scripts/curriculum/text_semantics.py"
            "${source_root}/scripts/curriculum/image_resources.py"
            "${source_root}/scripts/curriculum/simulation.py"
            "${source_root}/scripts/curriculum/bibliography_revision.py"
            "${source_root}/scripts/curriculum/course_edition.py"
            "${source_root}/scripts/curriculum/code_scope_presentations.py"
            "${source_root}/scripts/curriculum/math_matching_text_compat.py"
        VERBATIM)
    add_custom_target(lgaed_identifier_rename_prose_fixtures DEPENDS ${_rename_files})
    add_executable(lgaed_identifier_rename_prose_tests
        "${source_root}/native/platform/windows/identifier_rename_prose_tests.cpp")
    target_link_libraries(lgaed_identifier_rename_prose_tests PRIVATE
        lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_identifier_rename_prose_tests
        lgaed_identifier_rename_prose_fixtures)
    add_test(NAME native.identifier_rename_prose
        COMMAND lgaed_identifier_rename_prose_tests "${_rename_dir}/manifest.json")
    add_test(NAME native.identifier_rename_prose_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${source_root}/scripts/curriculum" -p test_identifier_rename_prose_history.py -v)
    set_tests_properties(native.identifier_rename_prose
        native.identifier_rename_prose_authoring PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

    add_executable(lgaed_private_probe_review_tests
        "${source_root}/native/platform/windows/private_probe_review_tests.cpp")
    target_link_libraries(lgaed_private_probe_review_tests PRIVATE
        lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_private_probe_review_tests lgaed_cpp_tool_host)
    add_test(NAME native.polymorphic_clone_private_review
        COMMAND lgaed_private_probe_review_tests --polymorphic-clone)
    add_test(NAME native.dual_interface_private_review
        COMMAND lgaed_private_probe_review_tests --dual-interface)
    set_tests_properties(native.polymorphic_clone_private_review
        native.dual_interface_private_review PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
endfunction()
