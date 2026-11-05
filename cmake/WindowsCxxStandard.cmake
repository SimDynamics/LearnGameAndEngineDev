# Shared host/try_compile policy. MSVC 19.51 accepts this C++23 spelling;
# CMake 4.4 otherwise maps standard 23 to c++latest (a later draft).
file(READ "${CMAKE_CURRENT_LIST_DIR}/../toolchains/windows.versions.json" _lgaed_cpp_manifest)
string(JSON LGAED_CPP_STANDARD GET "${_lgaed_cpp_manifest}" cpp standard)
string(JSON LGAED_CPP_STANDARD_OPTION GET "${_lgaed_cpp_manifest}" cpp msvcStandardOption)
if(NOT LGAED_CPP_STANDARD EQUAL 23 OR
   NOT LGAED_CPP_STANDARD_OPTION MATCHES "^/std:c\\+\\+23(preview)?$")
    message(FATAL_ERROR "The Windows language profile must explicitly select C++23.")
endif()
set(CMAKE_CXX_STANDARD "${LGAED_CPP_STANDARD}")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "${LGAED_CPP_STANDARD_OPTION}")
set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "${LGAED_CPP_STANDARD_OPTION}")
