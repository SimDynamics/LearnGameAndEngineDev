cmake_minimum_required(VERSION 3.22.1)

project("app")

# Small independent JNI boundary: narration never loads the graphics/WASM library for affinity.
add_library(lgp_narration_cpu SHARED narration-cpu-jni.cpp)

# Only one WebAssembly runtime is linked into libapp.so per APK build.
# Both WAMR and Wasmtime implement wasm-c-api symbol names, which must
# never be interposed across different opaque engine/config object types.
option(LGP_ENABLE_WASMTIME "Enable native Wasmtime WebAssembly runtime" OFF)

# The compiler prototype currently ships an Android ARM64 Clang toolchain.
if (NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "The C++/WASM prototype currently supports arm64-v8a only")
endif ()

set(WAMR_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/wasm-micro-runtime")
set(WAMR_BUILD_PLATFORM "android")
set(WAMR_BUILD_TARGET "AARCH64")
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 1)
set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_LIB_PTHREAD 0)
set(WAMR_BUILD_LIB_WASI_THREADS 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_REF_TYPES 1)
# Android apps share process signal handlers with ART and other native libraries.
# Software bounds checks avoid installing WAMR's process-wide SIGSEGV handler.
set(WAMR_DISABLE_HW_BOUND_CHECK 1)

if (NOT LGP_ENABLE_WASMTIME)
    include("${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")
    add_library(wamr_runtime STATIC ${WAMR_RUNTIME_LIB_SOURCE})
endif ()


# The Android NDK ships glslc. Compile GLSL to SPIR-V C initializers while
# building the native library, so the APK does not require a shader compiler.
file(GLOB LGP_SHADER_TOOL_DIRS "${ANDROID_NDK}/shader-tools/*")
find_program(LGP_GLSLC
        NAMES glslc glslc.exe
        PATHS ${LGP_SHADER_TOOL_DIRS}
        NO_DEFAULT_PATH)
if (NOT LGP_GLSLC)
    message(FATAL_ERROR "glslc not found in Android NDK shader-tools; install the NDK shader tools")
endif ()

set(LGP_GENERATED_SHADERS "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
file(MAKE_DIRECTORY "${LGP_GENERATED_SHADERS}")

foreach (LGP_SHADER_STAGE IN ITEMS vert frag)
    set(LGP_SHADER_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/physics.${LGP_SHADER_STAGE}")
    set(LGP_SHADER_OUTPUT
            "${LGP_GENERATED_SHADERS}/physics.${LGP_SHADER_STAGE}.inc")
    add_custom_command(
            OUTPUT "${LGP_SHADER_OUTPUT}"
            COMMAND "${LGP_GLSLC}" -c -mfmt=c
                    "${LGP_SHADER_SOURCE}" -o "${LGP_SHADER_OUTPUT}"
            DEPENDS "${LGP_SHADER_SOURCE}"
            VERBATIM)
    list(APPEND LGP_GENERATED_SHADER_FILES "${LGP_SHADER_OUTPUT}")
endforeach ()

add_custom_target(lgp_vulkan_shaders DEPENDS ${LGP_GENERATED_SHADER_FILES})

if (LGP_ENABLE_WASMTIME)
    set(LGP_RUNTIME_JNI "wasmtime-runtime-jni.cpp")
else ()
    set(LGP_RUNTIME_JNI "wasi-runtime-jni.cpp")
endif ()

add_library(${CMAKE_PROJECT_NAME} SHARED
        native-lib.cpp
        ${LGP_RUNTIME_JNI}
        graphics-renderer.cpp)

add_dependencies(${CMAKE_PROJECT_NAME} lgp_vulkan_shaders)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${LGP_GENERATED_SHADERS}")

add_executable(lgp_wasm_ld wasm-lld-wrapper.cpp)
set_target_properties(lgp_wasm_ld PROPERTIES
        OUTPUT_NAME "liblgp_wasm_ld"
        SUFFIX ".so")
target_link_libraries(lgp_wasm_ld dl)

if (NOT LGP_ENABLE_WASMTIME)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
            "${WAMR_ROOT_DIR}/core/iwasm/include")
endif ()

find_library(LGP_VULKAN_LIBRARY vulkan REQUIRED)
find_library(LGP_EGL_LIBRARY EGL REQUIRED)
find_library(LGP_GLES_LIBRARY GLESv3 REQUIRED)

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log
        ${LGP_VULKAN_LIBRARY}
        ${LGP_EGL_LIBRARY}
        ${LGP_GLES_LIBRARY})
if (NOT LGP_ENABLE_WASMTIME)
    target_link_libraries(${CMAKE_PROJECT_NAME} wamr_runtime)
endif ()

# Wasmtime 49.0.2 Android ARM64 C API, separate from WAMR.
# Android Gradle builds enable Wasmtime by default. Use -Plgp.clang23=false
# for the complete Clang 21 + WAMR fallback build.
if (LGP_ENABLE_WASMTIME)
    set(LGP_WASMTIME_SHARED "${LGP_WASMTIME_ROOT}/lib/libwasmtime.so")
    set(LGP_WASMTIME_HEADER "${LGP_WASMTIME_ROOT}/include/wasmtime.h")
    if (NOT EXISTS "${LGP_WASMTIME_SHARED}" OR NOT EXISTS "${LGP_WASMTIME_HEADER}")
        message(FATAL_ERROR "Wasmtime 49 ARM64 headers/library missing. Run prepareWasmtimeRuntime.")
    endif ()

    # The official prebuilt does not provide a usable ELF SONAME.
    # Linking it by absolute host path would write that Windows path
    # into libapp.so's DT_NEEDED and crash Android's dynamic loader.
    # -L + -lwasmtime forces DT_NEEDED to contain "libwasmtime.so".
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
            "${LGP_WASMTIME_ROOT}/include")
    target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
            "${LGP_WASMTIME_ROOT}/lib")
    target_link_libraries(${CMAKE_PROJECT_NAME}
            wasmtime
            dl)
endif ()
