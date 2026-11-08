include_guard(GLOBAL)

set(LGAED_NARRATION_RESOURCES "" CACHE PATH "Existing authenticated Kokoro resource directory")
set(LGAED_ENGLISH_RESOURCES "" CACHE PATH "Existing authenticated English auxiliary resource directory")

function(lgaed_add_narration_runtime scope)
    if(NOT scope MATCHES "^(Application|NarrationHost)$")
        message(FATAL_ERROR "Unsupported narration native-tools scope: ${scope}")
    endif()
    if(NOT LGAED_JAVA_HOME OR NOT LGAED_FFMPEG_EXECUTABLE)
        message(FATAL_ERROR "Narration runtime requires the selected JDK and FFmpeg locations.")
    endif()
    set(_arguments
        --native-tools-scope "${scope}"
        --cmake "${CMAKE_COMMAND}"
        --python "${Python3_EXECUTABLE}"
        --git "${GIT_EXECUTABLE}"
        --java-home "${LGAED_JAVA_HOME}"
        --ffmpeg "${LGAED_FFMPEG_EXECUTABLE}")
    if(LGAED_NARRATION_RESOURCES)
        list(APPEND _arguments --resources "${LGAED_NARRATION_RESOURCES}")
    endif()
    if(LGAED_ENGLISH_RESOURCES)
        list(APPEND _arguments --english-resources "${LGAED_ENGLISH_RESOURCES}")
    endif()
    add_custom_target(lgaed_narration_runtime
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/native/narration/worker/build_worker.py" ${_arguments}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Preparing the shared Kokoro and mathematical runtime"
        VERBATIM)
endfunction()
