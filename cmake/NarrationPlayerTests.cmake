# The application build and focused probe use the same production platform
# library and test source. Only the rate test accepts an output directory;
# --qa-path and --failure-diagnostic resolve unique LocalAppData QA instances
# through the real Windows known-folder API. The negative test diagnoses a
# real MediaFailed event without claiming that audio was reproduced.
include_guard(GLOBAL)

function(lgaed_add_narration_player_failure_test)
    if(NOT TARGET lgaed_narration_player_tests)
        add_executable(lgaed_narration_player_tests native/platform/windows/narration_player_tests.cpp)
        target_link_libraries(lgaed_narration_player_tests PRIVATE lgaed_platform_windows)
    endif()
    if(NOT TEST native.narration_player_failure_diagnostic)
        add_test(NAME native.narration_player_failure_diagnostic COMMAND lgaed_narration_player_tests --failure-diagnostic)
        set_tests_properties(native.narration_player_failure_diagnostic PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
    endif()
endfunction()

function(lgaed_add_narration_player_tests qa_root)
    lgaed_add_narration_player_failure_test()
    add_test(NAME native.narration_player_rates COMMAND lgaed_narration_player_tests "${qa_root}")
    set_tests_properties(native.narration_player_rates PROPERTIES TIMEOUT 45)
    add_test(NAME native.narration_player_qa_path COMMAND lgaed_narration_player_tests --qa-path)
    set_tests_properties(native.narration_player_qa_path PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
endfunction()
