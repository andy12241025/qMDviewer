# Cross-compile qMDviewer for 64-bit Windows using MinGW-w64 on a Unix host.
#
#   cmake -B build-win \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
#       -DCMAKE_BUILD_TYPE=Release \
#       -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.y/mingw_64 \
#       -DQT_HOST_PATH=/usr
#
# Qt 6 needs a host Qt of the same version for moc/rcc; on Debian-style hosts
# also pass -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/<arch>/cmake.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

# Qt's official MinGW builds use the POSIX threading model, which is also what
# provides std::thread in libstdc++; the win32 variant would not link.
find_program(MINGW_CXX NAMES ${TOOLCHAIN_PREFIX}-g++-posix ${TOOLCHAIN_PREFIX}-g++ REQUIRED)
find_program(MINGW_CC NAMES ${TOOLCHAIN_PREFIX}-gcc-posix ${TOOLCHAIN_PREFIX}-gcc REQUIRED)
find_program(MINGW_RC NAMES ${TOOLCHAIN_PREFIX}-windres REQUIRED)

set(CMAKE_CXX_COMPILER "${MINGW_CXX}")
set(CMAKE_C_COMPILER "${MINGW_CC}")
set(CMAKE_RC_COMPILER "${MINGW_RC}")

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Headers and libraries must come from the MinGW sysroot only. Searching the
# host prefixes as well would put /usr/include on the command line, where the
# host's glibc headers shadow MinGW's.
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# Packages are still looked up outside the sysroot so that Qt can be picked up
# from CMAKE_PREFIX_PATH.
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
