# Native application, tools and tests use headers rather than C++ modules.
# Keep ordinary compiler header dependency tracking while avoiding unnecessary
# module scanning and dyndep edges in the native build.
# This is a directory variable, not a cache or toolchain setting: independently
# configured learner projects retain their own module-scanning policy.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
