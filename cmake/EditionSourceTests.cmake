include_guard(GLOBAL)

function(lgaed_add_edition_source_tests source_root presentation_target)
    find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
    set(_edition_source_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/edition-source")
    set(_edition_source_files
        "${_edition_source_dir}/manifest.json"
        "${_edition_source_dir}/content/es/programming/courses.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/course_cpp_fundamentals.json"
        "${_edition_source_dir}/originals/u6l2.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__funciones_y_contratos/lesson_declaraciones_y_firmas.json"
        "${_edition_source_dir}/originals/u6l19.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__funciones_y_contratos/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json"
        "${_edition_source_dir}/originals/u6l20.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__funciones_y_contratos/lesson_construir_un_proyecto_desde_su_interfaz.json"
        "${_edition_source_dir}/originals/u9l6.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__datos_propios_y_estado_valido/lesson_consultas_const_y_objetos_constantes.json"
        "${_edition_source_dir}/originals/u9l10.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__datos_propios_y_estado_valido/lesson_valores_por_defecto_con_significado.json"
        "${_edition_source_dir}/originals/u9l11.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__datos_propios_y_estado_valido/lesson_argumentos_con_tipos_que_expresan_su_funcion.json"
        "${_edition_source_dir}/originals/u9l13.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json"
        "${_edition_source_dir}/originals/u11l5.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__texto_interpretacion_y_flujos/lesson_estados_del_flujo_y_datos_validos.json"
        "${_edition_source_dir}/originals/u11l6.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__texto_interpretacion_y_flujos/lesson_validar_una_operacion_y_recuperar_la_siguiente.json"
        "${_edition_source_dir}/originals/u11l7.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__texto_interpretacion_y_flujos/lesson_construir_un_lector_de_entrada_preparada.json"
        "${_edition_source_dir}/originals/u11l8.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__texto_interpretacion_y_flujos/lesson_tokens_con_categoria_y_valor.json"
        "${_edition_source_dir}/originals/u11l14.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__texto_interpretacion_y_flujos/lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json"
        "${_edition_source_dir}/originals/u11l15.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__texto_interpretacion_y_flujos/lesson_construir_un_interprete_con_historial_y_pruebas.json"
        "${_edition_source_dir}/originals/u11l16.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__texto_interpretacion_y_flujos/lesson_resto_y_asociacion_en_el_interprete.json"
        "${_edition_source_dir}/originals/u11l18.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__texto_interpretacion_y_flujos/lesson_una_tabla_persistente_de_nombres.json"
        "${_edition_source_dir}/originals/u12l13.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__archivos_y_programa_completo/lesson_compartir_flujos_sin_acoplar_el_calculo.json"
        "${_edition_source_dir}/originals/u12l20.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json"
        "${_edition_source_dir}/originals/u12l26.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__archivos_y_programa_completo/lesson_construir_un_programa_de_carga_y_guardado.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__funciones_y_contratos/unit_edition_es_1__funciones_y_contratos.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__datos_propios_y_estado_valido/unit_edition_es_1__datos_propios_y_estado_valido.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__texto_interpretacion_y_flujos/unit_edition_es_1__texto_interpretacion_y_flujos.json"
        "${_edition_source_dir}/content/es/programming/cpp_fundamentals/edition_es_1__archivos_y_programa_completo/unit_edition_es_1__archivos_y_programa_completo.json"
    )
    add_custom_command(OUTPUT ${_edition_source_files}
        COMMAND "${Python3_EXECUTABLE}" -B
            "${source_root}/scripts/curriculum/edition_source_fixture_source.py"
            --output-dir "${_edition_source_dir}"
        DEPENDS
            "${source_root}/scripts/curriculum/edition_source_fixture_source.py"
            "${source_root}/scripts/curriculum/course_edition.py"
            "${source_root}/scripts/curriculum/stable_state_layout.py"
            "${source_root}/scripts/curriculum/diagnostic_fixture_source.py"
        VERBATIM)
    add_custom_target(lgaed_edition_source_fixtures DEPENDS ${_edition_source_files})
    add_executable(lgaed_edition_source_tests
        "${source_root}/native/platform/windows/edition_source_tests.cpp")
    target_link_libraries(lgaed_edition_source_tests PRIVATE ${presentation_target} lgaed_host_options)
    add_dependencies(lgaed_edition_source_tests lgaed_edition_source_fixtures lgaed_cpp_tool_host)
    set(LGAED_EDITION_SOURCE_WORKER_RUNTIME "${source_root}/out/narration/runtime.json"
        CACHE FILEPATH "Real Windows math worker runtime used by edition-source QA")
    add_test(NAME native.edition_source
        COMMAND lgaed_edition_source_tests "${_edition_source_dir}/manifest.json"
            "${LGAED_EDITION_SOURCE_WORKER_RUNTIME}")
    set_tests_properties(native.edition_source PROPERTIES TIMEOUT 1200 RUN_SERIAL TRUE)
    add_test(NAME native.edition_source_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest
            test_edition_source_authoring test_edition_source_fixture_source -v)
    set_tests_properties(native.edition_source_authoring PROPERTIES TIMEOUT 300
        WORKING_DIRECTORY "${source_root}/scripts/curriculum")
    add_test(NAME native.edition_source_legacy
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest
            test_course_edition test_crt_output_variant test_output_exit_code_contract -v)
    set_tests_properties(native.edition_source_legacy PROPERTIES TIMEOUT 300
        WORKING_DIRECTORY "${source_root}/scripts/curriculum")
endfunction()
