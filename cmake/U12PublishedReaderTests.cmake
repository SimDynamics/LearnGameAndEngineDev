# The full application build and the focused U12 mode use the same historical
# fixture exporter and the original reader executable, arguments and assertions.
include_guard(GLOBAL)

function(lgaed_add_u12_published_reader_tests)
    set(_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u12-composed-output-published17")
    set(_fixtures
        "${_fixture_dir}/before.json"
        "${_fixture_dir}/after.json"
        "${_fixture_dir}/manifest.json")
    add_custom_command(OUTPUT ${_fixtures}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_composed_output_fixture_source.py"
            --published17-output-dir "${_fixture_dir}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_composed_output_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_composed_feedback_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/text_semantics.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/validar_json_programacion.py"
        COMMENT "Verifying the authentic positional U12L4 publication against its complete seventeen-edge revision"
        VERBATIM)
    add_custom_target(lgaed_u12_composed_output_published17_fixtures DEPENDS ${_fixtures})
    add_dependencies(lgaed_lesson_reader_tests lgaed_u12_composed_output_published17_fixtures)
    foreach(_phase RANGE 0 2)
        add_test(NAME native.u12_composed_output_published17_reader_${_phase}
            COMMAND lgaed_lesson_reader_tests --u12-composed-output-published17
                "${_fixture_dir}/before.json" "${_fixture_dir}/after.json" "${_phase}")
        set_tests_properties(native.u12_composed_output_published17_reader_${_phase}
            PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    endforeach()
endfunction()
