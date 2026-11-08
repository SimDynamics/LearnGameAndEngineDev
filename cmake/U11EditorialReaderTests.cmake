# Shared published editorial regressions, using the original reader and
# authentic fixtures defined by StateImportTests.cmake without its state tests.
include_guard(GLOBAL)

function(lgaed_add_u11_editorial_reader_tests)
    add_dependencies(lgaed_lesson_reader_tests lgaed_u11_assignment_published_fixtures lgaed_u11_remainder_published_fixtures)
    foreach(assignment_reader_phase RANGE 0 5)
        add_test(NAME native.u11_assignment_published_reader_${assignment_reader_phase}
            COMMAND lgaed_lesson_reader_tests --u11-assignment-published
                "${LGAED_U11_ASSIGNMENT_PUBLISHED_DIR}/assignment-before.json"
                "${LGAED_U11_ASSIGNMENT_PUBLISHED_DIR}/assignment-after.json" ${assignment_reader_phase})
        set_tests_properties(native.u11_assignment_published_reader_${assignment_reader_phase} PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    endforeach()
    foreach(remainder_publication IN ITEMS 26 30)
        foreach(remainder_reader_phase RANGE 0 1)
            add_test(NAME native.u11_remainder_published${remainder_publication}_reader_${remainder_reader_phase}
                COMMAND lgaed_lesson_reader_tests --u11-remainder-published
                    "${LGAED_U11_REMAINDER_PUBLISHED_DIR}/before.json"
                    "${LGAED_U11_REMAINDER_PUBLISHED_DIR}/published${remainder_publication}.json"
                    ${remainder_publication} ${remainder_reader_phase})
            set_tests_properties(native.u11_remainder_published${remainder_publication}_reader_${remainder_reader_phase}
                PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        endforeach()
    endforeach()
endfunction()
