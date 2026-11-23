# The same original Windows loader, runner and StateStore verify the ten
# published nested CMake recipes in the product and isolated hosted builds.
function(lgaed_add_nested_cmake_format_tests source_root)
    if(TARGET lgaed_nested_cmake_format_tests)
        return()
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_nested_cmake_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/nested-cmake")
    set(_nested_cmake_outputs "${_nested_cmake_dir}/manifest.json")
    foreach(_lesson IN ITEMS datos_y_listas_de_objetivos
            nombrar_reglas_y_separar_acuerdos
            reorganizar_la_descripcion_conservando_el_comprobador)
        foreach(_version IN ITEMS before after)
            list(APPEND _nested_cmake_outputs
                "${_nested_cmake_dir}/${_lesson}.${_version}.json")
        endforeach()
    endforeach()
    foreach(_control IN ITEMS control.filename invalid.logic invalid.path
            invalid.literal invalid.filename)
        list(APPEND _nested_cmake_outputs "${_nested_cmake_dir}/${_control}.json")
    endforeach()
    add_custom_command(OUTPUT ${_nested_cmake_outputs}
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/nested_cmake_format_fixture_source.py"
            --output-dir "${_nested_cmake_dir}"
        DEPENDS "${source_root}/scripts/curriculum/nested_cmake_format_fixture_source.py"
            "${source_root}/scripts/curriculum/format_batch_history.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
            "${source_root}/scripts/curriculum/code_format_compat.py"
        WORKING_DIRECTORY "${source_root}"
        VERBATIM)
    add_custom_target(lgaed_nested_cmake_format_fixtures
        DEPENDS ${_nested_cmake_outputs})
    add_executable(lgaed_nested_cmake_format_tests
        "${source_root}/native/platform/windows/nested_cmake_format_tests.cpp")
    target_link_libraries(lgaed_nested_cmake_format_tests
        PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_nested_cmake_format_tests lgaed_nested_cmake_format_fixtures)
    add_test(NAME native.nested_cmake_format
        COMMAND lgaed_nested_cmake_format_tests "${_nested_cmake_dir}")
    set_tests_properties(native.nested_cmake_format
        PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
    add_test(NAME native.nested_cmake_format_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${source_root}/scripts/curriculum" -p test_nested_cmake_format.py -v)
    set_tests_properties(native.nested_cmake_format_authoring PROPERTIES TIMEOUT 90)
endfunction()
