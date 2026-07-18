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

# ---- Dependencies --------------------------------------------------------
# Prefer a system/Homebrew-installed nlohmann_json; fall back to fetching the
# single-header release for machines without it (requires network at
# configure time).
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found locally; fetching release")
    include(FetchContent)
    FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(json)
endif()

# ---- Subdirectories ------------------------------------------------------
enable_testing()
add_subdirectory(daemon)
add_subdirectory(cli)
add_subdirectory(tests)
