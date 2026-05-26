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

# SPARQL tests
add_executable(test_sparql_lexer test_sparql_lexer.cpp)
target_link_libraries(test_sparql_lexer PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME sparql_lexer COMMAND test_sparql_lexer)

add_executable(test_sparql_parser test_sparql_parser.cpp)
target_link_libraries(test_sparql_parser PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME sparql_parser COMMAND test_sparql_parser)

add_executable(test_sparql_executor test_sparql_executor.cpp)
target_link_libraries(test_sparql_executor PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME sparql_executor COMMAND test_sparql_executor)

# SWRL tests
add_executable(test_swrl_forward test_swrl_forward.cpp)
target_link_libraries(test_swrl_forward PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME swrl_forward COMMAND test_swrl_forward)

add_executable(test_swrl_backward test_swrl_backward.cpp)
target_link_libraries(test_swrl_backward PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME swrl_backward COMMAND test_swrl_backward)

# SHACL tests
add_executable(test_shacl test_shacl.cpp)
target_link_libraries(test_shacl PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME shacl COMMAND test_shacl)

# Inference integration test
add_executable(test_inference_integration2 test_inference_integration.cpp)
target_link_libraries(test_inference_integration2 PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME inference_integration COMMAND test_inference_integration2)

# Unit test: ClassExpression TBox-aware reasoning
add_executable(test_class_expression test_class_expression.cpp)
target_link_libraries(test_class_expression PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME class_expression COMMAND test_class_expression)

# 单元测试: SPARQL named graph operations
add_executable(test_sparql_named_graphs test_sparql_named_graphs.cpp)
target_link_libraries(test_sparql_named_graphs PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME sparql_named_graphs COMMAND test_sparql_named_graphs)

# 单元测试: AutoModel
add_executable(test_auto_model test_auto_model.cpp)
target_link_libraries(test_auto_model PRIVATE ontology_core curl ${APPLE_FRAMEWORKS})
add_test(NAME auto_model COMMAND test_auto_model)

# Unit test: ALC Description Logic Reasoner
add_executable(test_alc_reasoner test_alc_reasoner.cpp)
target_link_libraries(test_alc_reasoner PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME alc_reasoner COMMAND test_alc_reasoner)

# Unit test: IO (JSON-LD + OWL/XML writers)
add_executable(test_io test_io.cpp)
target_link_libraries(test_io PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME io COMMAND test_io)
