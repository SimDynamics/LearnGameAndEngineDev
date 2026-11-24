include_guard(GLOBAL)

# Exercise reader close/reopen and live-file reload against authenticated edits.
# Windows controls, persistence and MediaPlayer are real; the protocol fixture
# supplies controlled PCM solely to verify sequence, never neural voice quality.
function(lgaed_add_reader_revision_tests source_root)
    if(NOT TARGET lgaed_curricular_windows)
        message(FATAL_ERROR "The original Windows curricular reader is required.")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_reader_revision_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/reader-revision")
    set(_reader_revision_outputs)
    foreach(_fixture manifest.json baseline.json insert.json retire-insert.json reorder.json prose.json other-page.json)
        list(APPEND _reader_revision_outputs "${_reader_revision_dir}/${_fixture}")
    endforeach()
    add_custom_command(OUTPUT ${_reader_revision_outputs}
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/reader_revision_fixture_source.py"
            --output-dir "${_reader_revision_dir}"
        DEPENDS "${source_root}/scripts/curriculum/reader_revision_fixture_source.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
            "${source_root}/scripts/curriculum/text_semantics.py"
            "${source_root}/scripts/curriculum/course_edition.py"
            "${source_root}/scripts/curriculum/code_format_compat.py"
        VERBATIM)
    add_custom_target(lgaed_reader_revision_fixtures DEPENDS ${_reader_revision_outputs})
    add_executable(lgaed_lesson_reader_edit_order_tests
        "${source_root}/native/presentation/windows/lesson_reader_edit_order_tests.cpp")
    target_link_libraries(lgaed_lesson_reader_edit_order_tests PRIVATE lgaed_curricular_windows)
    add_dependencies(lgaed_lesson_reader_edit_order_tests lgaed_reader_revision_fixtures)
    set(_reader_revision_scenarios insert insert-forward retire-insert reorder prose other-page unheard-negative live-file)
    if(ARGN)
        set(_reader_revision_scenarios ${ARGN})
    endif()
    foreach(_scenario IN LISTS _reader_revision_scenarios)
        string(REPLACE "-" "_" _test_suffix "${_scenario}")
        add_test(NAME "native.reader_revision_${_test_suffix}"
            COMMAND lgaed_lesson_reader_edit_order_tests
                "${_reader_revision_dir}/manifest.json" "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_BINARY_DIR}" "${_scenario}")
        set_tests_properties("native.reader_revision_${_test_suffix}"
            PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    endforeach()
    if("live-file" IN_LIST _reader_revision_scenarios)
        set_tests_properties(native.reader_revision_live_file PROPERTIES TIMEOUT 180)
    endif()
endfunction()
