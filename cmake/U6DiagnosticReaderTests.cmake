# The full build and focused U6 mode use the same original diagnostic flows.
# CompilerContractFixtures.cmake supplies the authenticated source files.
include_guard(GLOBAL)

function(lgaed_add_u6_diagnostic_reader_tests)
    add_dependencies(lgaed_u6_diagnostic_flow_tests lgaed_compiler_contract_fixtures lgaed_cpp_tool_host lgaed_narration_runtime)
    foreach(u6_flow IN ITEMS declarations interfaces project)
        add_test(NAME native.u6_diagnostic_flow_${u6_flow} COMMAND lgaed_u6_diagnostic_flow_tests ${u6_flow})
        set_tests_properties(native.u6_diagnostic_flow_${u6_flow} PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
    endforeach()
endfunction()
