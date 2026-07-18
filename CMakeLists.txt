cmake_minimum_required(VERSION 3.20)

project(AgentPulse
    VERSION 0.1.0
    DESCRIPTION "macOS system & automation reliability monitor"
    LANGUAGES CXX)

# ---- Global settings ----------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Export compile_commands.json for editors / clangd.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic)

# ---- Subdirectories ------------------------------------------------------
add_subdirectory(daemon)
