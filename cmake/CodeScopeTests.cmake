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
    set(_course_dir "${_fixture_dir}/content/es/programming/cpp_fundamentals")
    set(_unit_dir "${_course_dir}/edition_es_1__expresiones_y_decisiones")
    set(_baseline "${_unit_dir}/lesson_alternativas_y_fronteras.json")
    set(_course_index "${_course_dir}/course_cpp_fundamentals.json")
    set(_unit_index "${_unit_dir}/unit_edition_es_1__expresiones_y_decisiones.json")
    set(_manifest "${_fixture_dir}/manifest.json")
    add_custom_command(OUTPUT "${_baseline}" "${_course_index}" "${_unit_index}" "${_manifest}"
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/code_scope_fixture_source.py"
            --output-dir "${_fixture_dir}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/code_scope_fixture_source.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Authenticating the complete pre-overlay code-scope lesson from Git"
        VERBATIM)
    add_custom_target(lgaed_code_scope_fixture DEPENDS "${_baseline}" "${_course_index}" "${_unit_index}" "${_manifest}")
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
