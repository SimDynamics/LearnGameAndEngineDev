include_guard(GLOBAL)

# Shared by the production Windows tests and the isolated hosted QA build.
# The input is a complete authenticated publication; generated descendants
# live only in the build's test-fixtures directory.
function(lgaed_add_general_choice_prose_tests source_root)
    if(NOT TARGET lgaed_general_choice_editorial_tests)
        message(FATAL_ERROR "The original general-choice Windows test host is required.")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_choice_prose_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/choice-prose")
    set(_choice_prose_outputs)
    foreach(_fixture manifest.json published.json question.json explanation.json
            rejected-answer-index.json rejected-another-option.json
            rejected-unreviewed-option.json rejected-unowned-guide.json
            rejected-missing-option-edge.json rejected-forged-shortcut.json
            rejected-forged-archive-prose.json rejected-broken-prose-chain.json
            rejected-missing-question-marker.json rejected-stale-explanation-marker.json
            rejected-incompatible-question-spans.json rejected-blank-explanation.json
            rejected-stale-current-head.json)
        list(APPEND _choice_prose_outputs "${_choice_prose_dir}/${_fixture}")
    endforeach()
    add_custom_command(OUTPUT ${_choice_prose_outputs}
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/general_choice_prose_fixture_source.py"
            --output-dir "${_choice_prose_dir}"
        DEPENDS "${source_root}/scripts/curriculum/general_choice_prose_fixture_source.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
            "${source_root}/scripts/curriculum/text_semantics.py"
            "${source_root}/scripts/curriculum/course_edition.py"
            "${source_root}/scripts/curriculum/code_format_compat.py"
        VERBATIM)
    add_custom_target(lgaed_general_choice_prose_fixtures
        DEPENDS ${_choice_prose_outputs})
    add_dependencies(lgaed_general_choice_editorial_tests
        lgaed_general_choice_prose_fixtures)
    add_test(NAME native.general_choice_prose_history
        COMMAND lgaed_general_choice_editorial_tests --prose-history "${_choice_prose_dir}")
    set_tests_properties(native.general_choice_prose_history
        PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.general_choice_prose_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${source_root}/scripts/curriculum" -p test_general_choice_prose_history.py -v)
    set_tests_properties(native.general_choice_prose_authoring PROPERTIES TIMEOUT 45)

    # U2L9 independently revised existing presentation prose and its markers
    # before an option review. Preserve both complete published predecessors.
    set(_choice_composition_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/choice-semantic-composition")
    set(_choice_composition_outputs)
    foreach(_fixture manifest.json before.json published.json reviewed.json
            rejected-answer-index.json rejected-another-option.json
            rejected-unarchived-replacement.json rejected-wrong-archive-owner.json
            rejected-old-option-source.json rejected-new-option-source.json
            rejected-option-range.json rejected-paragraph-source.json
            rejected-missing-option-edge.json rejected-paragraph-chain.json)
        list(APPEND _choice_composition_outputs "${_choice_composition_dir}/${_fixture}")
    endforeach()
    add_custom_command(OUTPUT ${_choice_composition_outputs}
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/general_choice_semantic_composition_fixture_source.py"
            --output-dir "${_choice_composition_dir}"
        DEPENDS "${source_root}/scripts/curriculum/general_choice_semantic_composition_fixture_source.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
            "${source_root}/scripts/curriculum/text_semantics.py"
            "${source_root}/scripts/curriculum/course_edition.py"
            "${source_root}/scripts/curriculum/code_format_compat.py"
        VERBATIM)
    add_custom_target(lgaed_general_choice_semantic_composition_fixtures
        DEPENDS ${_choice_composition_outputs})
    add_dependencies(lgaed_general_choice_editorial_tests
        lgaed_general_choice_semantic_composition_fixtures)
    add_test(NAME native.general_choice_semantic_composition
        COMMAND lgaed_general_choice_editorial_tests --semantic-composition "${_choice_composition_dir}")
    set_tests_properties(native.general_choice_semantic_composition
        PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.general_choice_semantic_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${source_root}/scripts/curriculum" -p test_general_choice_semantic_composition.py -v)
    set_tests_properties(native.general_choice_semantic_authoring PROPERTIES TIMEOUT 60)
endfunction()
