# Cross-compiling the Windows (x86-64) build with mingw-w64, e.g. from macOS
# (brew install mingw-w64) or Linux:
#
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
#         -DCMAKE_BUILD_TYPE=Release -DSDL2_DIR=<SDL2-x.y.z>/x86_64-w64-mingw32/lib/cmake/SDL2
#
# SDL2_DIR points at the official SDL2 "devel mingw" package. The result,
# build-win/covert-reaction.exe, needs SDL2.dll from that package next to it.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${MINGW_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_PREFIX}-g++)
set(CMAKE_RC_COMPILER ${MINGW_PREFIX}-windres)

# Look for libraries and headers in the target environment only, programs on the host.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# No libgcc / libstdc++ / winpthread DLLs to ship: link them statically.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++ -static")
