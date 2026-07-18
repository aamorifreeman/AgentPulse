# AgentPulse daemon (agentpulsed)

find_package(SQLite3 REQUIRED)

# Core logic as a static library so both the daemon and the tests can link it.
add_library(agentpulse_core STATIC
    src/log.cpp
    src/paths.cpp
    src/db.cpp
    src/metrics/cpu_sampler.cpp
    src/ipc/socket_server.cpp
    src/ipc/api.cpp
)

target_include_directories(agentpulse_core PUBLIC src)

target_link_libraries(agentpulse_core PUBLIC
    SQLite3::SQLite3
    nlohmann_json::nlohmann_json
)

target_compile_definitions(agentpulse_core PUBLIC
    AGENTPULSE_VERSION="${PROJECT_VERSION}"
)

add_executable(agentpulsed src/main.cpp)
target_link_libraries(agentpulsed PRIVATE agentpulse_core)

install(TARGETS agentpulsed RUNTIME DESTINATION bin)
