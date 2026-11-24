# The product pin remains authoritative for local builds. The isolated hosted
# provisioning script explicitly selects the independently reviewed QA pin.
function(lgaed_read_msvc_profile out_toolset out_compiler)
    if("$ENV{LGAED_HOSTED_MSVC_QA}" STREQUAL "1")
        if(NOT "$ENV{GITHUB_ACTIONS}" STREQUAL "true" OR
           NOT "$ENV{RUNNER_ENVIRONMENT}" STREQUAL "github-hosted")
            message(FATAL_ERROR "Hosted MSVC QA profile requires a GitHub-hosted runner.")
        endif()
        file(READ "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../toolchains/windows.hosted-msvc.json" _manifest)
        string(JSON _toolset GET "${_manifest}" msvcToolset)
        string(JSON _compiler GET "${_manifest}" compilerVersion)
    else()
        file(READ "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../toolchains/windows.versions.json" _manifest)
        string(JSON _toolset GET "${_manifest}" visualStudio msvcToolset)
        string(JSON _compiler GET "${_manifest}" visualStudio compilerVersion)
    endif()
    set(${out_toolset} "${_toolset}" PARENT_SCOPE)
    set(${out_compiler} "${_compiler}" PARENT_SCOPE)
endfunction()
