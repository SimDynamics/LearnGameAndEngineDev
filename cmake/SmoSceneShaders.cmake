include_guard(GLOBAL)

# Each experiment has its own source identity. Changing a rules experiment
# must not silently substitute stale SPIR-V or alter the FP32 scene's identity.
function(lgaed_compile_smo_family target compiler family definition entry count macro)
    cmake_parse_arguments(_shader "QA_LAST_STAGE" "" "" ${ARGN})
    set(_source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../native/presentation/windows")
    set(_parts ${_shader_UNPARSED_ARGUMENTS})
    set(_files "${_source}/smo_shader_glsl.inc")
    foreach(_part IN LISTS _parts)
        list(APPEND _files "${_source}/${_part}")
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_files})
    set(_body "")
    foreach(_file IN LISTS _files)
        file(READ "${_file}" _text)
        string(REPLACE "\r\n" "\n" _text "${_text}")
        string(APPEND _body "${_text}")
    endforeach()
    set(_root "${CMAKE_CURRENT_BINARY_DIR}/generated/${family}")
    file(MAKE_DIRECTORY "${_root}/shaders")
    string(SHA256 _hash "${_body}")
    file(GENERATE OUTPUT "${_root}/source.sha256" CONTENT "${_hash}\n" NEWLINE_STYLE UNIX)
    set(_outputs)
    set(_reference_outputs)
    math(EXPR _last "${count}-1")
    foreach(_index RANGE 0 ${_last})
        set(_name "${entry}${_index}")
        set(_comp "${_root}/shaders/${_name}.comp")
        set(_spv "${_root}/shaders/${_name}.spv")
        file(GENERATE OUTPUT "${_comp}"
            CONTENT "#version 450\n#define SMO_VULKAN 1\n#define ${definition} ${_index}\n${_body}"
            NEWLINE_STYLE UNIX)
        add_custom_command(OUTPUT "${_spv}"
            COMMAND "${compiler}" -O --target-env=vulkan1.1 -fshader-stage=compute "${_comp}" -o "${_spv}"
            DEPENDS "${_comp}" "${compiler}"
            COMMENT "Compiling ${family} ${_name}" VERBATIM)
        if(_shader_QA_LAST_STAGE AND _index EQUAL _last)
            list(APPEND _reference_outputs "${_spv}")
        else()
            list(APPEND _outputs "${_spv}")
        endif()
    endforeach()
    add_custom_target("${family}_spirv" DEPENDS ${_outputs})
    if(_shader_QA_LAST_STAGE)
        add_custom_target("${family}_spirv_reference" DEPENDS ${_reference_outputs})
    endif()
    add_dependencies("${target}" "${family}_spirv")
    file(TO_CMAKE_PATH "${_root}" _root_definition)
    target_compile_definitions("${target}" PRIVATE "${macro}=\"${_root_definition}\"")
endfunction()

# Prepare the expensive integer HLSL once at author/build time. This family is
# independent of glslc/Vulkan availability. Existing families retain both
# profiles; SHARED_CS50 explicitly opts a new family into the same validated
# legacy compute bytecode on Direct3D11 and Direct3D12.
function(lgaed_compile_smo_hlsl_family target family definition entry count macro)
    cmake_parse_arguments(_shader "SHARED_CS50;QA_LAST_STAGE" "" "" ${ARGN})
    set(_source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../native/presentation/windows")
    set(_files "${_source}/smo_shader_hlsl.inc")
    foreach(_part IN LISTS _shader_UNPARSED_ARGUMENTS)
        list(APPEND _files "${_source}/${_part}")
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_files})
    set(_body "")
    foreach(_file IN LISTS _files)
        file(READ "${_file}" _text)
        string(REPLACE "\r\n" "\n" _text "${_text}")
        string(APPEND _body "${_text}")
    endforeach()
    set(_root "${CMAKE_CURRENT_BINARY_DIR}/generated/${family}_hlsl")
    file(MAKE_DIRECTORY "${_root}/shaders")
    string(SHA256 _hash "${_body}")
    file(GENERATE OUTPUT "${_root}/fullsource.sha256" CONTENT "${_hash}\n" NEWLINE_STYLE UNIX)
    math(EXPR _last "${count}-1")
    set(_outputs)
    set(_reference_outputs)
    set(_profiles cs_5_0 cs_5_1)
    if(_shader_SHARED_CS50)
        set(_profiles cs_5_0)
    endif()
    foreach(_index RANGE 0 ${_last})
        set(_name "${entry}${_index}")
        set(_hlsl "${_root}/shaders/${_name}.hlsl")
        file(GENERATE OUTPUT "${_hlsl}" CONTENT "#define ${definition} ${_index}\n${_body}" NEWLINE_STYLE UNIX)
        foreach(_profile IN LISTS _profiles)
            set(_cso "${_root}/shaders/${_name}.${_profile}.cso")
            set(_manifest "${_root}/shaders/${_name}.${_profile}.fullsource.sha256")
            add_custom_command(OUTPUT "${_cso}" "${_manifest}"
                COMMAND "$<TARGET_FILE:lgaed_smo_shader_compile>" "${_hlsl}" "${_cso}" "${_profile}" "${_manifest}"
                DEPENDS "${_hlsl}" lgaed_smo_shader_compile
                JOB_POOL smo_hlsl_compile
                COMMENT "Compiling ${family} ${_name} ${_profile} (strict IEEE/O3)" VERBATIM)
            if(_shader_QA_LAST_STAGE AND _index EQUAL _last)
                list(APPEND _reference_outputs "${_cso}" "${_manifest}")
            else()
                list(APPEND _outputs "${_cso}" "${_manifest}")
            endif()
        endforeach()
    endforeach()
    add_custom_target("${family}_dxbc" DEPENDS ${_outputs})
    if(_shader_QA_LAST_STAGE)
        add_custom_target("${family}_dxbc_reference" DEPENDS ${_reference_outputs})
    endif()
    add_dependencies("${target}" "${family}_dxbc")
    file(TO_CMAKE_PATH "${_root}" _root_definition)
    target_compile_definitions("${target}" PRIVATE "${macro}=\"${_root_definition}\"")
endfunction()

# Reuse an already installed Vulkan SDK or Android NDK. Only its Vulkan headers
# enter the build cache; the NDK's Bionic sysroot must never enter MSVC's search
# path. This helper does not install/download SDKs or change their contents.
function(lgaed_prepare_smo_shaders target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "SMO shader preparation requires an existing target: ${target}")
    endif()
    get_property(_prepared TARGET "${target}" PROPERTY LGAED_SMO_SHADERS_PREPARED)
    if(_prepared)
        return()
    endif()
    set_property(TARGET "${target}" PROPERTY LGAED_SMO_SHADERS_PREPARED TRUE)

    lgaed_compile_smo_hlsl_family("${target}" smo_d12 D12_STAGE d12_stage 5 LGAED_SMO_D12_HLSL_SHADER_ROOT
        smo_duodecimal.inc smo_duodecimal_scene.inc)
    lgaed_compile_smo_hlsl_family("${target}" smo_d12_control D12_STAGE f12_stage 5 LGAED_SMO_D12_CONTROL_HLSL_SHADER_ROOT
        smo_duodecimal_control.inc smo_duodecimal_scene.inc)
    lgaed_compile_smo_hlsl_family("${target}" smo_float12 D12_STAGE float12_stage 5 LGAED_SMO_FLOAT12_HLSL_SHADER_ROOT
        SHARED_CS50 smo_float12.inc smo_duodecimal_scene.inc)
    lgaed_compile_smo_hlsl_family("${target}" smo_float12_rules D12_ONLY_OPERATION float12_rules 11 LGAED_SMO_FLOAT12_RULE_HLSL_SHADER_ROOT
        SHARED_CS50 smo_float12.inc smo_duodecimal_rules.inc)
    lgaed_compile_smo_hlsl_family("${target}" smo_program_vm SMO_VM_OPCODE smo_vm 22 LGAED_SMO_PROGRAM_VM_HLSL_SHADER_ROOT
        SHARED_CS50 smo_program_profile.inc smo_float12.inc smo_program_vm.inc)
    lgaed_compile_smo_hlsl_family("${target}" smo_program_render SMO_RENDER_STAGE smo_render 5 LGAED_SMO_PROGRAM_RENDER_HLSL_SHADER_ROOT
        SHARED_CS50 QA_LAST_STAGE smo_float12.inc smo_program_render.inc)
    lgaed_compile_smo_hlsl_family("${target}" smo_radix_ablation SMO_RADIX_STAGE smo_radix 6 LGAED_SMO_RADIX_ABLATION_HLSL_SHADER_ROOT
        SHARED_CS50 smo_radix_ablation.inc)
    lgaed_compile_smo_hlsl_family("${target}" smo_native_render SMO_NATIVE_RENDER_STAGE native_render 5 LGAED_SMO_NATIVE_RENDER_HLSL_SHADER_ROOT
        SHARED_CS50 QA_LAST_STAGE smo_native_render.inc)

    set(_compiler_candidates)
    set(_header_candidates)
    if(NOT "$ENV{VULKAN_SDK}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" _vulkan_sdk)
        list(APPEND _compiler_candidates "${_vulkan_sdk}/Bin/glslc.exe" "${_vulkan_sdk}/bin/glslc.exe")
        list(APPEND _header_candidates "${_vulkan_sdk}/Include" "${_vulkan_sdk}/include")
    endif()

    set(_android_roots)
    foreach(_variable IN ITEMS ANDROID_HOME ANDROID_SDK_ROOT)
        if(NOT "$ENV{${_variable}}" STREQUAL "")
            file(TO_CMAKE_PATH "$ENV{${_variable}}" _android_sdk)
            list(APPEND _android_roots "${_android_sdk}")
        endif()
    endforeach()
    if(NOT "$ENV{LOCALAPPDATA}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/Android/Sdk" _android_sdk)
        list(APPEND _android_roots "${_android_sdk}")
    endif()
    list(REMOVE_DUPLICATES _android_roots)
    foreach(_sdk IN LISTS _android_roots)
        file(GLOB _ndks LIST_DIRECTORIES TRUE "${_sdk}/ndk/*")
        list(SORT _ndks COMPARE NATURAL ORDER DESCENDING)
        foreach(_ndk IN LISTS _ndks)
            if(IS_DIRECTORY "${_ndk}")
                list(APPEND _compiler_candidates "${_ndk}/shader-tools/windows-x86_64/glslc.exe")
                list(APPEND _header_candidates "${_ndk}/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include")
            endif()
        endforeach()
    endforeach()

    set(_glslc "")
    foreach(_candidate IN LISTS _compiler_candidates)
        if(EXISTS "${_candidate}" AND NOT IS_DIRECTORY "${_candidate}")
            set(_glslc "${_candidate}")
            break()
        endif()
    endforeach()
    if(NOT _glslc)
        find_program(_path_glslc NAMES glslc glslc.exe NO_CACHE)
        if(_path_glslc)
            set(_glslc "${_path_glslc}")
        endif()
    endif()
    set(_vulkan_headers "")
    foreach(_candidate IN LISTS _header_candidates)
        if(EXISTS "${_candidate}/vulkan/vulkan.h" AND IS_DIRECTORY "${_candidate}/vk_video")
            set(_vulkan_headers "${_candidate}")
            break()
        endif()
    endforeach()

    if(NOT _glslc OR NOT _vulkan_headers)
        if(NOT _glslc)
            message(STATUS "SMO Vulkan unavailable: glslc was not found in VULKAN_SDK, installed Android NDKs, or PATH.")
        endif()
        if(NOT _vulkan_headers)
            message(STATUS "SMO Vulkan unavailable: complete vulkan + vk_video headers were not found in VULKAN_SDK or installed Android NDKs.")
        endif()
        # The Vulkan factory remains compiled and reports this missing build
        # capability explicitly. Do not define LGAED_HAS_VULKAN to zero: its
        # consumers use #ifdef, and other GPU backends must remain available.
        return()
    endif()

    set(_source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../native/presentation/windows")
    set(_glsl "${_source}/smo_shader_glsl.inc")
    set(_hlsl "${_source}/smo_shader_hlsl.inc")
    set(_common "${_source}/smo_scene_common.inc")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_glsl}" "${_hlsl}" "${_common}")
    file(READ "${_glsl}" _glsl_prefix)
    file(READ "${_common}" _common_body)
    # Git's Windows CRLF policy must not create a false stale-shader mismatch.
    string(REPLACE "\r\n" "\n" _glsl_prefix "${_glsl_prefix}")
    string(REPLACE "\r\n" "\n" _common_body "${_common_body}")

    set(_root "${CMAKE_CURRENT_BINARY_DIR}/generated/smo_scene")
    set(_shader_root "${_root}/shaders")
    set(_cached_headers "${CMAKE_CURRENT_BINARY_DIR}/generated/graphics_headers")
    file(MAKE_DIRECTORY "${_shader_root}" "${_cached_headers}")
    file(COPY "${_vulkan_headers}/vulkan" "${_vulkan_headers}/vk_video" DESTINATION "${_cached_headers}")
    # Keep copied SDK headers current without exposing any sibling sysroot
    # headers. file(COPY) preserves unchanged files and their timestamps.
    file(GLOB_RECURSE _sdk_headers LIST_DIRECTORIES FALSE "${_vulkan_headers}/vulkan/*" "${_vulkan_headers}/vk_video/*")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_sdk_headers})

    # Runtime rejects stale SPIR-V if these LF-normalized source bytes change before a
    # rebuild. No separator or generated preamble enters this source identity.
    string(SHA256 _source_hash "${_glsl_prefix}${_common_body}")
    file(GENERATE OUTPUT "${_root}/source.sha256" CONTENT "${_source_hash}\n" NEWLINE_STYLE UNIX)

    set(_outputs)
    foreach(_stage RANGE 0 5)
        foreach(_mode RANGE 0 2)
            set(_comp "${_shader_root}/stage${_stage}_mode${_mode}.comp")
            set(_spv "${_shader_root}/stage${_stage}_mode${_mode}.spv")
            # GENERATE updates a source only when its content changes. An
            # unrelated configure therefore does not recompile 18 shaders.
            file(GENERATE OUTPUT "${_comp}"
                CONTENT "#version 450\n#define SMO_VULKAN 1\n#define STAGE ${_stage}\n#define MODE ${_mode}\n${_glsl_prefix}\n${_common_body}\n"
                NEWLINE_STYLE UNIX)
            add_custom_command(OUTPUT "${_spv}"
                COMMAND "${_glslc}" -O --target-env=vulkan1.1 -fshader-stage=compute "${_comp}" -o "${_spv}"
                DEPENDS "${_comp}" "${_glslc}"
                COMMENT "Compiling SMO Vulkan stage ${_stage}, mode ${_mode}"
                VERBATIM)
            list(APPEND _outputs "${_spv}")
        endforeach()
    endforeach()
    add_custom_target(smo_scene_spirv DEPENDS ${_outputs})
    add_dependencies("${target}" smo_scene_spirv)
    target_include_directories("${target}" PRIVATE "${_cached_headers}")
    file(TO_CMAKE_PATH "${_root}" _root_definition)
    target_compile_definitions("${target}" PRIVATE LGAED_HAS_VULKAN=1 "LGAED_SMO_SHADER_ROOT=\"${_root_definition}\"")
    # Isolated QA families reuse the selected installed compiler without
    # preparing their extra shaders as dependencies of the application.
    set_property(TARGET "${target}" PROPERTY LGAED_SMO_GLSLC "${_glslc}")
    message(STATUS "SMO Vulkan: ${_glslc}; Vulkan headers cached from ${_vulkan_headers}; 18 shared-body SPIR-V shaders.")
    lgaed_compile_smo_family("${target}" "${_glslc}" smo_rules MODE rule_mode 3 LGAED_SMO_RULE_SHADER_ROOT
        smo_arithmetic.inc smo_rules_common.inc)
    lgaed_compile_smo_family("${target}" "${_glslc}" smo_d12 D12_STAGE d12_stage 5 LGAED_SMO_D12_SHADER_ROOT
        smo_duodecimal.inc smo_duodecimal_scene.inc)
    lgaed_compile_smo_family("${target}" "${_glslc}" smo_d12_control D12_STAGE f12_stage 5 LGAED_SMO_D12_CONTROL_SHADER_ROOT
        smo_duodecimal_control.inc smo_duodecimal_scene.inc)
    lgaed_compile_smo_family("${target}" "${_glslc}" smo_d12_rules D12_ONLY_OPERATION d12_rules 11 LGAED_SMO_D12_RULE_SHADER_ROOT
        smo_duodecimal.inc smo_duodecimal_rules.inc)
    lgaed_compile_smo_family("${target}" "${_glslc}" smo_float12 D12_STAGE float12_stage 5 LGAED_SMO_FLOAT12_SHADER_ROOT
        smo_float12.inc smo_duodecimal_scene.inc)
    lgaed_compile_smo_family("${target}" "${_glslc}" smo_float12_rules D12_ONLY_OPERATION float12_rules 11 LGAED_SMO_FLOAT12_RULE_SHADER_ROOT
        smo_float12.inc smo_duodecimal_rules.inc)
    lgaed_compile_smo_family("${target}" "${_glslc}" smo_program_vm SMO_VM_OPCODE smo_vm 22 LGAED_SMO_PROGRAM_VM_SHADER_ROOT
        smo_program_profile.inc smo_float12.inc smo_program_vm.inc)
    lgaed_compile_smo_family("${target}" "${_glslc}" smo_program_render SMO_RENDER_STAGE smo_render 5 LGAED_SMO_PROGRAM_RENDER_SHADER_ROOT
        QA_LAST_STAGE smo_float12.inc smo_program_render.inc)
    lgaed_compile_smo_family("${target}" "${_glslc}" smo_radix_ablation SMO_RADIX_STAGE smo_radix 6 LGAED_SMO_RADIX_ABLATION_SHADER_ROOT
        smo_radix_ablation.inc)
    lgaed_compile_smo_family("${target}" "${_glslc}" smo_native_render SMO_NATIVE_RENDER_STAGE native_render 5 LGAED_SMO_NATIVE_RENDER_SHADER_ROOT
        QA_LAST_STAGE smo_native_render.inc)
endfunction()
