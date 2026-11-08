include_guard(GLOBAL)

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
endfunction()
