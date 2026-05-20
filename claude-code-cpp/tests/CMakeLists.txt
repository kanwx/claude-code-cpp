cmake_minimum_required(VERSION 3.20)

find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# Core tests
add_executable(test_tool test_tool.cpp)
target_link_libraries(test_tool PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_permission test_permission.cpp)
target_link_libraries(test_permission PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_agent_loop test_agent_loop.cpp)
target_link_libraries(test_agent_loop PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_compact test_compact.cpp)
target_link_libraries(test_compact PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_claude_md test_claude_md.cpp)
target_link_libraries(test_claude_md PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_task_manager test_task_manager.cpp)
target_link_libraries(test_task_manager PRIVATE claude_core Catch2::Catch2WithMain)

# New comprehensive tests
add_executable(test_sse_parser test_sse_parser.cpp)
target_link_libraries(test_sse_parser PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_api_client test_api_client.cpp)
target_link_libraries(test_api_client PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_tools_comprehensive test_tools_comprehensive.cpp)
target_link_libraries(test_tools_comprehensive PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_integration test_integration.cpp)
target_link_libraries(test_integration PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_commands test_commands.cpp)
target_link_libraries(test_commands PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_performance test_performance.cpp)
target_link_libraries(test_performance PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_e2e test_e2e.cpp)
target_link_libraries(test_e2e PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_bridge test_bridge.cpp)
target_link_libraries(test_bridge PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_beta_headers test_beta_headers.cpp)
target_link_libraries(test_beta_headers PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_agent_loop_recovery test_agent_loop_recovery.cpp)
target_link_libraries(test_agent_loop_recovery PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_autocompact_circuit test_autocompact_circuit.cpp)
target_link_libraries(test_autocompact_circuit PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_retryable_client test_retryable_client.cpp)
target_link_libraries(test_retryable_client PRIVATE claude_core Catch2::Catch2WithMain)

# 添加测试
include(Catch)
catch_discover_tests(test_tool)
catch_discover_tests(test_permission)
catch_discover_tests(test_agent_loop)
catch_discover_tests(test_compact)
catch_discover_tests(test_claude_md)
catch_discover_tests(test_task_manager)
catch_discover_tests(test_sse_parser)
catch_discover_tests(test_api_client)
catch_discover_tests(test_tools_comprehensive)
catch_discover_tests(test_integration)
catch_discover_tests(test_commands)
catch_discover_tests(test_performance)
catch_discover_tests(test_e2e)
catch_discover_tests(test_bridge)
catch_discover_tests(test_beta_headers)
catch_discover_tests(test_agent_loop_recovery)
catch_discover_tests(test_autocompact_circuit)
catch_discover_tests(test_retryable_client)
