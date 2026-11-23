# Exercise the three build-time fixture generators against complete publications
# before and after the later format reviews. The product MSVC pin is unchanged.
function(lgaed_add_format_fixture_history_tests root)
    if(TARGET lgaed_format_fixture_history_fixtures)
        return()
    endif()
    set(_scripts "${root}/scripts/curriculum")
    set(_fixtures "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/format-history")
    set(_general "${_fixtures}/general")
    set(_general_outputs)
    foreach(_locale es en fr)
        foreach(_side before after)
            list(APPEND _general_outputs "${_general}/${_locale}.${_side}.json")
        endforeach()
    endforeach()
    list(APPEND _general_outputs "${_general}/es.changed-answer.json"
        "${_general}/es.forged-archive.json")
    foreach(_prefix u7-question u7-question-history)
        foreach(_side before after changed-answer stale-question unmarked-question forged-source)
            list(APPEND _general_outputs "${_general}/${_prefix}.${_side}.json")
        endforeach()
    endforeach()
    foreach(_prefix u12-formatting u12-group)
        foreach(_side before after changed-answer stale-semantic forged-proof)
            list(APPEND _general_outputs "${_general}/${_prefix}.${_side}.json")
        endforeach()
    endforeach()
    set(_shared_dependencies
        "${_scripts}/stable_state_layout.py" "${_scripts}/code_format_compat.py"
        "${_scripts}/format_batch_history.py" "${_scripts}/windows_cpp_ci.py"
        "${_scripts}/text_semantics.py" "${_scripts}/bibliography_revision.py")
    add_custom_command(OUTPUT ${_general_outputs}
        COMMAND "${Python3_EXECUTABLE}" -B "${_scripts}/code_format_fixture_source.py"
            --output-dir "${_general}"
        DEPENDS ${_shared_dependencies} "${_scripts}/code_format_fixture_source.py"
            "${_scripts}/test_code_format_compat.py"
            "${_scripts}/test_published_u12_question_editorials.py"
            "${_scripts}/editorial_fixture_history.py"
        WORKING_DIRECTORY "${root}" VERBATIM)
    set(_all_outputs ${_general_outputs})
    foreach(_publication 921e1df 83e2d4e6)
        if(_publication STREQUAL "921e1df")
            set(_scenario original)
        else()
            set(_scenario coexisting)
        endif()
        set(_pilot "${_fixtures}/${_publication}/pilot")
        set(_diagnostic "${_fixtures}/${_publication}/diagnostic")
        set(_pilot_outputs)
        foreach(_file real.before.json real.after.json
                after-editorial/real.before.json after-editorial/real.after.json)
            list(APPEND _pilot_outputs "${_pilot}/${_file}")
        endforeach()
        add_custom_command(OUTPUT ${_pilot_outputs}
            COMMAND "${Python3_EXECUTABLE}" -B "${_scripts}/test_real_code_format_pilot.py"
                --output-dir "${_pilot}" --publication "${_publication}"
            DEPENDS ${_shared_dependencies} "${_scripts}/test_real_code_format_pilot.py"
            WORKING_DIRECTORY "${root}" VERBATIM)
        set(_diagnostic_outputs "${_diagnostic}/all-diagnostics.manifest.json")
        foreach(_prefix u6 u6more u9)
            foreach(_side before after forged-goal forged-archive forged-source)
                list(APPEND _diagnostic_outputs "${_diagnostic}/${_prefix}.${_side}.json")
            endforeach()
        endforeach()
        foreach(_index RANGE 0 5)
            foreach(_side before after)
                list(APPEND _diagnostic_outputs "${_diagnostic}/all-${_index}.${_side}.json")
            endforeach()
        endforeach()
        add_custom_command(OUTPUT ${_diagnostic_outputs}
            COMMAND "${Python3_EXECUTABLE}" -B "${_scripts}/compile_check_source_format_fixture.py"
                --output-dir "${_diagnostic}" --publication "${_publication}"
            DEPENDS ${_shared_dependencies} "${_scripts}/compile_check_source_format_fixture.py"
                "${_scripts}/test_compile_check_source_format.py"
                "${_scripts}/test_reviewed_diagnostic_structural_format.py"
                "${_scripts}/validar_json_programacion.py"
            WORKING_DIRECTORY "${root}" VERBATIM)
        list(APPEND _all_outputs ${_pilot_outputs} ${_diagnostic_outputs})
        add_test(NAME native.format_fixture_history_${_scenario}
            COMMAND lgaed_code_format_loader_tests --build-format-fixtures
                "${_general}" "${_pilot}" "${_diagnostic}")
        set_tests_properties(native.format_fixture_history_${_scenario}
            PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
        add_test(NAME native.format_fixture_diagnostics_${_scenario}
            COMMAND lgaed_format_fixture_diagnostics_msvc_tests "${_diagnostic}")
        set_tests_properties(native.format_fixture_diagnostics_${_scenario}
            PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    endforeach()
    add_custom_target(lgaed_format_fixture_history_fixtures DEPENDS ${_all_outputs})
    add_dependencies(lgaed_code_format_loader_tests lgaed_format_fixture_history_fixtures)
    add_executable(lgaed_format_fixture_diagnostics_msvc_tests
        "${root}/native/platform/windows/u6_compilecheck_format_runner_tests.cpp")
    target_link_libraries(lgaed_format_fixture_diagnostics_msvc_tests PRIVATE lgaed_platform_windows)
    add_dependencies(lgaed_format_fixture_diagnostics_msvc_tests lgaed_format_fixture_history_fixtures)
    add_test(NAME native.format_fixture_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest -v
            test_compile_check_source_format test_real_code_format_pilot
            test_code_format_compat.SourceFormatTest.test_real_u7_combined_format_fixtures_keep_authentic_history)
    set_tests_properties(native.format_fixture_authoring PROPERTIES
        TIMEOUT 300 RUN_SERIAL TRUE WORKING_DIRECTORY "${_scripts}")
endfunction()
