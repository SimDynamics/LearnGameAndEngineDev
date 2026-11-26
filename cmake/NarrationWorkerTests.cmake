include_guard(GLOBAL)

# Exercise real worker planning without synthesis, audio devices or GUI.
function(lgaed_add_concept_diagram_narration_test)
    add_test(NAME native.concept_diagram_narration COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_curriculum_visible_semantics.py"
        CurriculumVisibleSemanticsTest.test_concept_diagram_prepares_exact_native_fields_in_authored_order)
    set_tests_properties(native.concept_diagram_narration PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
endfunction()

function(lgaed_add_narration_worker_tests)
    add_executable(lgaed_narration_worker_tests native/platform/windows/narration_worker_tests.cpp)
    target_link_libraries(lgaed_narration_worker_tests PRIVATE lgaed_platform_windows)
    add_dependencies(lgaed_narration_worker_tests lgaed_narration_runtime)
    add_test(NAME native.narration_worker COMMAND lgaed_narration_worker_tests
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(native.narration_worker PROPERTIES TIMEOUT 180)
    add_test(NAME native.narration_spanish_pcm COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_worker.py"
        WindowsNarrationWorkerTest.test_03_spanish_math_is_progressive_through_numbers_and_operators)
    set_tests_properties(native.narration_spanish_pcm PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    add_test(NAME native.narration_large_lesson_transport COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_background_plan.py"
        WindowsPageNarrationRevisionTest.test_complete_published_assetcheck_history_exceeds_old_transport_limit)
    set_tests_properties(native.narration_large_lesson_transport PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
endfunction()
