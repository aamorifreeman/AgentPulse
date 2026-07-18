# AgentPulse tests

add_executable(agentpulse_tests test_main.cpp)
target_link_libraries(agentpulse_tests PRIVATE agentpulse_core)

add_test(NAME agentpulse_tests COMMAND agentpulse_tests)
