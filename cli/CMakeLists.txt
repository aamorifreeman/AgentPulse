# AgentPulse CLI client (apctl)

add_executable(apctl src/main.cpp)

target_link_libraries(apctl PRIVATE nlohmann_json::nlohmann_json)

install(TARGETS apctl RUNTIME DESTINATION bin)
