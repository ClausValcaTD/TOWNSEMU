cmake_minimum_required(VERSION 3.20)

add_library(townsemu_libretro MODULE
    libretro_core.cpp
)

# Inherit C++17 from root
set_target_properties(townsemu_libretro PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    PREFIX ""          # output: townsemu_libretro.so (no "lib" prefix)
)

target_include_directories(townsemu_libretro PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(townsemu_libretro PRIVATE
    towns
    outside_world
    headless_mode
    townsrender
    discimg
    townsargv
)
