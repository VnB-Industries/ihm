set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(RPI_TRIPLE "aarch64-linux-gnu" CACHE STRING "Cross compiler target triple")
set(RPI_TOOLCHAIN_PREFIX "${RPI_TRIPLE}" CACHE STRING "Cross compiler prefix")

if(NOT DEFINED CMAKE_SYSROOT AND DEFINED ENV{RPI_SYSROOT})
    set(CMAKE_SYSROOT "$ENV{RPI_SYSROOT}" CACHE PATH "Raspberry Pi sysroot")
endif()

if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER "${RPI_TOOLCHAIN_PREFIX}-gcc")
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "${RPI_TOOLCHAIN_PREFIX}-g++")
endif()

# Force C11 standard to avoid GCC 13+ emitting __isoc23_strtol (GLIBC_2.38+)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The cross g++ toolchain still prefers /usr/aarch64-linux-gnu/include/stdlib.h
# (glibc 2.39) over the sysroot headers, which emits __isoc23_strtol and pulls
# in a GLIBC_2.38 runtime dependency. Prepend the sysroot glibc headers for C++
# so ThorVG/Lottie code resolves against the Pi sysroot's older libc headers.
if(CMAKE_SYSROOT)
    set(CMAKE_CXX_FLAGS_INIT
        "-isystem ${CMAKE_SYSROOT}/usr/include/aarch64-linux-gnu -isystem ${CMAKE_SYSROOT}/usr/include")
endif()

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Force sysroot libraries to be linked instead of toolchain's (which have GLIBC_2.38)
if(CMAKE_SYSROOT)
    link_directories(${CMAKE_SYSROOT}/lib/aarch64-linux-gnu ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu)
endif()

if(CMAKE_SYSROOT)
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")

    if(NOT DEFINED RPI_PKG_CONFIG_LIBDIR)
        set(RPI_PKG_CONFIG_LIBDIR
            "${CMAKE_SYSROOT}/usr/lib/${RPI_TRIPLE}/pkgconfig:${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig"
            CACHE STRING "pkg-config search path inside the Pi sysroot")
    endif()

    set(ENV{PKG_CONFIG_LIBDIR} "${RPI_PKG_CONFIG_LIBDIR}")
endif()