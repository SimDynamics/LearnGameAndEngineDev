# Shared original compiler-history reader regressions. The authentic before/
# after sources come from StateImportTests.cmake without registering state tests.
include_guard(GLOBAL)

function(lgaed_add_compiler_historical_reader_tests)
    add_dependencies(lgaed_u11_declarations_reader_tests lgaed_compiler_import_published_fixtures)
    foreach(compiler_reader IN ITEMS declarations assignments constants)
        if(compiler_reader STREQUAL "declarations")
            set(compiler_reader_lesson u11l19)
        elseif(compiler_reader STREQUAL "assignments")
            set(compiler_reader_lesson u11l20)
        else()
            set(compiler_reader_lesson u11l21)
        endif()
        foreach(compiler_reader_phase RANGE 0 1)
            add_test(NAME native.u11_${compiler_reader}_historical_reader_${compiler_reader_phase}
                COMMAND lgaed_u11_declarations_reader_tests --${compiler_reader}-historical
                    "${LGAED_COMPILER_IMPORT_PUBLISHED_DIR}/${compiler_reader_lesson}-before.json"
                    "${LGAED_COMPILER_IMPORT_PUBLISHED_DIR}/${compiler_reader_lesson}-after.json"
                    "${compiler_reader_phase}")
            set_tests_properties(native.u11_${compiler_reader}_historical_reader_${compiler_reader_phase}
                PROPERTIES TIMEOUT 420 RUN_SERIAL TRUE)
        endforeach()
    endforeach()
    foreach(compiler_reader IN ITEMS session characters)
        if(compiler_reader STREQUAL "session")
            set(compiler_reader_unit u11)
            set(compiler_reader_lesson u11l22)
        else()
            set(compiler_reader_unit u12)
            set(compiler_reader_lesson u12l20)
        endif()
        foreach(compiler_reader_phase RANGE 0 3)
            add_test(NAME native.${compiler_reader_unit}_${compiler_reader}_historical_reader_${compiler_reader_phase}
                COMMAND lgaed_u11_declarations_reader_tests --${compiler_reader}-historical
                    "${LGAED_COMPILER_IMPORT_PUBLISHED_DIR}/${compiler_reader_lesson}-before.json"
                    "${LGAED_COMPILER_IMPORT_PUBLISHED_DIR}/${compiler_reader_lesson}-after.json"
                    "${compiler_reader_phase}")
            set_tests_properties(native.${compiler_reader_unit}_${compiler_reader}_historical_reader_${compiler_reader_phase}
                PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        endforeach()
    endforeach()
endfunction()
