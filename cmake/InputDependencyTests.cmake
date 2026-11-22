# Shared, isolated QA for root-authored input dependencies. Product and hosted
# Windows builds use the same original runner, loader and StateStore sources.
function(lgaed_add_input_dependency_state_tests source_root)
    if(TARGET lgaed_input_dependency_state_tests)
        return()
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_input_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u2l9-input")
    add_custom_command(OUTPUT "${_input_fixture_dir}/before.json"
            "${_input_fixture_dir}/after.json" "${_input_fixture_dir}/manifest.json"
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/input_dependency_fixture_source.py"
            --output-dir "${_input_fixture_dir}"
        DEPENDS "${source_root}/scripts/curriculum/input_dependency_fixture_source.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
        VERBATIM)
    add_custom_target(lgaed_input_dependency_fixtures
        DEPENDS "${_input_fixture_dir}/before.json"
            "${_input_fixture_dir}/after.json" "${_input_fixture_dir}/manifest.json")
    add_executable(lgaed_input_dependency_state_tests
        "${source_root}/native/platform/windows/input_dependency_state_tests.cpp")
    target_link_libraries(lgaed_input_dependency_state_tests
        PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_input_dependency_state_tests lgaed_input_dependency_fixtures)
    add_test(NAME native.input_dependency_state
        COMMAND lgaed_input_dependency_state_tests "${_input_fixture_dir}")
    set_tests_properties(native.input_dependency_state PROPERTIES TIMEOUT 420 RUN_SERIAL TRUE)

    add_executable(lgaed_input_probe_tests
        "${source_root}/native/core/cpp_input_probe_tests.cpp")
    target_include_directories(lgaed_input_probe_tests PRIVATE "${source_root}/native")
    target_link_libraries(lgaed_input_probe_tests PRIVATE lgaed_host_options)
    add_test(NAME native.input_probe COMMAND lgaed_input_probe_tests)
    set_tests_properties(native.input_probe PROPERTIES TIMEOUT 20)
    add_test(NAME native.input_dependency_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${source_root}/scripts/curriculum" -p test_input_dependency_authoring.py -v)
    set_tests_properties(native.input_dependency_authoring PROPERTIES TIMEOUT 60)
endfunction()

# Both current and archived Win32 controls use an authenticated earlier lesson,
# never a current lesson with its mandatory flag artificially disabled.
function(lgaed_add_u2l9_input_view_history_tests source_root)
    if(TARGET lgaed_u2l9_view_history_fixtures)
        return()
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_u2l9_history_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u2l9-view-history")
    set(_u2l9_history_file "${_u2l9_history_dir}/u2l9-archived.json")
    add_custom_command(OUTPUT "${_u2l9_history_file}"
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/u2l9_history_fixture_source.py"
            --output-dir "${_u2l9_history_dir}"
        DEPENDS "${source_root}/scripts/curriculum/u2l9_history_fixture_source.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
        VERBATIM)
    add_custom_target(lgaed_u2l9_view_history_fixtures DEPENDS "${_u2l9_history_file}")
    add_dependencies(lgaed_curricular_input_lab_tests lgaed_u2l9_view_history_fixtures)
    add_dependencies(lgaed_exact_prediction_view_tests lgaed_u2l9_view_history_fixtures)
    add_test(NAME native.curricular_input_lab_history
        COMMAND lgaed_curricular_input_lab_tests --u2l9-history "${_u2l9_history_file}")
    add_test(NAME native.prepared_input_predictions_history
        COMMAND lgaed_exact_prediction_view_tests --prepared-input-history "${_u2l9_history_file}")
    set_tests_properties(native.curricular_input_lab_history PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    set_tests_properties(native.prepared_input_predictions_history PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
endfunction()
