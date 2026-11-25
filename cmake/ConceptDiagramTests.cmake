# Shared registration in focal and complete native reader builds.
function(lgaed_add_concept_diagram_tests)
    set(_diagram_fixture "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/concept-diagram/lesson_qa.json")
    add_custom_command(OUTPUT "${_diagram_fixture}"
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/test_concept_diagram.py"
            --fixture "${_diagram_fixture}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/test_concept_diagram.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/concept_diagram.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/text_semantics.py"
            "${CMAKE_SOURCE_DIR}/docs/examples/concept_diagram_memory_console.json"
        VERBATIM)
    add_custom_target(lgaed_concept_diagram_fixture DEPENDS "${_diagram_fixture}")
    add_dependencies(lgaed_lesson_reader_tests lgaed_concept_diagram_fixture)
    add_test(NAME native.concept_diagram_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest -v test_concept_diagram)
    set_tests_properties(native.concept_diagram_authoring
        PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/scripts/curriculum" TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.concept_diagram_reader
        COMMAND lgaed_lesson_reader_tests --concept-diagram "${_diagram_fixture}"
            "${CMAKE_CURRENT_BINARY_DIR}/code-scope-captures/concept-diagram")
    set_tests_properties(native.concept_diagram_reader PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
endfunction()
