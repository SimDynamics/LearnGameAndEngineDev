include_guard(GLOBAL)

function(lgaed_add_code_scope_tests)
    if(NOT TARGET lgaed_curricular_windows OR NOT TARGET lgaed_narration_runtime)
        message(FATAL_ERROR "Code-scope QA requires the original curricular controls and real mathematical/narration runtime.")
    endif()
    # This focal profile retains the original reader translation unit and its
    # normal /bigobj requirement without configuring the unrelated reader hosts.
    add_executable(lgaed_lesson_reader_tests
        "${CMAKE_SOURCE_DIR}/native/presentation/windows/lesson_reader_tests.cpp")
    target_link_libraries(lgaed_lesson_reader_tests PRIVATE lgaed_curricular_windows)
    target_compile_options(lgaed_lesson_reader_tests PRIVATE /bigobj)
    add_dependencies(lgaed_lesson_reader_tests lgaed_narration_runtime lgaed_cpp_tool_host)

    set(_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/code-scope")
    set(_baseline "${_fixture_dir}/lesson_alternativas_y_fronteras.json")
    set(_manifest "${_fixture_dir}/manifest.json")
    add_custom_command(OUTPUT "${_baseline}" "${_manifest}"
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/code_scope_fixture_source.py"
            --output-dir "${_fixture_dir}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/code_scope_fixture_source.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Authenticating the complete pre-overlay code-scope lesson from Git"
        VERBATIM)
    add_custom_target(lgaed_code_scope_fixture DEPENDS "${_baseline}" "${_manifest}")
    add_dependencies(lgaed_lesson_reader_tests lgaed_code_scope_fixture)

    add_test(NAME native.code_scope_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest -v
            test_code_scope_presentations.CodeScopePresentationTest)
    add_test(NAME native.retirement_publication_chain_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest -v
            test_retired_state_layout.RetirementPublicationChainTest)
    set_tests_properties(native.code_scope_authoring native.retirement_publication_chain_authoring
        PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/scripts/curriculum" TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.code_scope_reader
        COMMAND lgaed_lesson_reader_tests --code-scope-reader "${_baseline}"
            "${CMAKE_CURRENT_BINARY_DIR}/code-scope-captures")
    set_tests_properties(native.code_scope_reader PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
endfunction()
