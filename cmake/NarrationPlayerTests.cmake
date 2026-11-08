# The application build and focused probe use the same production platform
# library and unchanged test source. Only the rate test accepts an output
# directory; --qa-path deliberately resolves its own unique LocalAppData QA
# through the real Windows known-folder API.
include_guard(GLOBAL)

function(lgaed_add_narration_player_tests qa_root)
    add_executable(lgaed_narration_player_tests native/platform/windows/narration_player_tests.cpp)
    target_link_libraries(lgaed_narration_player_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.narration_player_rates COMMAND lgaed_narration_player_tests "${qa_root}")
    set_tests_properties(native.narration_player_rates PROPERTIES TIMEOUT 45)
    add_test(NAME native.narration_player_qa_path COMMAND lgaed_narration_player_tests --qa-path)
    set_tests_properties(native.narration_player_qa_path PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
endfunction()
