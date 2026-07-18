# AgentPulse daemon (agentpulsed)

find_package(SQLite3 REQUIRED)

add_executable(agentpulsed
    src/main.cpp
    src/log.cpp
    src/paths.cpp
)

target_include_directories(agentpulsed PRIVATE src)

target_link_libraries(agentpulsed PRIVATE SQLite3::SQLite3)

# Version string available to the source.
target_compile_definitions(agentpulsed PRIVATE
    AGENTPULSE_VERSION="${PROJECT_VERSION}"
)

install(TARGETS agentpulsed RUNTIME DESTINATION bin)
