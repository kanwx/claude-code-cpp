cmake_minimum_required(VERSION 3.20)

# macOS frameworks needed by httplib (CoreFoundation/Security for load_system_certs)
if(APPLE)
    set(APPLE_FRAMEWORKS "-framework CoreFoundation" "-framework Security")
else()
    set(APPLE_FRAMEWORKS "")
endif()

# 集成测试
add_executable(test_integration test_integration.cpp)
target_link_libraries(test_integration PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME IntegrationTest COMMAND test_integration)
set_tests_properties(IntegrationTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 单元测试: TripleStore
add_executable(test_triple_store test_triple_store.cpp)
target_link_libraries(test_triple_store PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME triple_store COMMAND test_triple_store)

# 单元测试: SymbolicReasoner
add_executable(test_symbolic_reasoner test_symbolic_reasoner.cpp)
target_link_libraries(test_symbolic_reasoner PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME symbolic_reasoner COMMAND test_symbolic_reasoner)

# 单元测试: NeuralReasoner
add_executable(test_neural_reasoner test_neural_reasoner.cpp)
target_link_libraries(test_neural_reasoner PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME neural_reasoner COMMAND test_neural_reasoner)

# 单元测试: HybridReasoner
add_executable(test_hybrid_reasoner test_hybrid_reasoner.cpp)
target_link_libraries(test_hybrid_reasoner PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME hybrid_reasoner COMMAND test_hybrid_reasoner)

# 单元测试: Config
add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME config COMMAND test_config)
