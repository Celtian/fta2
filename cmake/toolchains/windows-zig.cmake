# Cross-compile on a POSIX host using Zig's Clang and bundled Windows runtime.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
find_program(FTA2_ZIG_EXECUTABLE NAMES zig REQUIRED)
set(CMAKE_CXX_COMPILER "${FTA2_ZIG_EXECUTABLE}")
set(CMAKE_CXX_COMPILER_ARG1 "c++")
set(CMAKE_CXX_COMPILER_TARGET x86_64-windows-gnu)
set(CMAKE_CXX_FLAGS_INIT "--target=x86_64-windows-gnu")
