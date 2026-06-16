cmake_minimum_required(VERSION 3.15)

project(ihm LANGUAGES C CXX)

# Use C11 to avoid GCC 13+ emitting __isoc23_strtol@GLIBC_2.38
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Explicitly use older symbol versions to match GLIBC 2.36 available on Pi
# Prevents GCC 13+ from emitting fmodf@@GLIBC_2.38 and __isoc23_strtol@@GLIBC_2.38
add_compile_options(-Wl,--hash-style=gnu -Wl,-O1)

option(IHM_USE_LINUX_FBDEV "Build against LVGL's Linux framebuffer backend" OFF)
option(IHM_USE_LINUX_DRM "Build against LVGL's Linux DRM backend" ON)

set(IHM_LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/lvgl" CACHE PATH "Path to the LVGL source tree")
set(IHM_LV_DRIVERS_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/lv_drivers" CACHE PATH "Path to the legacy lv_drivers source tree")
set(IHM_LV_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE FILEPATH "Path to the LVGL configuration header")
set(IHM_DB_PATH "./game.db" CACHE STRING "Runtime path to the SQLite database")
set(IHM_FBDEV_DEVICE "/dev/fb0" CACHE STRING "Framebuffer device path")
set(IHM_EVDEV_DEVICE "/dev/input/event0" CACHE STRING "Input event device path")
set(IHM_DRM_DEVICE "/dev/dri/card0" CACHE STRING "DRM card device path")
set(IHM_SCREEN_WIDTH 1024 CACHE STRING "Logical UI width")
set(IHM_SCREEN_HEIGHT 600 CACHE STRING "Logical UI height")

if(IHM_USE_LINUX_FBDEV AND IHM_USE_LINUX_DRM)
    message(FATAL_ERROR "Select only one Linux display backend: fbdev or DRM.")
endif()

if(NOT IHM_USE_LINUX_FBDEV AND NOT IHM_USE_LINUX_DRM)
    message(FATAL_ERROR "At least one Linux display backend must be enabled.")
endif()

if(NOT EXISTS "${IHM_LVGL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "LVGL source tree not found at ${IHM_LVGL_DIR}")
endif()

if(NOT EXISTS "${IHM_LV_CONF_PATH}")
    message(FATAL_ERROR "LVGL config header not found at ${IHM_LV_CONF_PATH}")
endif()

if(EXISTS "${IHM_LV_DRIVERS_DIR}/README.md")
    message(STATUS "lv_drivers is vendored at ${IHM_LV_DRIVERS_DIR} but not linked for the Pi target because LVGL v9 Linux drivers are built-in.")
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    src/*.c       src/*.cpp
    screens/*.c   screens/*.cpp
    widgets/*.c   widgets/*.cpp
    components/*.c components/*.cpp
)

include(ExternalProject)

get_filename_component(IHM_LV_CONF_PATH_ABS "${IHM_LV_CONF_PATH}" ABSOLUTE)

if(CMAKE_TOOLCHAIN_FILE)
    get_filename_component(IHM_TOOLCHAIN_FILE_ABS "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE)
endif()

set(IHM_THIRD_PARTY_BUILD_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/.third_party_build"
    CACHE PATH "Directory used to cache third-party builds outside the main build tree")
set(IHM_LVGL_BUILD_DIR "${IHM_THIRD_PARTY_BUILD_ROOT}/lvgl")
set(IHM_LVGL_LIB_PATH "${IHM_LVGL_BUILD_DIR}/liblvgl.a")
set(IHM_LVGL_THORVG_LIB_PATH "${IHM_LVGL_BUILD_DIR}/lib/liblvgl_thorvg.a")

set(IHM_LVGL_CMAKE_C_FLAGS "-DIHM_LV_USE_SDL=0 -DIHM_LV_USE_EVDEV=1 -std=c11")

if(IHM_USE_LINUX_FBDEV)
    string(APPEND IHM_LVGL_CMAKE_C_FLAGS " -DIHM_LV_USE_LINUX_FBDEV=1")
elseif(IHM_USE_LINUX_DRM)
    string(APPEND IHM_LVGL_CMAKE_C_FLAGS " -DIHM_LV_USE_LINUX_DRM=1 -I/usr/include/drm")
endif()

set(IHM_LVGL_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${IHM_LVGL_BUILD_DIR}/install
    -DLV_BUILD_CONF_PATH=${IHM_LV_CONF_PATH_ABS}
    -DCONFIG_LV_BUILD_DEMOS=OFF
    -DCONFIG_LV_BUILD_EXAMPLES=OFF
    -DCONFIG_LV_USE_THORVG_INTERNAL=ON
    -DCMAKE_C_STANDARD=11
    -DCMAKE_C_STANDARD_REQUIRED=ON
    "-DCMAKE_C_FLAGS=${IHM_LVGL_CMAKE_C_FLAGS}"
    "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS_INIT}"
)

if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND IHM_LVGL_CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${IHM_TOOLCHAIN_FILE_ABS})
endif()

if(CMAKE_SYSROOT)
    list(APPEND IHM_LVGL_CMAKE_ARGS -DCMAKE_SYSROOT=${CMAKE_SYSROOT})
endif()

ExternalProject_Add(lvgl_external
    SOURCE_DIR ${IHM_LVGL_DIR}
    BINARY_DIR ${IHM_LVGL_BUILD_DIR}
    CMAKE_ARGS ${IHM_LVGL_CMAKE_ARGS}
    BUILD_BYPRODUCTS ${IHM_LVGL_LIB_PATH} ${IHM_LVGL_THORVG_LIB_PATH}
    BUILD_ALWAYS TRUE
    BUILD_JOB_SERVER_AWARE TRUE
    BUILD_COMMAND
        ${CMAKE_COMMAND} -S <SOURCE_DIR> -B <BINARY_DIR> ${IHM_LVGL_CMAKE_ARGS}
        COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR>
    INSTALL_COMMAND ""
)

add_library(lvgl STATIC IMPORTED GLOBAL)
set_target_properties(lvgl PROPERTIES
    IMPORTED_LOCATION ${IHM_LVGL_LIB_PATH}
    INTERFACE_INCLUDE_DIRECTORIES "${IHM_LVGL_DIR};${IHM_LVGL_DIR}/include;${CMAKE_CURRENT_LIST_DIR};${IHM_LVGL_BUILD_DIR}"
    INTERFACE_COMPILE_DEFINITIONS "LV_CONF_INCLUDE_SIMPLE;LV_CONF_PATH=\"${IHM_LV_CONF_PATH}\";IHM_LV_USE_SDL=0;IHM_LV_USE_LINUX_FBDEV=$<BOOL:${IHM_USE_LINUX_FBDEV}>;IHM_LV_USE_LINUX_DRM=$<BOOL:${IHM_USE_LINUX_DRM}>;IHM_LV_USE_EVDEV=1"
)

add_library(lvgl_thorvg STATIC IMPORTED GLOBAL)
set_target_properties(lvgl_thorvg PROPERTIES
    IMPORTED_LOCATION ${IHM_LVGL_THORVG_LIB_PATH}
)

# Explicit cleanup for cached third-party outputs. Normal `make clean` leaves this cache intact.
add_custom_target(clean-third-party
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${IHM_THIRD_PARTY_BUILD_ROOT}
    COMMENT "Cleaning cached third-party build artifacts"
)

include(FindPkgConfig)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

if(IHM_USE_LINUX_DRM)
    pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})
add_dependencies(${PROJECT_NAME} lvgl_external)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    components
    fonts
    images
    screens
    widgets
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_CONF_PATH=\"${IHM_LV_CONF_PATH}\"
    SCREEN_W=${IHM_SCREEN_WIDTH}
    SCREEN_H=${IHM_SCREEN_HEIGHT}
    IHM_DB_PATH=\"${IHM_DB_PATH}\"
    IHM_FBDEV_DEVICE=\"${IHM_FBDEV_DEVICE}\"
    IHM_EVDEV_DEVICE=\"${IHM_EVDEV_DEVICE}\"
    IHM_DRM_DEVICE=\"${IHM_DRM_DEVICE}\"
    IHM_LV_USE_SDL=0
    IHM_LV_USE_LINUX_FBDEV=$<BOOL:${IHM_USE_LINUX_FBDEV}>
    IHM_LV_USE_LINUX_DRM=$<BOOL:${IHM_USE_LINUX_DRM}>
    IHM_LV_USE_EVDEV=1
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    lvgl
    lvgl_thorvg
    PkgConfig::SQLITE3
)

if(IHM_USE_LINUX_DRM)
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::LIBDRM)
endif()

if(NOT MSVC)
    if(CMAKE_SYSROOT)
        target_link_options(${PROJECT_NAME} PRIVATE -nostdlib++)
        # Explicitly link sysroot libraries to avoid GLIBC_2.38 symbols from toolchain's libm
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/libstdc++.so.6"
            "${CMAKE_SYSROOT}/lib/aarch64-linux-gnu/libgcc_s.so.1"
            "${CMAKE_SYSROOT}/lib/aarch64-linux-gnu/libm.so.6"
        )
        # Link pthread from sysroot as well
        target_link_libraries(${PROJECT_NAME} PRIVATE "${CMAKE_SYSROOT}/lib/aarch64-linux-gnu/libpthread.so.0")
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE m pthread)
    endif()
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE m pthread)
endif()

if(STRIP_BINARY)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "Stripping binary (all symbols)"
)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME ihm.app
    LINKER_LANGUAGE CXX
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

