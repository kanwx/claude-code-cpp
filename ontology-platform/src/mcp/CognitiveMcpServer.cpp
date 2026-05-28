#include "ontology/mcp/CognitiveMcpServer.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <algorithm>
#include <regex>
#include <httplib.h>

namespace ontology {

// ============================================================================
// Tool Implementations
// ============================================================================

IntentRecognitionTool::IntentRecognitionTool(AutoModelEngine* autoModel, HybridReasoner* reasoner)
    : autoModel_(autoModel), reasoner_(reasoner) {}

String IntentRecognitionTool::description() const {
    return "Analyze user input to identify intent type and extract entities.";
}

Json IntentRecognitionTool::inputSchema() const {
    Json schema;
    schema["type"] = "object";
    schema["properties"]["text"]["type"] = "string";
    schema["properties"]["text"]["description"] = "User input text to analyze";
    return schema;
}

Json IntentRecognitionTool::execute(const Json& input) {
    String text = input["text"].get<String>();

    Intent intent;
    intent.type = classifyIntent(text);
    intent.entities = extractEntities(text);
    intent.confidence = 0.85f;

    return intent.toJson();
}

String IntentRecognitionTool::classifyIntent(const String& text) {
    if (text.find("创建") != String::npos || text.find("添加") != String::npos ||
        text.find("create") != String::npos || text.find("add") != String::npos) {
        return "create";
    }
    if (text.find("删除") != String::npos || text.find("移除") != String::npos ||
        text.find("delete") != String::npos || text.find("remove") != String::npos) {
        return "delete";
    }
    if (text.find("更新") != String::npos || text.find("修改") != String::npos ||
        text.find("update") != String::npos || text.find("modify") != String::npos) {
        return "update";
    }
    if (text.find("为什么") != String::npos || text.find("解释") != String::npos ||
        text.find("why") != String::npos || text.find("explain") != String::npos) {
        return "explain";
    }
    if (text.find("推理") != String::npos || text.find("分析") != String::npos ||
        text.find("infer") != String::npos || text.find("reason") != String::npos) {
        return "reason";
    }
    return "query";
}

std::vector<String> IntentRecognitionTool::extractEntities(const String& text) {
    std::vector<String> entities;
    std::regex entityPattern(R"([A-Z][a-z]+(?:[A-Z][a-z]+)+)");
    std::sregex_iterator it(text.begin(), text.end(), entityPattern);
    std::sregex_iterator end;

    for (; it != end; ++it) {
        entities.push_back(it->str());
    }
    return entities;
}

// OntologyQueryTool
OntologyQueryTool::OntologyQueryTool(StoragePtr storage) : storage_(storage) {}

String OntologyQueryTool::description() const {
    return "Query the knowledge graph using SPARQL-like patterns.";
}

Json OntologyQueryTool::inputSchema() const {
    Json schema;
    schema["type"] = "object";
    schema["properties"]["subject"]["type"] = "string";
    schema["properties"]["predicate"]["type"] = "string";
    schema["properties"]["object"]["type"] = "string";
    return schema;
}

Json OntologyQueryTool::execute(const Json& input) {
    auto subject = input.contains("subject") && !input["subject"].is_null()
        ? std::optional<String>(input["subject"].get<String>())
        : std::nullopt;
    auto predicate = input.contains("predicate") && !input["predicate"].is_null()
        ? std::optional<String>(input["predicate"].get<String>())
        : std::nullopt;
    auto object = input.contains("object") && !input["object"].is_null()
        ? std::optional<String>(input["object"].get<String>())
        : std::nullopt;

    std::vector<Triple> results = storage_->queryTriples(
        TripleStore::TriplePattern{
            subject.value_or(""),
            predicate.value_or(""),
            object.value_or("")
        }
    );

    Json result = Json::array();
    for (const auto& t : results) {
        result.push_back(t.toJson());
    }
    return result;
}

// InferenceTool
InferenceTool::InferenceTool(HybridReasoner* reasoner) : reasoner_(reasoner) {}

String InferenceTool::description() const {
    return "Perform hybrid inference (symbolic + neural) on an entity.";
}

Json InferenceTool::inputSchema() const {
    Json schema;
    schema["type"] = "object";
    schema["properties"]["entityId"]["type"] = "string";
    schema["properties"]["entityId"]["description"] = "Entity ID to infer from";
    return schema;
}

Json InferenceTool::execute(const Json& input) {
    String entityId = input["entityId"].get<String>();

    auto results = reasoner_->infer(entityId);

    Json result = Json::array();
    for (const auto& t : results.combined) {
        result.push_back(t.toJson());
    }
    return result;
}

// OntologySuggestTool
OntologySuggestTool::OntologySuggestTool(AutoModelEngine* autoModel) : autoModel_(autoModel) {}

String OntologySuggestTool::description() const {
    return "Suggest ontology structure from natural language description.";
}

Json OntologySuggestTool::inputSchema() const {
    Json schema;
    schema["type"] = "object";
    schema["properties"]["description"]["type"] = "string";
    return schema;
}

Json OntologySuggestTool::execute(const Json& input) {
    String description = input["description"].get<String>();

    auto suggestion = autoModel_->getSuggestions();

    Json result;
    Json relations = Json::array();
    for (const auto& r : suggestion.relations) {
        Json rel;
        rel["subject"] = r.subject;
        rel["predicate"] = r.predicate;
        rel["object"] = r.object;
        rel["confidence"] = r.confidence;
        relations.push_back(rel);
    }
    result["relations"] = relations;

    return result;
}

// KnowledgeCreateTool
KnowledgeCreateTool::KnowledgeCreateTool(StoragePtr storage) : storage_(storage) {}

String KnowledgeCreateTool::description() const {
    return "Create new knowledge (entities, relations, rules).";
}

Json KnowledgeCreateTool::inputSchema() const {
    Json schema;
    schema["type"] = "object";
    schema["properties"]["type"]["type"] = "string";
    schema["properties"]["type"]["description"] = "Type: class, relation, individual, triple";
    schema["properties"]["definition"]["type"] = "object";
    return schema;
}

Json KnowledgeCreateTool::execute(const Json& input) {
    String type = input["type"].get<String>();
    Json definition = input["definition"];

    try {
        if (type == "class") {
            Class cls;
            cls.id = definition["id"].get<String>();
            cls.name = definition["name"].get<String>();
            cls.description = definition.value("description", "");

            storage_->addClass(cls);
            return {{"success", true}, {"id", cls.id}};
        }
        else if (type == "relation") {
            Relation rel;
            rel.id = definition["id"].get<String>();
            rel.name = definition["name"].get<String>();
            rel.domain = definition.value("domain", "");
            rel.range = definition.value("range", "");

            storage_->addRelation(rel);
            return {{"success", true}, {"id", rel.id}};
        }
        else if (type == "individual") {
            Individual ind;
            ind.id = definition["id"].get<String>();
            ind.name = definition.value("name", "");
            ind.classId = definition.value("classId", "");

            storage_->addIndividual(ind);
            return {{"success", true}, {"id", ind.id}};
        }
        else if (type == "triple") {
            Triple t;
            t.subject = definition["subject"].get<String>();
            t.predicate = definition["predicate"].get<String>();
            t.object = definition["object"].get<String>();
            t.confidence = definition.value("confidence", 1.0f);

            storage_->addTriple(t);
            return {{"success", true}, {"triple", t.toJson()}};
        }

        return {{"success", false}, {"error", "Unknown type: " + type}};
    }
    catch (const std::exception& e) {
        return {{"success", false}, {"error", e.what()}};
    }
}

// SemanticSearchTool
SemanticSearchTool::SemanticSearchTool(StoragePtr storage, NeuralReasoner* neural, TextEmbedder* embedder)
    : storage_(storage), neural_(neural), textEmbedder_(embedder) {}

String SemanticSearchTool::description() const {
    return "Semantic search over knowledge graph.";
}

Json SemanticSearchTool::inputSchema() const {
    Json schema;
    schema["type"] = "object";
    schema["properties"]["query"]["type"] = "string";
    schema["properties"]["topK"]["type"] = "integer";
    return schema;
}

Json SemanticSearchTool::execute(const Json& input) {
    String query = input["query"].get<String>();
    int topK = input.value("topK", 10);

    std::vector<std::pair<String, float>> results;

    // Use text-based search if TextEmbedder is available
    if (neural_ && textEmbedder_) {
        results = neural_->findSimilarByText(query, topK);
    } else if (neural_) {
        // Fallback: try entity-ID lookup first, then brute-force match
        auto idResults = neural_->findSimilar(query, topK);
        if (!idResults.empty()) {
            results = idResults;
        } else {
            // Brute-force: match query text against entity names
            auto individuals = storage_->getAllIndividuals();
            for (const auto& ind : individuals) {
                if (!ind.name.empty() && query.find(ind.name) != String::npos) {
                    results.push_back({ind.id, 0.9f});
                } else if (query.find(ind.id) != String::npos) {
                    results.push_back({ind.id, 0.8f});
                }
            }
            if (static_cast<int>(results.size()) > topK) {
                results.resize(topK);
            }
        }
    }

    Json result = Json::array();
    for (const auto& [id, score] : results) {
        Json item;
        item["id"] = id;
        item["score"] = score;
        auto ind = storage_->getIndividual(id);
        if (ind) {
            item["name"] = ind->name;
            item["classId"] = ind->classId;
        }
        result.push_back(item);
    }
    return result;
}

// ConsistencyCheckTool
ConsistencyCheckTool::ConsistencyCheckTool(SymbolicReasoner* symbolic) : symbolic_(symbolic) {}

String ConsistencyCheckTool::description() const {
    return "Check ontology consistency for conflicts.";
}

Json ConsistencyCheckTool::inputSchema() const {
    return {{"type", "object"}, {"properties", Json::object()}};
}

Json ConsistencyCheckTool::execute(const Json& input) {
    auto violations = symbolic_->checkConsistency();

    Json result;
    result["isConsistent"] = violations.empty();
    Json arr = Json::array();
    for (const auto& v : violations) {
        arr.push_back(v.description);
    }
    result["violations"] = arr;
    return result;
}

// EmbeddingTool - 计算实体嵌入
EmbeddingTool::EmbeddingTool(NeuralReasoner* neural) : neural_(neural) {}

String EmbeddingTool::description() const {
    return "Compute or retrieve embedding vectors for entities.";
}

Json EmbeddingTool::inputSchema() const {
    Json schema;
    schema["type"] = "object";
    schema["properties"]["entities"]["type"] = "array";
    schema["properties"]["entities"]["description"] = "List of entity IDs to embed";
    return schema;
}

Json EmbeddingTool::execute(const Json& input) {
    Json entities = input["entities"];
    Json result = Json::array();

    for (const auto& e : entities) {
        String entityId = e.get<String>();
        auto embedding = neural_->getEmbedding(entityId);

        Json item;
        item["entity"] = entityId;
        item["embedding"] = Json::array();
        for (float v : embedding) {
            item["embedding"].push_back(v);
        }
        result.push_back(item);
    }

    return result;
}

// LinkPredictionTool - 链接预测
LinkPredictionTool::LinkPredictionTool(NeuralReasoner* neural) : neural_(neural) {}

String LinkPredictionTool::description() const {
    return "Predict missing links using neural embedding models.";
}

Json LinkPredictionTool::inputSchema() const {
    Json schema;
    schema["type"] = "object";
    schema["properties"]["head"]["type"] = "string";
    schema["properties"]["relation"]["type"] = "string";
    schema["properties"]["tail"]["type"] = "string";
    schema["properties"]["topK"]["type"] = "integer";
    schema["description"] = "Provide head+relation to predict tail, or relation+tail to predict head";
    return schema;
}

Json LinkPredictionTool::execute(const Json& input) {
    String head = input.value("head", "");
    String relation = input.value("relation", "");
    String tail = input.value("tail", "");
    int topK = input.value("topK", 5);

    Json result;

    if (!head.empty() && !relation.empty() && tail.empty()) {
        // 预测尾实体: (?, relation, ?)
        auto predictions = neural_->predictLinks(head, relation, topK);
        result["type"] = "tail_prediction";
        result["predictions"] = Json::array();
        for (const auto& [entity, score] : predictions) {
            result["predictions"].push_back({{"entity", entity}, {"score", score}});
        }
    }
    else if (head.empty() && !relation.empty() && !tail.empty()) {
        // 预测头实体: (?, relation, tail)
        auto predictions = neural_->predictHead(tail, relation, topK);
        result["type"] = "head_prediction";
        result["predictions"] = Json::array();
        for (const auto& [entity, score] : predictions) {
            result["predictions"].push_back({{"entity", entity}, {"score", score}});
        }
    }
    else {
        result["error"] = "Need either (head+relation) or (relation+tail)";
    }

    return result;
}

// ExplainTool - 推理解释
ExplainTool::ExplainTool(HybridReasoner* hybrid, StoragePtr storage)
    : hybrid_(hybrid), storage_(storage) {}

String ExplainTool::description() const {
    return "Explain how a conclusion was derived.";
}

Json ExplainTool::inputSchema() const {
    Json schema;
    schema["type"] = "object";
    schema["properties"]["subject"]["type"] = "string";
    schema["properties"]["predicate"]["type"] = "string";
    schema["properties"]["object"]["type"] = "string";
    return schema;
}

Json ExplainTool::execute(const Json& input) {
    String subject = input["subject"].get<String>();
    String predicate = input["predicate"].get<String>();
    String object = input["object"].get<String>();

    Json result;
    result["conclusion"]["subject"] = subject;
    result["conclusion"]["predicate"] = predicate;
    result["conclusion"]["object"] = object;

    // 查找证据链
    Json evidence = Json::array();

    // 1. 检查是否为原始事实
    TripleStore::TriplePattern pattern;
    pattern.subject = subject;
    pattern.predicate = predicate;
    pattern.object = object;
    pattern.subjectIsVar = false;
    pattern.predicateIsVar = false;
    pattern.objectIsVar = false;

    auto direct = storage_->queryTriples(pattern);
    if (!direct.empty()) {
        Json step;
        step["type"] = "direct_fact";
        step["source"] = "knowledge_base";
        step["confidence"] = direct[0].confidence;
        evidence.push_back(step);
    }

    // 2. 检查传递性推理
    if (predicate == "manages" || predicate == "reportsTo" || predicate == "partOf") {
        // 尝试找中间节点
        auto intermediates = storage_->findBySubject(subject);
        for (const auto& t : intermediates) {
            if (t.predicate == predicate) {
                // 检查 t.object -> object 是否存在
                auto next = storage_->queryTriples(
                    TripleStore::TriplePattern{t.object, predicate, object}
                );
                if (!next.empty()) {
                    Json step;
                    step["type"] = "transitive_inference";
                    step["intermediate"] = t.object;
                    step["chain"] = Json::array();
                    step["chain"].push_back({{subject, predicate, t.object}});
                    step["chain"].push_back({{t.object, predicate, object}});
                    evidence.push_back(step);
                }
            }
        }
    }

    result["evidence"] = evidence;
    result["explainable"] = !evidence.empty();

    return result;
}

// TransitiveClosureTool - 传递闭包
TransitiveClosureTool::TransitiveClosureTool(StoragePtr storage) : storage_(storage) {}

String TransitiveClosureTool::description() const {
    return "Compute transitive closure for a relation.";
}

Json TransitiveClosureTool::inputSchema() const {
    Json schema;
    schema["type"] = "object";
    schema["properties"]["predicate"]["type"] = "string";
    schema["properties"]["maxDepth"]["type"] = "integer";
    return schema;
}

Json TransitiveClosureTool::execute(const Json& input) {
    String predicate = input["predicate"].get<String>();
    int maxDepth = input.value("maxDepth", 10);

    auto closure = storage_->computeTransitiveClosure(predicate, maxDepth);

    Json result;
    result["predicate"] = predicate;
    result["pairs"] = Json::array();
    result["count"] = closure.size();

    for (const auto& [from, to] : closure) {
        result["pairs"].push_back({{"from", from}, {"to", to}});
    }

    return result;
}

// PathFindingTool - 路径查找
PathFindingTool::PathFindingTool(StoragePtr storage) : storage_(storage) {}

String PathFindingTool::description() const {
    return "Find paths between two entities.";
}

Json PathFindingTool::inputSchema() const {
    Json schema;
    schema["type"] = "object";
    schema["properties"]["from"]["type"] = "string";
    schema["properties"]["to"]["type"] = "string";
    schema["properties"]["predicate"]["type"] = "string";
    schema["properties"]["maxDepth"]["type"] = "integer";
    return schema;
}

Json PathFindingTool::execute(const Json& input) {
    String from = input["from"].get<String>();
    String to = input["to"].get<String>();
    String predicate = input.value("predicate", "");
    int maxDepth = input.value("maxDepth", 5);

    auto paths = storage_->findPath(from, to, predicate, maxDepth);

    Json result;
    result["from"] = from;
    result["to"] = to;
    result["paths"] = Json::array();
    result["pathCount"] = paths.size();

    for (const auto& path : paths) {
        Json pathJson = Json::array();
        for (const String& node : path) {
            pathJson.push_back(node);
        }
        result["paths"].push_back(pathJson);
    }

    return result;
}

// ============================================================================
// CognitiveMcpServer Implementation
// ============================================================================

CognitiveMcpServer::CognitiveMcpServer(StoragePtr storage,
                                       HybridReasoner* hybrid,
                                       SymbolicReasoner* symbolic,
                                       NeuralReasoner* neural,
                                       AutoModelEngine* autoModel,
                                       TextEmbedder* textEmbedder)
    : storage_(storage)
    , hybrid_(hybrid)
    , symbolic_(symbolic)
    , neural_(neural)
    , autoModel_(autoModel)
    , textEmbedder_(textEmbedder)
{
    initializeTools();
    initializeResources();
    initializePrompts();
}

CognitiveMcpServer::~CognitiveMcpServer() {
    for (auto& [name, tool] : tools_) {
        delete tool;
    }
}

void CognitiveMcpServer::initializeTools() {
    // 核心认知工具
    registerTool(std::make_unique<IntentRecognitionTool>(autoModel_, hybrid_));
    registerTool(std::make_unique<OntologyQueryTool>(storage_));
    registerTool(std::make_unique<InferenceTool>(hybrid_));
    registerTool(std::make_unique<OntologySuggestTool>(autoModel_));
    registerTool(std::make_unique<KnowledgeCreateTool>(storage_));
    registerTool(std::make_unique<SemanticSearchTool>(storage_, neural_, textEmbedder_));
    registerTool(std::make_unique<ConsistencyCheckTool>(symbolic_));

    // 新增工具
    registerTool(std::make_unique<EmbeddingTool>(neural_));
    registerTool(std::make_unique<LinkPredictionTool>(neural_));
    registerTool(std::make_unique<ExplainTool>(hybrid_, storage_));
    registerTool(std::make_unique<TransitiveClosureTool>(storage_));
    registerTool(std::make_unique<PathFindingTool>(storage_));
}

void CognitiveMcpServer::initializeResources() {
    resources_["knowledge://graph"] = {
        "knowledge://graph",
        "Knowledge Graph",
        "The complete knowledge graph",
        "application/json"
    };

    resources_["ontology://schema"] = {
        "ontology://schema",
        "Ontology Schema",
        "The ontology schema (classes and relations)",
        "application/json"
    };
}

void CognitiveMcpServer::initializePrompts() {
    prompts_["reasoning"] = {
        "reasoning",
        "Chain-of-thought reasoning with knowledge graph lookup",
        {"query", "context"}
    };

    prompts_["ontology_modeling"] = {
        "ontology_modeling",
        "Prompt for creating ontology from text",
        {"text"}
    };
}

void CognitiveMcpServer::registerTool(std::unique_ptr<CognitiveTool> tool) {
    String name = tool->name();
    tools_[name] = tool.release();
}

std::vector<McpTool> CognitiveMcpServer::listTools() const {
    std::vector<McpTool> result;
    for (const auto& [name, tool] : tools_) {
        result.push_back(tool->toMcpTool());
    }
    return result;
}

Json CognitiveMcpServer::handleToolCall(const String& name, const Json& arguments) {
    auto it = tools_.find(name);
    if (it != tools_.end()) {
        return it->second->execute(arguments);
    }
    return {{"error", "Tool not found: " + name}};
}

std::vector<McpResource> CognitiveMcpServer::listResources() const {
    std::vector<McpResource> result;
    for (const auto& [uri, resource] : resources_) {
        result.push_back(resource);
    }
    return result;
}

String CognitiveMcpServer::readResource(const String& uri) {
    if (uri == "knowledge://graph") {
        auto triples = storage_->getAllTriples();
        Json result = Json::array();
        for (const auto& t : triples) {
            result.push_back(t.toJson());
        }
        return result.dump();
    }
    else if (uri == "ontology://schema") {
        auto classes = storage_->getAllClasses();

        Json result;
        Json classArr = Json::array();
        for (const auto& c : classes) {
            classArr.push_back(c.toJson());
        }
        result["classes"] = classArr;

        return result.dump();
    }

    return "";
}

std::vector<McpPrompt> CognitiveMcpServer::listPrompts() const {
    std::vector<McpPrompt> result;
    for (const auto& [name, prompt] : prompts_) {
        result.push_back(prompt);
    }
    return result;
}

String CognitiveMcpServer::getPrompt(const String& name, const Json& arguments) {
    if (name == "reasoning") {
        return "Use the cognitive tools to reason about the query:\n"
               "1. Use cognitive_intent to analyze the query\n"
               "2. Use cognitive_query to retrieve relevant facts\n"
               "3. Use cognitive_infer for deeper inference\n"
               "Query: " + arguments.value("query", "");
    }
    else if (name == "ontology_modeling") {
        return "Analyze the following text and suggest ontology structure:\n"
               "Text: " + arguments.value("text", "");
    }

    return "";
}

void CognitiveMcpServer::run() {
    String line;
    while (std::getline(std::cin, line)) {
        try {
            Json request = Json::parse(line);

            String method = request["method"].get<String>();
            Json params = request.value("params", Json::object());

            Json response;

            if (method == "initialize") {
                response["result"]["protocolVersion"] = "2024-11-05";
                response["result"]["serverInfo"]["name"] = "cognitive-server";
                response["result"]["serverInfo"]["version"] = "1.0.0";
            }
            else if (method == "tools/list") {
                Json tools = Json::array();
                for (const auto& t : listTools()) {
                    tools.push_back(t.toJson());
                }
                response["result"]["tools"] = tools;
            }
            else if (method == "tools/call") {
                String toolName = params["name"].get<String>();
                Json arguments = params.value("arguments", Json::object());
                Json result = handleToolCall(toolName, arguments);
                response["result"] = result;
            }
            else if (method == "resources/list") {
                Json resources = Json::array();
                for (const auto& r : listResources()) {
                    Json res;
                    res["uri"] = r.uri;
                    res["name"] = r.name;
                    resources.push_back(res);
                }
                response["result"]["resources"] = resources;
            }
            else if (method == "resources/read") {
                String uri = params["uri"].get<String>();
                String content = readResource(uri);
                response["result"]["contents"] = Json::array();
                response["result"]["contents"][0]["uri"] = uri;
                response["result"]["contents"][0]["text"] = content;
            }
            else {
                response["error"]["code"] = -32601;
                response["error"]["message"] = "Method not found";
            }

            if (request.contains("id")) {
                response["id"] = request["id"];
            }

            std::cout << response.dump() << std::endl;
        }
        catch (const std::exception& e) {
            Json response;
            response["error"]["code"] = -32700;
            response["error"]["message"] = e.what();
            std::cout << response.dump() << std::endl;
        }
    }
}

void CognitiveMcpServer::runHttp(int port) {
    httplib::Server server;

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status": "ok"})", "application/json");
    });

    server.Get("/tools/list", [this](const httplib::Request&, httplib::Response& res) {
        Json tools = Json::array();
        for (const auto& t : listTools()) {
            tools.push_back(t.toJson());
        }
        res.set_content(Json{{"tools", tools}}.dump(), "application/json");
    });

    server.Post("/tools/call", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            Json body = Json::parse(req.body);
            String toolName = body["name"].get<String>();
            Json arguments = body.value("arguments", Json::object());
            Json result = handleToolCall(toolName, arguments);
            res.set_content(result.dump(), "application/json");
        }
        catch (const std::exception& e) {
            res.status = 400;
            res.set_content(String(R"({"error": ")") + e.what() + R"("})", "application/json");
        }
    });

    std::cout << "Cognitive MCP Server listening on port " << port << std::endl;
    server.listen("0.0.0.0", port);
}

// ============================================================================
// LlmCollaboration Implementation
// ============================================================================

LlmCollaboration::LlmCollaboration(StoragePtr storage,
                                   HybridReasoner* hybrid,
                                   AutoModelEngine* autoModel,
                                   const String& apiKey)
    : storage_(storage)
    , hybrid_(hybrid)
    , autoModel_(autoModel)
    , apiKey_(apiKey)
{
}

CollaborationResult LlmCollaboration::reason(const String& query) {
    CollaborationResult result;

    String intent = analyzeIntent(query);
    std::vector<String> entities = extractEntities(query);

    // 构建上下文
    String context = buildContext(entities);

    if (!apiKey_.empty()) {
        // 使用 LLM 协作推理
        String systemPrompt =
            "You are an ontology reasoning assistant. You have access to a knowledge graph.\n"
            "Context from knowledge graph:\n" + context + "\n\n"
            "Answer the user's question based on the knowledge graph data. "
            "If the data is insufficient, say so clearly. "
            "Provide confidence level (0-1) for your answer.";

        String llmAnswer = callLlm(systemPrompt, query);
        result.answer = llmAnswer;
        result.confidence = 0.85f;
    } else {
        // 本地推理回退
        for (const String& entityId : entities) {
            auto inferred = hybrid_->infer(entityId);
            for (const auto& t : inferred.combined) {
                result.facts.push_back(t);
            }
        }
        result.answer = "Based on knowledge graph analysis: found " +
                        std::to_string(result.facts.size()) + " relevant facts.";
        result.confidence = 0.7f;
    }

    result.reasoning.push_back("Intent: " + intent);
    result.reasoning.push_back("Entities: " + std::to_string(entities.size()));
    result.reasoning.push_back("Facts found: " + std::to_string(result.facts.size()));

    return result;
}

McpOntologySuggestion LlmCollaboration::autoModel(
    const String& text, const String& domain) {

    McpOntologySuggestion suggestion;

    if (!apiKey_.empty()) {
        String systemPrompt =
            "You are an ontology modeling expert. Analyze the given text and suggest "
            "an ontology structure (classes, relations, triples).\n"
            "Domain: " + (domain.empty() ? "general" : domain) + "\n\n"
            "Respond in JSON format:\n"
            "{\n"
            "  \"classes\": [{\"id\": \"...\", \"name\": \"...\", \"superClasses\": [...]}],\n"
            "  \"relations\": [{\"id\": \"...\", \"name\": \"...\", \"domain\": \"...\", \"range\": \"...\"}],\n"
            "  \"triples\": [{\"subject\": \"...\", \"predicate\": \"...\", \"object\": \"...\"}],\n"
            "  \"confidence\": 0.0-1.0\n"
            "}";

        String llmResponse = callLlm(systemPrompt, text);
        suggestion = parseModelResponse(llmResponse);
    } else {
        // Local heuristic modeling
        auto entities = extractEntities(text);
        for (const auto& entityId : entities) {
            Class cls;
            cls.id = entityId;
            cls.name = entityId;
            suggestion.suggestedClasses.push_back(cls);
        }
        suggestion.confidence = 0.5f;
    }

    return suggestion;
}

LlmCollaboration::ExtractionResult LlmCollaboration::extractFromText(
    const String& text, const String& domain) {

    ExtractionResult result;

    if (!apiKey_.empty()) {
        String systemPrompt =
            "You are an entity and relation extraction expert. Extract entities and their "
            "relations from the given text.\n"
            "Domain: " + (domain.empty() ? "general" : domain) + "\n\n"
            "Respond in JSON format:\n"
            "{\n"
            "  \"entities\": [{\"id\": \"...\", \"type\": \"...\"}],\n"
            "  \"relations\": [{\"subject\": \"...\", \"predicate\": \"...\", \"object\": \"...\"}],\n"
            "  \"confidence\": 0.0-1.0\n"
            "}";

        String llmResponse = callLlm(systemPrompt, text);
        result = parseExtractionResponse(llmResponse);
    } else {
        // 本地提取回退
        auto entities = extractEntities(text);
        for (const auto& entityId : entities) {
            result.entities.push_back({entityId, "Entity"});
        }
        result.confidence = 0.4f;
    }

    return result;
}

String LlmCollaboration::callLlm(const String& systemPrompt, const String& userMessage) {
    if (apiKey_.empty()) return "";

    try {
        httplib::Client client("https://api.anthropic.com");
        client.set_read_timeout(60);

        Json requestBody;
        requestBody["model"] = model_;
        requestBody["max_tokens"] = 4096;

        Json systemMsg;
        systemMsg["type"] = "text";
        systemMsg["text"] = systemPrompt;
        requestBody["system"] = Json::array({systemMsg});

        Json userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = userMessage;
        requestBody["messages"] = Json::array({userMsg});

        httplib::Headers headers = {
            {"Content-Type", "application/json"},
            {"x-api-key", apiKey_},
            {"anthropic-version", "2023-06-01"}
        };

        auto res = client.Post("/v1/messages", headers, requestBody.dump(), "application/json");

        if (res && res->status == 200) {
            Json response = Json::parse(res->body);
            if (response.contains("content") && response["content"].is_array()) {
                String result;
                for (const auto& block : response["content"]) {
                    if (block.value("type", "") == "text") {
                        result += block["text"].get<String>();
                    }
                }
                return result;
            }
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("MCP JSON error: {}", e.what());
    } catch (const std::exception& e) {
        spdlog::error("MCP error: {}", e.what());
    }

    return "";
}

String LlmCollaboration::buildContext(const std::vector<String>& entityIds) {
    std::ostringstream oss;
    oss << "Knowledge Graph Context:\n";

    for (const auto& entityId : entityIds) {
        // 查询实体的所有三元组
        if (storage_) {
            auto bySubject = storage_->findBySubject(entityId);
            if (!bySubject.empty()) {
                oss << "Entity: " << entityId << "\n";
                for (const auto& t : bySubject) {
                    oss << "  - " << t.predicate << " -> " << t.object
                        << " (confidence: " << t.confidence << ")\n";
                }
            }

            auto cls = storage_->getClass(entityId);
            if (cls) {
                oss << "Class: " << cls->name << " (id: " << cls->id << ")\n";
                if (!cls->superClasses.empty()) {
                    oss << "  SuperClasses: ";
                    for (size_t i = 0; i < cls->superClasses.size(); i++) {
                        if (i > 0) oss << ", ";
                        oss << cls->superClasses[i];
                    }
                    oss << "\n";
                }
            }
        }
    }

    return oss.str();
}

McpOntologySuggestion LlmCollaboration::parseModelResponse(
    const String& llmResponse) {
    McpOntologySuggestion suggestion;

    try {
        auto jsonStart = llmResponse.find('{');
        auto jsonEnd = llmResponse.rfind('}');
        if (jsonStart != String::npos && jsonEnd != String::npos) {
            String jsonStr = llmResponse.substr(jsonStart, jsonEnd - jsonStart + 1);
            Json data = Json::parse(jsonStr);

            suggestion.confidence = data.value("confidence", 0.7f);

            if (data.contains("classes")) {
                for (const auto& c : data["classes"]) {
                    Class cls;
                    cls.id = c.value("id", "");
                    cls.name = c.value("name", cls.id);
                    cls.superClasses = c.value("superClasses", std::vector<String>{});
                    suggestion.suggestedClasses.push_back(cls);
                }
            }

            if (data.contains("relations")) {
                for (const auto& r : data["relations"]) {
                    Relation rel;
                    rel.id = r.value("id", "");
                    rel.name = r.value("name", rel.id);
                    rel.domain = r.value("domain", "");
                    rel.range = r.value("range", "");
                    suggestion.suggestedRelations.push_back(rel);
                }
            }

            if (data.contains("triples")) {
                for (const auto& t : data["triples"]) {
                    Triple tr;
                    tr.subject = t.value("subject", "");
                    tr.predicate = t.value("predicate", "");
                    tr.object = t.value("object", "");
                    tr.confidence = suggestion.confidence;
                    suggestion.suggestedTriples.push_back(tr);
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("MCP JSON error: {}", e.what());
        suggestion.confidence = 0.3f;
    } catch (const std::exception& e) {
        spdlog::error("MCP error: {}", e.what());
        suggestion.confidence = 0.3f;
    }

    return suggestion;
}

LlmCollaboration::ExtractionResult LlmCollaboration::parseExtractionResponse(
    const String& llmResponse) {
    ExtractionResult result;

    try {
        auto jsonStart = llmResponse.find('{');
        auto jsonEnd = llmResponse.rfind('}');
        if (jsonStart != String::npos && jsonEnd != String::npos) {
            String jsonStr = llmResponse.substr(jsonStart, jsonEnd - jsonStart + 1);
            Json data = Json::parse(jsonStr);

            result.confidence = data.value("confidence", 0.7f);

            if (data.contains("entities")) {
                for (const auto& e : data["entities"]) {
                    result.entities.push_back({
                        e.value("id", ""),
                        e.value("type", "Entity")
                    });
                }
            }

            if (data.contains("relations")) {
                for (const auto& r : data["relations"]) {
                    Triple tr;
                    tr.subject = r.value("subject", "");
                    tr.predicate = r.value("predicate", "");
                    tr.object = r.value("object", "");
                    tr.confidence = result.confidence;
                    result.relations.push_back(tr);
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("MCP JSON error: {}", e.what());
        result.confidence = 0.3f;
    } catch (const std::exception& e) {
        spdlog::error("MCP error: {}", e.what());
        result.confidence = 0.3f;
    }

    return result;
}

String LlmCollaboration::analyzeIntent(const String& query) {
    if (query.find("创建") != String::npos || query.find("添加") != String::npos) {
        return "create";
    }
    if (query.find("删除") != String::npos || query.find("移除") != String::npos) {
        return "delete";
    }
    return "query";
}

std::vector<String> LlmCollaboration::extractEntities(const String& query) {
    std::vector<String> entities;
    std::regex entityPattern(R"([A-Z][a-z]+(?:[A-Z][a-z]+)+)");
    std::sregex_iterator it(query.begin(), query.end(), entityPattern);
    std::sregex_iterator end;

    for (; it != end; ++it) {
        entities.push_back(it->str());
    }
    return entities;
}

// ============================================================================
// IntentRecognitionService Implementation
// ============================================================================

IntentRecognitionService::IntentRecognitionService(StoragePtr storage,
                                                   AutoModelEngine* autoModel)
    : storage_(storage)
    , autoModel_(autoModel)
{
}

Intent IntentRecognitionService::recognize(const String& text) {
    Intent intent;

    intent.type = classifyIntent(text);
    intent.entities = extractEntities(text);
    intent.confidence = 0.85f;

    return intent;
}

String IntentRecognitionService::classifyIntent(const String& text) {
    if (text.find("创建") != String::npos || text.find("添加") != String::npos) {
        return "create";
    }
    if (text.find("删除") != String::npos || text.find("移除") != String::npos) {
        return "delete";
    }
    if (text.find("更新") != String::npos || text.find("修改") != String::npos) {
        return "update";
    }
    return "query";
}

std::vector<String> IntentRecognitionService::extractEntities(const String& text) {
    std::vector<String> entities;
    std::regex entityPattern(R"([A-Z][a-z]+(?:[A-Z][a-z]+)+)");
    std::sregex_iterator it(text.begin(), text.end(), entityPattern);
    std::sregex_iterator end;

    for (; it != end; ++it) {
        entities.push_back(it->str());
    }
    return entities;
}

// ============================================================================
// CognitiveCache Implementation
// ============================================================================

CognitiveCache::CognitiveCache(size_t maxSize, int ttl)
    : maxSize_(maxSize)
    , ttlSeconds_(ttl)
{
}

void CognitiveCache::cacheQuery(const String& pattern,
                               const std::vector<Triple>& results) {
    std::lock_guard<std::mutex> lock(mutex_);

    String key = makeKey("query", pattern);
    Json arr = Json::array();
    for (const auto& t : results) {
        arr.push_back(t.toJson());
    }
    cache_[key] = arr;
    timestamps_[key] = std::time(nullptr);
}

std::optional<std::vector<Triple>> CognitiveCache::getQueryCache(const String& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);

    String key = makeKey("query", pattern);
    auto it = cache_.find(key);

    if (it != cache_.end()) {
        auto ts = timestamps_.find(key);
        if (ts != timestamps_.end() && std::time(nullptr) - ts->second < ttlSeconds_) {
            hits_++;
            std::vector<Triple> results;
            for (const Json& j : it->second) {
                Triple t;
                t.subject = j["subject"].get<String>();
                t.predicate = j["predicate"].get<String>();
                t.object = j["object"].get<String>();
                results.push_back(t);
            }
            return results;
        }
    }

    misses_++;
    return std::nullopt;
}

void CognitiveCache::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);

    int64_t now = std::time(nullptr);

    std::vector<String> expired;
    for (const auto& [key, timestamp] : timestamps_) {
        if (now - timestamp >= ttlSeconds_) {
            expired.push_back(key);
        }
    }

    for (const String& key : expired) {
        cache_.erase(key);
        timestamps_.erase(key);
    }
}

size_t CognitiveCache::size() const {
    return cache_.size();
}

float CognitiveCache::hitRate() const {
    size_t total = hits_ + misses_;
    return total > 0 ? static_cast<float>(hits_) / total : 0.0f;
}

} // namespace ontology
