cmake_minimum_required(VERSION 3.15)

project(ihm LANGUAGES C CXX)

file(GLOB_RECURSE SOURCES
    src/*.c       src/*.cpp
    screens/*.c   screens/*.cpp
    widgets/*.c   widgets/*.cpp
    components/*.c components/*.cpp
)

include_directories(src)
include_directories(components)
include_directories(fonts)
include_directories(images)
include_directories(screens)
include_directories(widgets)

add_executable(${PROJECT_NAME} ${SOURCES})

if(STRIP_BINARY)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "Stripping binary (all symbols)"
)
endif()

add_definitions(-DLV_CONF_INCLUDE_SIMPLE)

include(FindPkgConfig)
pkg_check_modules(LVDRIVERS REQUIRED lv_drivers)
pkg_check_modules(LVGL REQUIRED lvgl)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${LVDRIVERS_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${LVDRIVERS_LIBRARIES})

target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${LVGL_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${LVGL_LIBRARIES})

target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${SQLITE3_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${SQLITE3_LIBRARIES})

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ihm.app)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION /root/easy/
)
