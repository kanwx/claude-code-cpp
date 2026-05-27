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

add_executable(test_retry_policy_413 test_retry_policy_413.cpp)
target_link_libraries(test_retry_policy_413 PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_redacted_thinking test_redacted_thinking.cpp)
target_link_libraries(test_redacted_thinking PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_agent_loop_abort test_agent_loop_abort.cpp)
target_link_libraries(test_agent_loop_abort PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_fallback_tombstone test_fallback_tombstone.cpp)
target_link_libraries(test_fallback_tombstone PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_apiclient_factory test_apiclient_factory.cpp)
target_link_libraries(test_apiclient_factory PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_rate_limit_tracking test_rate_limit_tracking.cpp)
target_link_libraries(test_rate_limit_tracking PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_streaming_interleave test_streaming_interleave.cpp)
target_link_libraries(test_streaming_interleave PRIVATE claude_core Catch2::Catch2WithMain)

# P6-1 Integration tests for key subsystems
add_executable(test_compaction test_compaction.cpp)
target_link_libraries(test_compaction PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_retry_policy test_retry_policy.cpp)
target_link_libraries(test_retry_policy PRIVATE claude_core Catch2::Catch2WithMain)

add_executable(test_prompt_command test_prompt_command.cpp)
target_link_libraries(test_prompt_command PRIVATE claude_core Catch2::Catch2WithMain)

# Discover tests
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
catch_discover_tests(test_retry_policy_413)
catch_discover_tests(test_redacted_thinking)
catch_discover_tests(test_agent_loop_abort)
catch_discover_tests(test_fallback_tombstone)
catch_discover_tests(test_apiclient_factory)
catch_discover_tests(test_rate_limit_tracking)
catch_discover_tests(test_streaming_interleave)
catch_discover_tests(test_retry_policy)
catch_discover_tests(test_prompt_command)
catch_discover_tests(test_compaction)

add_executable(test_tool_render test_tool_render.cpp)
target_link_libraries(test_tool_render PRIVATE claude_core Catch2::Catch2WithMain)
catch_discover_tests(test_tool_render)

add_executable(test_cost_tracker test_cost_tracker.cpp)
target_link_libraries(test_cost_tracker PRIVATE claude_core Catch2::Catch2WithMain)
catch_discover_tests(test_cost_tracker)

add_executable(test_file_read_streaming test_file_read_streaming.cpp)
target_link_libraries(test_file_read_streaming PRIVATE claude_core Catch2::Catch2WithMain)
catch_discover_tests(test_file_read_streaming)

add_executable(test_anthropic_format test_anthropic_format.cpp)
target_link_libraries(test_anthropic_format PRIVATE claude_core Catch2::Catch2WithMain)
catch_discover_tests(test_anthropic_format)

add_executable(test_context_injector test_context_injector.cpp)
target_link_libraries(test_context_injector PRIVATE claude_core Catch2::Catch2WithMain)
catch_discover_tests(test_context_injector)

add_executable(test_tool_registry test_tool_registry.cpp)
target_link_libraries(test_tool_registry PRIVATE claude_core Catch2::Catch2WithMain)
catch_discover_tests(test_tool_registry)
