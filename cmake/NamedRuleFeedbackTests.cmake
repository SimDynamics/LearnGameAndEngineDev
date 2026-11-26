# Preserve the complete original named-rule publication and authenticate the
# actual current asset before exercising both through the original Win32 UI.
function(lgaed_add_named_rule_feedback_tests root)
    if(TARGET lgaed_named_rule_feedback_fixtures)
        return()
    endif()
    set(_scripts "${root}/scripts/curriculum")
    set(_fixtures "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/named-rule-feedback")
    set(_outputs "${_fixtures}/original.json" "${_fixtures}/baseline.json" "${_fixtures}/current.json"
        "${_fixtures}/manifest.json")
    add_custom_command(OUTPUT ${_outputs}
        COMMAND "${Python3_EXECUTABLE}" -B "${_scripts}/named_rule_feedback_fixture_source.py"
            --output-dir "${_fixtures}"
        DEPENDS "${_scripts}/named_rule_feedback_fixture_source.py"
            "${_scripts}/format_batch_history.py" "${_scripts}/windows_cpp_ci.py"
            "${_scripts}/published_adoption_fixtures.py" "${_scripts}/published_adoption_pins.json"
            "${_scripts}/stable_state_layout.py" "${_scripts}/code_format_compat.py"
            "${_scripts}/text_semantics.py" "${_scripts}/bibliography_revision.py"
            "${root}/app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_nombrar_reglas_y_separar_acuerdos.json"
        WORKING_DIRECTORY "${root}" VERBATIM)
    add_custom_target(lgaed_named_rule_feedback_fixtures DEPENDS ${_outputs})
    add_dependencies(lgaed_curricular_input_lab_tests lgaed_named_rule_feedback_fixtures)
    add_test(NAME native.named_rule_feedback_view
        COMMAND lgaed_curricular_input_lab_tests --named-rule-feedback "${_fixtures}")
    set_tests_properties(native.named_rule_feedback_view PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    add_test(NAME native.named_rule_feedback_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest -v test_named_rule_feedback_history)
    set_tests_properties(native.named_rule_feedback_authoring PROPERTIES
        TIMEOUT 120 WORKING_DIRECTORY "${_scripts}")
endfunction()
