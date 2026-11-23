# Shared, isolated QA for root-authored input and scalar roles. Product and hosted
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
    set(_extended_input_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/continued-input")
    set(_extended_input_files)
    foreach(_lesson IN ITEMS u2l10 u3l1 u3l2)
        foreach(_file IN ITEMS before.json after.json manifest.json)
            list(APPEND _extended_input_files "${_extended_input_dir}/${_lesson}/${_file}")
        endforeach()
    endforeach()
    add_custom_command(OUTPUT ${_extended_input_files}
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/input_dependency_fixture_source.py"
            --extended-input --output-dir "${_extended_input_dir}"
        DEPENDS "${source_root}/scripts/curriculum/input_dependency_fixture_source.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
        VERBATIM)
    add_custom_target(lgaed_continued_input_fixtures DEPENDS ${_extended_input_files})
    add_dependencies(lgaed_input_dependency_state_tests lgaed_continued_input_fixtures)
    add_test(NAME native.input_dependency_state
        COMMAND lgaed_input_dependency_state_tests "${_input_fixture_dir}")
    set_tests_properties(native.input_dependency_state PROPERTIES TIMEOUT 420 RUN_SERIAL TRUE)
    add_test(NAME native.continued_input_dependency_state
        COMMAND lgaed_input_dependency_state_tests --extended-input "${_extended_input_dir}")
    set_tests_properties(native.continued_input_dependency_state PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)

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

function(lgaed_add_local_role_state_tests source_root)
    if(TARGET lgaed_local_role_state_tests)
        return()
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_role_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u2l10-local-roles")
    add_custom_command(OUTPUT "${_role_fixture_dir}/before.json"
            "${_role_fixture_dir}/after.json" "${_role_fixture_dir}/manifest.json"
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/local_role_fixture_source.py"
            --output-dir "${_role_fixture_dir}"
        DEPENDS "${source_root}/scripts/curriculum/local_role_fixture_source.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
        VERBATIM)
    add_custom_target(lgaed_local_role_fixtures
        DEPENDS "${_role_fixture_dir}/before.json"
            "${_role_fixture_dir}/after.json" "${_role_fixture_dir}/manifest.json")
    add_executable(lgaed_local_role_state_tests
        "${source_root}/native/platform/windows/local_role_state_tests.cpp")
    target_link_libraries(lgaed_local_role_state_tests
        PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_local_role_state_tests lgaed_local_role_fixtures)
    add_test(NAME native.current_u2l10_local_role_state
        COMMAND lgaed_local_role_state_tests "${_role_fixture_dir}")
    set_tests_properties(native.current_u2l10_local_role_state PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
    add_executable(lgaed_local_role_probe_tests
        "${source_root}/native/core/cpp_local_role_probe_tests.cpp")
    target_include_directories(lgaed_local_role_probe_tests PRIVATE "${source_root}/native")
    target_link_libraries(lgaed_local_role_probe_tests PRIVATE lgaed_host_options)
    add_test(NAME native.local_role_probe COMMAND lgaed_local_role_probe_tests)
    set_tests_properties(native.local_role_probe PROPERTIES TIMEOUT 30)
    add_test(NAME native.local_role_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${source_root}/scripts/curriculum" -p test_local_role_probe_authoring.py -v)
    set_tests_properties(native.local_role_authoring PROPERTIES TIMEOUT 90)
endfunction()

# Current and archived Win32 controls preserve authenticated publications,
# including the same-page reference, without disabling their mandatory case.
function(lgaed_add_u2l9_input_view_history_tests source_root)
    if(TARGET lgaed_u2l9_view_history_fixtures)
        return()
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_u2l9_history_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u2l9-view-history")
    set(_u2l9_history_file "${_u2l9_history_dir}/u2l9-archived.json")
    set(_u2l9_same_page_file "${_u2l9_history_dir}/u2l9-same-page.json")
    add_custom_command(OUTPUT "${_u2l9_history_file}"
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/u2l9_history_fixture_source.py"
            --output-dir "${_u2l9_history_dir}"
        DEPENDS "${source_root}/scripts/curriculum/u2l9_history_fixture_source.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
        VERBATIM)
    add_custom_command(OUTPUT "${_u2l9_same_page_file}"
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/u2l9_history_fixture_source.py"
            --output-dir "${_u2l9_history_dir}" --same-page
        DEPENDS "${source_root}/scripts/curriculum/u2l9_history_fixture_source.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
        VERBATIM)
    add_custom_target(lgaed_u2l9_view_history_fixtures
        DEPENDS "${_u2l9_history_file}" "${_u2l9_same_page_file}")
    add_dependencies(lgaed_curricular_input_lab_tests lgaed_u2l9_view_history_fixtures)
    add_dependencies(lgaed_exact_prediction_view_tests lgaed_u2l9_view_history_fixtures)
    add_test(NAME native.curricular_input_lab_history
        COMMAND lgaed_curricular_input_lab_tests --u2l9-history "${_u2l9_history_file}")
    add_test(NAME native.curricular_input_lab_same_page
        COMMAND lgaed_curricular_input_lab_tests --u2l9-same-page "${_u2l9_same_page_file}")
    add_test(NAME native.prepared_input_predictions_history
        COMMAND lgaed_exact_prediction_view_tests --prepared-input-history "${_u2l9_history_file}")
    set_tests_properties(native.curricular_input_lab_history PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    set_tests_properties(native.curricular_input_lab_same_page PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    set_tests_properties(native.prepared_input_predictions_history PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
endfunction()

# Preserve stdout-era history while adopting only fully sealed private v1
# receipts. This uses the production Windows runner and an isolated StateStore.
function(lgaed_add_local_dependency_receipt_tests source_root)
    if(TARGET lgaed_local_dependency_receipt_tests)
        return()
    endif()
    add_executable(lgaed_local_dependency_receipt_tests
        "${source_root}/native/platform/windows/local_dependency_receipt_tests.cpp")
    target_link_libraries(lgaed_local_dependency_receipt_tests
        PRIVATE lgaed_platform_windows lgaed_host_options)
    add_test(NAME native.local_dependency_receipts
        COMMAND lgaed_local_dependency_receipt_tests)
    set_tests_properties(native.local_dependency_receipts
        PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
endfunction()

# Exercise the real reader across all sixteen variable/input owners in nine
# authenticated lessons. Only private QA copies receive the new declarations.
# Keep presentation out of the StateImport-only configuration.
function(lgaed_add_local_dependency_view_tests source_root presentation_target)
    if(TARGET lgaed_local_dependency_view_tests)
        return()
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_view_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/local-dependency-view")
    set(_view_fixture_files "${_view_fixture_dir}/manifest.json")
    foreach(_lesson IN ITEMS u2l1 u2l2 u2l3 u2l7 u2l8 u2l9 u2l10 u3l1 u3l2)
        foreach(_file IN ITEMS before.json after.json)
            list(APPEND _view_fixture_files "${_view_fixture_dir}/${_lesson}/${_file}")
        endforeach()
    endforeach()
    add_custom_command(OUTPUT ${_view_fixture_files}
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/local_dependency_view_fixture_source.py"
            --output-dir "${_view_fixture_dir}"
        DEPENDS "${source_root}/scripts/curriculum/local_dependency_view_fixture_source.py"
            "${source_root}/scripts/curriculum/code_format_compat.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
            "${source_root}/scripts/curriculum/local_scalar_fixture_source.py"
            "${source_root}/scripts/curriculum/input_dependency_fixture_source.py"
            "${source_root}/scripts/curriculum/local_role_fixture_source.py"
        VERBATIM)
    add_custom_target(lgaed_local_dependency_view_fixtures DEPENDS ${_view_fixture_files})
    add_executable(lgaed_local_dependency_view_tests
        "${source_root}/native/presentation/windows/local_dependency_view_tests.cpp")
    target_link_libraries(lgaed_local_dependency_view_tests
        PRIVATE ${presentation_target} lgaed_host_options)
    add_dependencies(lgaed_local_dependency_view_tests lgaed_local_dependency_view_fixtures)
    foreach(_lesson IN ITEMS u2l1 u2l2 u2l3 u2l7 u2l8 u2l9 u2l10 u3l1 u3l2)
        add_test(NAME native.local_dependency_view_${_lesson}
            COMMAND lgaed_local_dependency_view_tests "${_view_fixture_dir}/manifest.json" "${_lesson}")
        set_tests_properties(native.local_dependency_view_${_lesson}
            PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    endforeach()
    add_test(NAME native.local_dependency_view_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${source_root}/scripts/curriculum" -p test_local_dependency_view_authoring.py -v)
    set_tests_properties(native.local_dependency_view_authoring PROPERTIES TIMEOUT 120)

    # Separate complete U3 publications exercise append-only joint observations
    # without changing the nine historical manifests or their sixteen owners.
    set(_joint_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/joint-local-int")
    set(_joint_fixture_files "${_joint_fixture_dir}/manifest.json")
    foreach(_lesson IN ITEMS u3l7 u3l8)
        foreach(_file IN ITEMS before.json after.json)
            list(APPEND _joint_fixture_files "${_joint_fixture_dir}/${_lesson}/${_file}")
        endforeach()
    endforeach()
    add_custom_command(OUTPUT ${_joint_fixture_files}
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/joint_local_int_fixture_source.py"
            --output-dir "${_joint_fixture_dir}"
        DEPENDS "${source_root}/scripts/curriculum/joint_local_int_fixture_source.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
        VERBATIM)
    add_custom_target(lgaed_joint_local_int_fixtures DEPENDS ${_joint_fixture_files})
    add_dependencies(lgaed_local_dependency_view_tests lgaed_joint_local_int_fixtures)
    add_test(NAME native.joint_local_int_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${source_root}/scripts/curriculum" -p test_joint_local_int_fixture_source.py -v)
    set_tests_properties(native.joint_local_int_authoring PROPERTIES TIMEOUT 120)
    add_test(NAME native.joint_local_int_view
        COMMAND lgaed_local_dependency_view_tests --joint-local-int "${_joint_fixture_dir}/manifest.json")
    set_tests_properties(native.joint_local_int_view PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
endfunction()
