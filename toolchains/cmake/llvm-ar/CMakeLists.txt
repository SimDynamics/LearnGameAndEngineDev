cmake_minimum_required(VERSION 3.22)
project(LgpLlvmAr LANGUAGES CXX)

if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "llvm-ar helper must target Android ARM64")
endif()
if(NOT EXISTS "${LGP_LLVM_SOURCE}/llvm/tools/llvm-ar/llvm-ar.cpp" OR
   NOT EXISTS "${LGP_LLVM_BUILD}/include/llvm/Config/llvm-config.h")
    message(FATAL_ERROR "Verified LLVM 23 source/generated headers are required")
endif()

# Read the existing compiler build; never invoke its Ninja/build scripts or
# change its files. Only this project's object and executable outputs are new.
add_executable(lgp_llvm_ar
    "${LGP_LLVM_SOURCE}/llvm/tools/llvm-ar/llvm-ar.cpp"
    "${LGP_LLVM_BUILD}/tools/llvm-ar/llvm-ar-driver.cpp"
    "${LGP_LLVM_SOURCE}/llvm/lib/ToolDrivers/llvm-dlltool/DlltoolDriver.cpp")
target_compile_features(lgp_llvm_ar PRIVATE cxx_std_17)
target_compile_definitions(lgp_llvm_ar PRIVATE
    __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS __STDC_LIMIT_MACROS)
target_compile_options(lgp_llvm_ar PRIVATE -fno-exceptions -fno-rtti)
target_include_directories(lgp_llvm_ar PRIVATE
    "${LGP_LLVM_BUILD}/include" "${LGP_LLVM_SOURCE}/llvm/include"
    "${CMAKE_CURRENT_BINARY_DIR}/dlltool")

# Official LLVM TableGen produces the same option table as llvm-dlltool's
# upstream build. The native host TableGen binary is read-only input.
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dlltool/Options.inc"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/dlltool"
    COMMAND "${LGP_LLVM_TABLEGEN}" -gen-opt-parser-defs
        -I "${LGP_LLVM_SOURCE}/llvm/include"
        "${LGP_LLVM_SOURCE}/llvm/lib/ToolDrivers/llvm-dlltool/Options.td"
        -o "${CMAKE_CURRENT_BINARY_DIR}/dlltool/Options.inc"
    DEPENDS "${LGP_LLVM_SOURCE}/llvm/lib/ToolDrivers/llvm-dlltool/Options.td"
    VERBATIM)
target_sources(lgp_llvm_ar PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/dlltool/Options.inc")

# Same dependency set/order as upstream llvm-ar for the WebAssembly-only build.
# There is no LLVM DLL or new libc++ shared library in the APK.
foreach(component IN ITEMS
    WebAssemblyAsmParser WebAssemblyDesc WebAssemblyInfo BinaryFormat Core
    LibDriver Object Support TargetParser CodeGenTypes MCParser MC
    DebugInfoDWARFLowLevel IRReader AsmParser TextAPI Option BitReader Remarks
    BitstreamReader Demangle)
    set(library "${LGP_LLVM_BUILD}/lib/libLLVM${component}.a")
    if(NOT EXISTS "${library}")
        message(FATAL_ERROR "LLVM 23 static input is missing: ${library}")
    endif()
    target_link_libraries(lgp_llvm_ar PRIVATE "${library}")
endforeach()
target_link_libraries(lgp_llvm_ar PRIVATE atomic m)
