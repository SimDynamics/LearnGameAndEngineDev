# Focused native QA uses the same native toolchain as the application.
# Only Python/Git discovery differs: an ephemeral Windows runner has no
# personal runtime installation paths. Import/player/reader-build modes do not
# load models or JVM; the worker mode additionally selects its full narration
# tool closure through Resolve-NativeTools.ps1 before building the runtime.
# The normal application selector is unchanged.
function(lgaed_select_windows_state_import_qa_tools)
    if(NOT CMAKE_TOOLCHAIN_FILE)
        message(FATAL_ERROR "Focused Windows QA requires cmake/WindowsToolchain.cmake.")
    endif()
    file(REAL_PATH "${CMAKE_TOOLCHAIN_FILE}" _selected_toolchain BASE_DIRECTORY "${CMAKE_SOURCE_DIR}")
    file(REAL_PATH "${CMAKE_SOURCE_DIR}/cmake/WindowsToolchain.cmake" _required_toolchain)
    string(TOLOWER "${_selected_toolchain}" _selected_toolchain)
    string(TOLOWER "${_required_toolchain}" _required_toolchain)
    if(NOT _selected_toolchain STREQUAL _required_toolchain)
        message(FATAL_ERROR "Focused Windows QA must use the official Windows toolchain without bypassing its MSVC/SDK checks.")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/toolchains/windows.versions.json")
    file(READ "${CMAKE_SOURCE_DIR}/toolchains/windows.versions.json" _tools)
    string(JSON _compiler_version GET "${_tools}" visualStudio compilerVersion)
    if(NOT CMAKE_CXX_COMPILER_VERSION STREQUAL _compiler_version)
        message(FATAL_ERROR "Focused Windows QA requires compiler ${_compiler_version}; selected ${CMAKE_CXX_COMPILER_VERSION}.")
    endif()

    string(JSON _python_version GET "${_tools}" python version)
    find_program(Python3_EXECUTABLE NAMES python.exe python3.exe python REQUIRED)
    if(NOT EXISTS "${Python3_EXECUTABLE}" OR IS_DIRECTORY "${Python3_EXECUTABLE}")
        message(FATAL_ERROR "The selected QA Python is not an executable file: ${Python3_EXECUTABLE}")
    endif()
    execute_process(COMMAND "${Python3_EXECUTABLE}" -I -S -B -c
        "import platform; print(platform.python_version())"
        RESULT_VARIABLE _python_exit OUTPUT_VARIABLE _python_actual ERROR_VARIABLE _python_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _python_exit EQUAL 0 OR NOT _python_actual STREQUAL _python_version)
        message(FATAL_ERROR "Focused Windows QA requires Python ${_python_version}; selected '${_python_actual}' at ${Python3_EXECUTABLE}. ${_python_error}")
    endif()
    set(Python3_EXECUTABLE "${Python3_EXECUTABLE}" CACHE FILEPATH "Verified Windows QA Python" FORCE)

    string(JSON _git_version GET "${_tools}" git version)
    find_program(GIT_EXECUTABLE NAMES git.exe git REQUIRED)
    if(NOT EXISTS "${GIT_EXECUTABLE}" OR IS_DIRECTORY "${GIT_EXECUTABLE}")
        message(FATAL_ERROR "The selected QA Git is not an executable file: ${GIT_EXECUTABLE}")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" --version
        RESULT_VARIABLE _git_exit OUTPUT_VARIABLE _git_actual ERROR_VARIABLE _git_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _git_exit EQUAL 0 OR NOT _git_actual STREQUAL "git version ${_git_version}")
        message(FATAL_ERROR "Focused Windows QA requires Git ${_git_version}; selected '${_git_actual}' at ${GIT_EXECUTABLE}. ${_git_error}")
    endif()
    set(GIT_EXECUTABLE "${GIT_EXECUTABLE}" CACHE FILEPATH "Verified Windows QA Git" FORCE)
    set(GITCOMMAND "${GIT_EXECUTABLE}" CACHE FILEPATH "Verified Windows QA Git for CTest" FORCE)
    message(STATUS "Focused Windows QA tools: Python ${_python_actual} at ${Python3_EXECUTABLE}; ${_git_actual} at ${GIT_EXECUTABLE}")
endfunction()
