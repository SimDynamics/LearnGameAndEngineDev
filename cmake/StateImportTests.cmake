# Shared authentic fixtures and unchanged native state-test implementation.
# Both the full application configuration and focused import QA use these
# generators, target definitions and four published-history test commands.
include_guard(GLOBAL)

# Exporters resolve historical objects through Git subprocesses. Use the same
# verified Git for those children, not another executable inherited on PATH.
get_filename_component(_lgaed_state_git_directory "${GIT_EXECUTABLE}" DIRECTORY)
set(_lgaed_state_fixture_python "${CMAKE_COMMAND}" -E env
    --modify "PATH=path_list_prepend:${_lgaed_state_git_directory}" -- "${Python3_EXECUTABLE}" -B)

set(LGAED_WORK_EXACT_EDITORIAL_DIR "${CMAKE_CURRENT_BINARY_DIR}/work-exact-editorials")
set(LGAED_WORK_EXACT_EDITORIAL_FIXTURES
    "${LGAED_WORK_EXACT_EDITORIAL_DIR}/duplication-before.json"
    "${LGAED_WORK_EXACT_EDITORIAL_DIR}/duplication-after.json"
    "${LGAED_WORK_EXACT_EDITORIAL_DIR}/remainder-before.json"
    "${LGAED_WORK_EXACT_EDITORIAL_DIR}/remainder-after.json"
    "${LGAED_WORK_EXACT_EDITORIAL_DIR}/manifest.json")
add_custom_command(OUTPUT ${LGAED_WORK_EXACT_EDITORIAL_FIXTURES}
    COMMAND ${_lgaed_state_fixture_python} "${CMAKE_SOURCE_DIR}/scripts/curriculum/work_exact_editorial_fixture_source.py"
        --output-dir "${LGAED_WORK_EXACT_EDITORIAL_DIR}"
    DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/work_exact_editorial_fixture_source.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/validar_json_programacion.py"
    VERBATIM)
add_custom_target(lgaed_work_exact_editorial_fixtures DEPENDS ${LGAED_WORK_EXACT_EDITORIAL_FIXTURES})

set(LGAED_U11_REMAINDER_PUBLISHED_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u11-remainder-published")
set(LGAED_U11_REMAINDER_PUBLISHED_FIXTURES
    "${LGAED_U11_REMAINDER_PUBLISHED_DIR}/before.json"
    "${LGAED_U11_REMAINDER_PUBLISHED_DIR}/published26.json"
    "${LGAED_U11_REMAINDER_PUBLISHED_DIR}/published30.json"
    "${LGAED_U11_REMAINDER_PUBLISHED_DIR}/manifest.json")
add_custom_command(OUTPUT ${LGAED_U11_REMAINDER_PUBLISHED_FIXTURES}
    COMMAND ${_lgaed_state_fixture_python} "${CMAKE_SOURCE_DIR}/scripts/curriculum/u11_remainder_editorial_fixture_source.py"
        --output-dir "${LGAED_U11_REMAINDER_PUBLISHED_DIR}"
    DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/u11_remainder_editorial_fixture_source.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/text_semantics.py"
    COMMENT "Verifying complete U11L16 publications with seventeen, twenty-six and thirty editorial edges"
    VERBATIM)
add_custom_target(lgaed_u11_remainder_published_fixtures DEPENDS ${LGAED_U11_REMAINDER_PUBLISHED_FIXTURES})

set(LGAED_U11_ASSIGNMENT_PUBLISHED_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u11-assignment-published")
set(LGAED_U11_ASSIGNMENT_PUBLISHED_FIXTURES
    "${LGAED_U11_ASSIGNMENT_PUBLISHED_DIR}/assignment-before.json"
    "${LGAED_U11_ASSIGNMENT_PUBLISHED_DIR}/assignment-after.json"
    "${LGAED_U11_ASSIGNMENT_PUBLISHED_DIR}/manifest.json")
add_custom_command(OUTPUT ${LGAED_U11_ASSIGNMENT_PUBLISHED_FIXTURES}
    COMMAND ${_lgaed_state_fixture_python} "${CMAKE_SOURCE_DIR}/scripts/curriculum/u11_assignment_editorial_fixture_source.py"
        --published --output-dir "${LGAED_U11_ASSIGNMENT_PUBLISHED_DIR}"
    DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/u11_assignment_editorial_fixture_source.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/text_semantics.py"
    COMMENT "Verifying the authentic U11L20 publications with twenty-one and twenty-five editorial edges"
    VERBATIM)
add_custom_target(lgaed_u11_assignment_published_fixtures DEPENDS ${LGAED_U11_ASSIGNMENT_PUBLISHED_FIXTURES})

set(LGAED_COMPILER_IMPORT_PUBLISHED_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/compiler-import-published")
set(LGAED_COMPILER_IMPORT_PUBLISHED_FIXTURES "${LGAED_COMPILER_IMPORT_PUBLISHED_DIR}/manifest.json")
foreach(compiler_import_lesson IN ITEMS u11l19 u11l20 u11l21 u11l22 u12l20)
    foreach(compiler_import_side IN ITEMS before after)
        list(APPEND LGAED_COMPILER_IMPORT_PUBLISHED_FIXTURES
            "${LGAED_COMPILER_IMPORT_PUBLISHED_DIR}/${compiler_import_lesson}-${compiler_import_side}.json")
    endforeach()
endforeach()
add_custom_command(OUTPUT ${LGAED_COMPILER_IMPORT_PUBLISHED_FIXTURES}
    COMMAND ${_lgaed_state_fixture_python} "${CMAKE_SOURCE_DIR}/scripts/curriculum/compiler_import_fixture_source.py"
        --output-dir "${LGAED_COMPILER_IMPORT_PUBLISHED_DIR}"
    DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/compiler_import_fixture_source.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/diagnostic_fixture_source.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/text_semantics.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/image_resources.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/bibliography_revision.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying five published positional-to-v2 compiler import pairs"
    VERBATIM)
add_custom_target(lgaed_compiler_import_published_fixtures DEPENDS ${LGAED_COMPILER_IMPORT_PUBLISHED_FIXTURES})

set(LGAED_U11_NAMES_BEFORE_FILE "${CMAKE_CURRENT_BINARY_DIR}/u11-names-fixtures/before.json")
add_custom_command(OUTPUT "${LGAED_U11_NAMES_BEFORE_FILE}"
    COMMAND ${_lgaed_state_fixture_python} "${CMAKE_SOURCE_DIR}/scripts/curriculum/u11_names_fixture_source.py"
        --output "${LGAED_U11_NAMES_BEFORE_FILE}" --git "${GIT_EXECUTABLE}"
    DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/u11_names_fixture_source.py"
    VERBATIM)
add_custom_target(lgaed_u11_names_fixture DEPENDS "${LGAED_U11_NAMES_BEFORE_FILE}")

# Generate complete historical/authenticated source-format examples for all
# three supported locales. These never overwrite published curricular assets.
set(LGAED_CODE_FORMAT_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/code-format")
set(LGAED_CODE_FORMAT_FIXTURES)
foreach(code_format_locale IN ITEMS es en fr)
    foreach(code_format_side IN ITEMS before after)
        list(APPEND LGAED_CODE_FORMAT_FIXTURES
            "${LGAED_CODE_FORMAT_FIXTURE_DIR}/${code_format_locale}.${code_format_side}.json")
    endforeach()
endforeach()
list(APPEND LGAED_CODE_FORMAT_FIXTURES
    "${LGAED_CODE_FORMAT_FIXTURE_DIR}/es.changed-answer.json"
    "${LGAED_CODE_FORMAT_FIXTURE_DIR}/es.forged-archive.json")
add_custom_command(OUTPUT ${LGAED_CODE_FORMAT_FIXTURES}
    COMMAND ${_lgaed_state_fixture_python} "${CMAKE_SOURCE_DIR}/scripts/curriculum/code_format_fixture_source.py"
        --output-dir "${LGAED_CODE_FORMAT_FIXTURE_DIR}"
    DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/code_format_fixture_source.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/test_code_format_compat.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/code_format_compat.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/text_semantics.py"
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/bibliography_revision.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verify source-only transformations with immutable historical origins in es/en/fr"
    VERBATIM)
add_custom_target(lgaed_code_format_fixtures DEPENDS ${LGAED_CODE_FORMAT_FIXTURES})

function(lgaed_add_state_import_tests qa_root)
    # In the isolated Windows QA mode, test the actual immutable project
    # revision files and selective restoration, not only compiler state.
    # The normal full build registers this same test separately below.
    if(LGAED_STATE_IMPORT_QA_ONLY)
        add_executable(lgaed_cpp_project_history_tests native/platform/windows/cpp_project_history_tests.cpp)
        target_link_libraries(lgaed_cpp_project_history_tests PRIVATE lgaed_platform_windows)
        add_test(NAME native.cpp_project_history COMMAND lgaed_cpp_project_history_tests
            "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history")
        set_tests_properties(native.cpp_project_history PROPERTIES TIMEOUT 40)
        add_executable(lgaed_cpp_history_training_tests native/platform/windows/cpp_history_training_tests.cpp)
        target_link_libraries(lgaed_cpp_history_training_tests PRIVATE lgaed_platform_windows)
        add_test(NAME native.cpp_history_training COMMAND lgaed_cpp_history_training_tests)
        set_tests_properties(native.cpp_history_training PROPERTIES TIMEOUT 40)
    endif()
    # Real Windows parsing, persisted answers/drafts and three-locale state
    # mapping must survive physically reformatted code while rejecting forgeries.
    add_executable(lgaed_code_format_loader_tests
        native/platform/windows/code_format_loader_tests.cpp)
    target_link_libraries(lgaed_code_format_loader_tests PRIVATE
        lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_code_format_loader_tests lgaed_code_format_fixtures)
    add_test(NAME native.code_format_loader COMMAND lgaed_code_format_loader_tests
        "${LGAED_CODE_FORMAT_FIXTURE_DIR}")
    set_tests_properties(native.code_format_loader PROPERTIES TIMEOUT 45)
    # The real Windows loader/evaluator must reject wrong stderr bytes while
    # retaining previous stdout-only and nonzero-exit completion identities.
    add_executable(lgaed_stderr_exact_tests native/core/stderr_exact_contract_tests.cpp)
    target_link_libraries(lgaed_stderr_exact_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_test(NAME native.stderr_exact_contract COMMAND lgaed_stderr_exact_tests)
    set_tests_properties(native.stderr_exact_contract PROPERTIES TIMEOUT 45)
    add_executable(lgaed_state_tests native/core/lesson_state_tests.cpp)
    target_link_libraries(lgaed_state_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    target_compile_definitions(lgaed_state_tests PRIVATE LGAED_WORK_EXACT_EDITORIAL_DIR="${LGAED_WORK_EXACT_EDITORIAL_DIR}")
    add_dependencies(lgaed_state_tests lgaed_work_exact_editorial_fixtures)
    add_dependencies(lgaed_state_tests lgaed_u11_assignment_published_fixtures)
    add_dependencies(lgaed_state_tests lgaed_u11_remainder_published_fixtures)
    add_dependencies(lgaed_state_tests lgaed_compiler_import_published_fixtures)
    add_dependencies(lgaed_state_tests lgaed_u11_names_fixture)
    target_compile_definitions(lgaed_state_tests PRIVATE LGAED_U11_NAMES_BEFORE_FILE="${LGAED_U11_NAMES_BEFORE_FILE}")
    foreach(remainder_publication IN ITEMS 26 30)
        add_test(NAME native.remainder_published${remainder_publication}_state_import
            COMMAND lgaed_state_tests "${qa_root}" --remainder-published
                "${LGAED_U11_REMAINDER_PUBLISHED_DIR}/before.json"
                "${LGAED_U11_REMAINDER_PUBLISHED_DIR}/published${remainder_publication}.json"
                "${remainder_publication}")
        set_tests_properties(native.remainder_published${remainder_publication}_state_import PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    endforeach()
    add_test(NAME native.u11_assignment_state_import COMMAND lgaed_state_tests "${qa_root}"
        --u11-assignment-editorial "${LGAED_U11_ASSIGNMENT_PUBLISHED_DIR}/assignment-before.json"
        "${LGAED_U11_ASSIGNMENT_PUBLISHED_DIR}/assignment-after.json")
    set_tests_properties(native.u11_assignment_state_import PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.compiler_contract_state_import COMMAND lgaed_state_tests "${qa_root}"
        --compiler-contract-import "${LGAED_COMPILER_IMPORT_PUBLISHED_DIR}")
    set_tests_properties(native.compiler_contract_state_import PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
endfunction()
