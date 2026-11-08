# Shared authentic pre-adoption fixtures used by the normal compiler tests
# and the original U6 reader flows. Keep the directory relative to their exes.
include_guard(GLOBAL)

set(LGAED_COMPILER_CONTRACT_FIXTURES
    "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/crt-b9e32b9/lesson_clasificar_caracteres_y_normalizar_una_copia.json"
    "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/exit-585cef9/lesson_compartir_flujos_sin_acoplar_el_calculo.json"
    "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/exit-l26-e8389c8/lesson_construir_un_programa_de_carga_y_guardado.json"
    "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u6-diagnostics-e8389c8/lesson_declaraciones_y_firmas.json"
    "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u6-diagnostics-e8389c8/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json"
    "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u6-diagnostics-e8389c8/lesson_construir_un_proyecto_desde_su_interfaz.json")
add_custom_command(OUTPUT ${LGAED_COMPILER_CONTRACT_FIXTURES}
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/compiler_contract_fixture_source.py"
        --output-dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures"
    DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/compiler_contract_fixture_source.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/diagnostic_fixture_source.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying immutable pre-adoption compiler contracts from local Git"
    VERBATIM)
add_custom_target(lgaed_compiler_contract_fixtures DEPENDS ${LGAED_COMPILER_CONTRACT_FIXTURES})
