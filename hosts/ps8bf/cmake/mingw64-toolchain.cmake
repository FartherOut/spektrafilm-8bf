# mingw64-toolchain.cmake — cross-compile the spektrafilm .8bf from Linux with
# mingw-w64. This is the *validation* toolchain: it produces a real Windows x64
# DLL (runs in Photoshop/Affinity) but links the mingw/UCRT runtime instead of
# the MSVC static CRT. The shipping artifact (imports kernel32/user32 only,
# /MT) is still built with MSVC on Windows — see CMakePresets.json "msvc".
#
# Use directly:
#   cmake -S . -B build/x -DCMAKE_TOOLCHAIN_FILE=cmake/mingw64-toolchain.cmake
# or via the preset:
#   cmake --preset mingw64 && cmake --build --preset mingw64
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# Look for libraries/headers in the mingw sysroot, but run host programs
# (python3) from the host.
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
