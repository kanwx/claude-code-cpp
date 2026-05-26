cmake_minimum_required(VERSION 3.20)
project(ontology_platform VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ========== 依赖库 ==========
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(CLI11 REQUIRED)

# 使用 OpenSSL 3.0
set(OPENSSL_ROOT_DIR "/usr/local/Cellar/openssl@3/3.6.2")
find_package(OpenSSL REQUIRED)

# httplib - 使用 OpenSSL 3.0
add_library(httplib INTERFACE)
target_include_directories(httplib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
)
target_compile_definitions(httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(httplib INTERFACE OpenSSL::SSL OpenSSL::Crypto)

# 确保 OpenSSL 3.0 头文件优先
include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(BEFORE ${OPENSSL_INCLUDE_DIR})

# ========== 核心库 ==========
set(CORE_SOURCES
    src/core/OntologyCore.cpp
    src/core/TripleStore.cpp
    src/core/Temporal.cpp
)

set(OWL_SOURCES
    src/owl/ClassExpression.cpp
    src/owl/ExpressionParser.cpp
)

set(IO_SOURCES
    src/io/OntologyIO.cpp
)

set(SPARQL_SOURCES
    src/sparql/SparqlLexer.cpp
    src/sparql/SparqlAst.cpp
    src/sparql/SparqlParser.cpp
    src/sparql/SparqlExecutor.cpp
    src/sparql/SparqlEndpoint.cpp
    src/sparql/Sparql.cpp
)

set(SWRL_SOURCES
    src/swrl/Swrl.cpp
)

set(STORAGE_SOURCES
    src/storage/Neo4jClient.cpp
    src/storage/MilvusClient.cpp
    src/storage/HybridStorage.cpp
    src/storage/StellarDBClient.cpp
    src/storage/HippoClient.cpp
)

set(INFERENCE_SOURCES
    src/inference/SymbolicReasoner.cpp
    src/inference/NeuralReasoner.cpp
    src/inference/HybridReasoner.cpp
    src/inference/AutoModel.cpp
    src/inference/MissingInference.cpp
    src/inference/ShaclValidation.cpp
    src/inference/DempsterShafer.cpp
    src/inference/NeuroSymbolicLearning.cpp
    src/inference/Explainability.cpp
    src/inference/SwrlBackwardChainer.cpp
    src/inference/DlReasoner.cpp
)

set(MCP_SOURCES
    src/mcp/CognitiveMcpServer.cpp
)

set(RAG_SOURCES
    src/rag/RagPipeline.cpp
    src/rag/RagQuery.cpp
    src/rag/RagStorage.cpp
    src/rag/TextEmbedding.cpp
    src/rag/HybridRetrieval.cpp
    src/rag/QueryTransform.cpp
    src/rag/Reranker.cpp
    src/rag/SemanticChunker.cpp
    src/rag/CommunityDetection.cpp
    src/rag/EmbeddingService.cpp
    src/rag/Persistence.cpp
    src/rag/GraphRagAgent.cpp
    src/rag/LlmBackend.cpp
    src/rag/DocumentPreprocessor.cpp
)

set(NEURAL_SOURCES
    src/neural/EmbeddingModels.cpp
    src/neural/NeuralMissing.cpp
    src/neural/GnnEmbedding.cpp
)

set(API_SOURCES
    src/api/HttpServer.cpp
    src/api/WebSocket.cpp
    src/api/handlers/HealthHandler.cpp
    src/api/handlers/ClassHandler.cpp
    src/api/handlers/IndividualHandler.cpp
    src/api/handlers/TripleHandler.cpp
    src/api/handlers/InferenceHandler.cpp
    src/api/handlers/SparqlHandler.cpp
    src/api/handlers/RuleHandler.cpp
    src/api/handlers/RagHandler.cpp
    src/api/handlers/ShaclHandler.cpp
    src/api/handlers/PersistenceHandler.cpp
    src/api/handlers/ExportHandler.cpp
    src/api/handlers/JsonHelpers.cpp
)

set(BOOTSTRAP_SOURCES
    src/bootstrap/Bootstrap.cpp
)

# 创建核心库
add_library(ontology_core STATIC
    ${CORE_SOURCES}
    ${OWL_SOURCES}
    ${IO_SOURCES}
    ${SPARQL_SOURCES}
    ${SWRL_SOURCES}
    ${STORAGE_SOURCES}
    ${INFERENCE_SOURCES}
    ${NEURAL_SOURCES}
    ${MCP_SOURCES}
    ${RAG_SOURCES}
)

target_include_directories(ontology_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(ZLIB REQUIRED)

target_link_libraries(ontology_core
    PUBLIC
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        CLI11::CLI11
    PRIVATE
        httplib
        ZLIB::ZLIB
)

# ========== API 服务 ==========
add_executable(ontology-server
    src/main.cpp
    ${API_SOURCES}
    ${BOOTSTRAP_SOURCES}
)
target_include_directories(ontology-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ontology-server PRIVATE ontology_core httplib curl)
if(APPLE)
    target_link_libraries(ontology-server PRIVATE "-framework CoreFoundation" "-framework Security")
endif()

# ========== 测试 ==========
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

# ========== 安装 ==========
install(TARGETS ontology_core ontology-server
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ontology DESTINATION include)

message(STATUS "=== Ontology Platform ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
