cmake_minimum_required(VERSION 3.25)

project(hyprthanos VERSION 0.1.0 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET glesv2)

set(HYPRTHANOS_MIN_HYPRLAND_VERSION "0.56.2")
set(HYPRTHANOS_MIN_HYPRLAND_COMMITS 7661)

if(HYPRLAND_VERSION VERSION_LESS HYPRTHANOS_MIN_HYPRLAND_VERSION)
    find_file(HYPRLAND_VERSION_HEADER
        NAMES version.h
        HINTS ${HYPRLAND_INCLUDE_DIRS}
        PATH_SUFFIXES src
        NO_DEFAULT_PATH
        NO_CACHE
    )

    if(HYPRLAND_VERSION_HEADER)
        file(STRINGS "${HYPRLAND_VERSION_HEADER}" HYPRLAND_COMMITS_DEFINITION
            REGEX "^#define[ \t]+GIT_COMMITS[ \t]+\"[0-9]+\""
        )
        string(REGEX MATCH "\"([0-9]+)\"" _ "${HYPRLAND_COMMITS_DEFINITION}")
        set(HYPRLAND_COMMITS "${CMAKE_MATCH_1}")
    endif()

    if(NOT HYPRLAND_COMMITS OR HYPRLAND_COMMITS LESS HYPRTHANOS_MIN_HYPRLAND_COMMITS)
        message(FATAL_ERROR
            "hyprthanos requires Hyprland ${HYPRTHANOS_MIN_HYPRLAND_VERSION} or a newer revision "
            "(commit count >= ${HYPRTHANOS_MIN_HYPRLAND_COMMITS}); found pkg-config version ${HYPRLAND_VERSION}"
        )
    endif()
endif()

add_library(hyprthanos SHARED
    src/main.cpp
    src/fadeout_hook.cpp
    src/dust_pass.cpp
    src/dust_shader.cpp
)

set_target_properties(hyprthanos PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    PREFIX "lib"
    OUTPUT_NAME "hyprthanos"
)

target_link_libraries(hyprthanos PRIVATE
    PkgConfig::HYPRLAND
    PkgConfig::GLESV2
)

target_compile_options(hyprthanos PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(hyprthanos PRIVATE -fno-gnu-unique)
endif()

target_compile_definitions(hyprthanos PRIVATE
    HYPRTHANOS_VERSION="${PROJECT_VERSION}"
)
