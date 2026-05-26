#pragma once

#include "Core.hpp"
#include "Storage.hpp"
#include "Inference.hpp"
#include "Swrl.hpp"
#include <queue>

namespace ontology {

// ============================================================================
// 自动建模配置
// ============================================================================

struct AutoModelConfig {
    // LLM 配置
    String llmEndpoint = "https://api.openai.com/v1";
    String llmModel = "gpt-4";
    String llmApiKey;
    int maxTokens = 4000;
    float temperature = 0.7f;

    // 抽取配置
    bool extractEntities = true;
    bool extractRelations = true;
    bool extractHierarchy = true;
    bool extractRules = true;
    float confidenceThreshold = 0.7f;

    // 增量学习配置
    bool enableIncrementalLearning = true;
    int minSamplesForInduction = 5;
    float inductionThreshold = 0.8f;
};

// ============================================================================
// 抽取结果
// ============================================================================

struct ExtractionResult {
    // 提取的实体
    struct EntityMention {
        String text;           // 原始文本
        String entityId;       // 推荐ID
        String suggestedClass; // 建议类型
        float confidence;
        int startPos;
        int endPos;
    };
    std::vector<EntityMention> entities;

    // 提取的关系
    struct RelationMention {
        String subject;
        String predicate;
        String object;
        float confidence;
        String evidence;  // 支持证据文本
    };
    std::vector<RelationMention> relations;

    // 提取的类
    struct ClassSuggestion {
        String id;
        String name;
        String description;
        std::vector<String> superClasses;
        std::vector<String> properties;
        float confidence;
    };
    std::vector<ClassSuggestion> classes;

    // 提取的规则
    struct RuleSuggestion {
        String naturalLanguage;  // 自然语言描述
        String swrlRule;         // SWRL格式
        float confidence;
    };
    std::vector<RuleSuggestion> rules;

    // 元信息
    String source;
    float overallConfidence;
};

// ============================================================================
// 本体建议
// ============================================================================

struct OntologySuggestion {
    // 类层次建议
    struct HierarchySuggestion {
        String parent;
        String child;
        float confidence;
        String reason;
    };
    std::vector<HierarchySuggestion> hierarchy;

    // 属性建议
    struct PropertySuggestion {
        String domain;
        String property;
        String range;
        float confidence;
        bool isObjectProperty;  // true=对象属性, false=数据属性
    };
    std::vector<PropertySuggestion> properties;

    // 实例建议
    struct InstanceSuggestion {
        String individualId;
        String classId;
        std::vector<std::pair<String, String>> attributes;
        float confidence;
    };
    std::vector<InstanceSuggestion> instances;

    // 关系建议
    struct RelationSuggestion {
        String subject;
        String predicate;
        String object;
        float confidence;
        String source;
    };
    std::vector<RelationSuggestion> relations;
};

// ============================================================================
// 大模型接口
// ============================================================================

class LLMInterface {
public:
    LLMInterface(const AutoModelConfig& config);
    ~LLMInterface();

    /// 设置 API Key
    void setApiKey(const String& key);
    void setEndpoint(const String& endpoint);
    void setModel(const String& model);

    /// 调用 LLM
    String chat(const String& prompt, const String& systemPrompt = "");
    String complete(const String& prompt);

    /// 结构化输出
    Json chatJson(const String& prompt, const String& systemPrompt = "");

    /// 批量调用
    std::vector<String> batchChat(const std::vector<String>& prompts);

private:
    AutoModelConfig config_;
    String makeRequest(const String& prompt, const String& systemPrompt);
    String httpPost(const String& url, const String& body);
};

// ============================================================================
// 实体抽取器
// ============================================================================

class EntityExtractor {
public:
    EntityExtractor(std::shared_ptr<LLMInterface> llm, StoragePtr storage);

    /// 从文本抽取实体
    ExtractionResult extract(const String& text);

    /// 批量抽取
    std::vector<ExtractionResult> extractBatch(const std::vector<String>& texts);

    /// 设置抽取模板
    void setPromptTemplate(const String& template_);

    /// 添加自定义实体类型
    void addEntityType(const String& type, const String& description);

private:
    std::shared_ptr<LLMInterface> llm_;
    StoragePtr storage_;
    String promptTemplate_;
    std::unordered_map<String, String> entityTypes_;

    String buildPrompt(const String& text);
    ExtractionResult parseResult(const String& llmResponse, const String& source);
};

// ============================================================================
// 关系抽取器
// ============================================================================

class RelationExtractor {
public:
    RelationExtractor(std::shared_ptr<LLMInterface> llm, StoragePtr storage);

    /// 从文本抽取关系
    ExtractionResult extract(const String& text,
                            const std::vector<String>& knownEntities);

    /// 开放关系抽取 (不依赖已知实体)
    ExtractionResult extractOpen(const String& text);

    /// 共指消解 (合并相同实体)
    std::vector<String> resolveCoreference(const std::vector<String>& mentions);

private:
    std::shared_ptr<LLMInterface> llm_;
    StoragePtr storage_;

    String buildPrompt(const String& text, const std::vector<String>& entities);
};

// ============================================================================
// 本体学习器
// ============================================================================

class OntologyLearner {
public:
    OntologyLearner(std::shared_ptr<LLMInterface> llm, StoragePtr storage);

    /// 从文本学习本体
    OntologySuggestion learn(const std::vector<String>& documents);

    /// 学习类层次
    std::vector<OntologySuggestion::HierarchySuggestion> learnHierarchy(
        const std::vector<Class>& existingClasses);

    /// 学习属性
    std::vector<OntologySuggestion::PropertySuggestion> learnProperties(
        const String& classId);

    /// 发现隐含关系
    std::vector<OntologySuggestion::RelationSuggestion> discoverRelations(
        int minSupport = 3);

    /// 增量学习
    void incrementalLearn(const Triple& newTriple);

private:
    std::shared_ptr<LLMInterface> llm_;
    StoragePtr storage_;
    AutoModelConfig config_;

    // 统计支持度
    std::unordered_map<String, int> relationSupport_;
    std::unordered_map<String, std::unordered_map<String, int>> cooccurrence_;

    // 归纳学习
    bool tryInduction(const String& pattern);
};

// ============================================================================
// 规则生成器
// ============================================================================

class RuleGenerator {
public:
    RuleGenerator(std::shared_ptr<LLMInterface> llm, StoragePtr storage);

    /// 从自然语言生成规则
    SwrlRule generateFromNL(const String& description);

    /// 从示例归纳规则
    std::vector<SwrlRule> induceFromExamples(
        const std::vector<Triple>& positiveExamples,
        const std::vector<Triple>& negativeExamples = {});

    /// 自动发现规则
    std::vector<SwrlRule> discoverRules(int minSupport = 5, float minConfidence = 0.8f);

    /// 验证规则
    bool validateRule(const SwrlRule& rule);

private:
    std::shared_ptr<LLMInterface> llm_;
    StoragePtr storage_;

    // 规则模板
    std::vector<String> ruleTemplates_;

    // FOIL 算法归纳
    SwrlRule foilAlgorithm(
        const std::vector<Triple>& positive,
        const std::vector<Triple>& negative);
};

// ============================================================================
// 冲突解决动作
// ============================================================================

struct ConflictAction {
    enum Type { RemoveTriple, AddTriple, ModifyClass };
    Type type;
    String subject;
    String predicate;
    String object;
    String description;
};

// ============================================================================
// 冲突检测结果
// ============================================================================

struct Conflict {
    enum Type {
        DisjointClassAssertion,       // individual typed as both C and D where C ⊓ D ⊑ ⊥
        FunctionalPropertyViolation,  // subject has multiple values for functional property
        Inconsistency                 // DlReasoner detected inconsistency
    };
    Type type;
    String description;
    std::vector<Triple> conflictingTriples;
    float severity = 1.0f;  // 0.0-1.0
};

// ============================================================================
// 数据来源追踪
// ============================================================================

struct Provenance {
    String sourceId;       // ontology/document ID
    String sourceName;     // human-readable name
    float confidence = 1.0f;
    String timestamp;      // ISO 8601 when this triple was added
};

// ============================================================================
// 实体对齐结果
// ============================================================================

struct AlignmentResult {
    String entity1;
    String entity2;
    float embeddingScore;   // cosine similarity
    float structuralScore;  // Jaccard of shared properties
    float labelScore;       // normalized Levenshtein
    float combinedScore;    // weighted combination
};

// ============================================================================
// 自动建模引擎
// ============================================================================

class AutoModelEngine {
public:
    AutoModelEngine(StoragePtr storage, const AutoModelConfig& config = {});
    ~AutoModelEngine();

    /// 初始化 LLM 连接
    bool initialize(const String& apiKey, const String& endpoint = "");

    // ===== 文本驱动的建模 =====

    /// 从文本构建本体
    ExtractionResult buildFromText(const String& text);

    /// 从文档集合构建本体
    std::vector<ExtractionResult> buildFromDocuments(
        const std::vector<String>& documents);

    /// 从结构化数据构建
    ExtractionResult buildFromStructured(const Json& data);

    // ===== 交互式建模 =====

    /// 回答建模问题
    String answerQuestion(const String& question);

    /// 获取建模建议
    OntologySuggestion getSuggestions();

    /// 解释推理结果
    String explainInference(const Triple& result);

    // ===== 增量学习 =====

    /// 添加新知识并更新模型
    void addKnowledge(const Triple& triple, bool retrain = false);

    /// 添加新类并更新层次
    void addClass(const Class& cls, bool suggestHierarchy = true);

    /// 批量导入并学习
    int importAndLearn(const std::vector<Triple>& triples);

    // ===== 自动发现 =====

    /// 发现新实体
    std::vector<Individual> discoverEntities(int limit = 100);

    /// 发现新关系
    std::vector<std::pair<String, float>> discoverRelations(int limit = 50);

    /// 解决冲突
    void resolveConflict(const String& conflictId, bool dryRun = false);

private:
    String getClassList();

    /// 发现类层次
    std::vector<std::pair<String, String>> discoverHierarchy();

    /// 发现 SWRL 规则
    std::vector<SwrlRule> discoverRules(int minSupport = 5);

    // ===== 模型优化 =====

    /// 训练嵌入
    void trainEmbeddings(int epochs = 100, float lr = 0.01f);

    /// 优化本体
    std::vector<String> optimize();

    /// 检测冲突 (TBox-aware)
    std::vector<Conflict> detectConflicts();

    // ===== 知识融合 =====

    /// 实体对齐 (multi-signal)
    std::vector<AlignmentResult> alignEntities(
        const std::vector<String>& entities1,
        const std::vector<String>& entities2);

    /// 本体融合 (provenance-aware)
    void mergeOntologies(const std::vector<Triple>& externalTriples,
                         const String& sourceId = "external",
                         const String& sourceName = "External Ontology");

    // ===== 导出与解释 =====

    /// 生成自然语言描述
    String describeOntology();

    /// 生成文档
    String generateDocumentation();

    /// 导出训练数据
    std::vector<std::pair<String, String>> exportTrainingData();

    // ===== 获取组件 =====

    std::shared_ptr<LLMInterface> getLLM() { return llm_; }
    std::shared_ptr<EntityExtractor> getEntityExtractor() { return entityExtractor_; }
    std::shared_ptr<RelationExtractor> getRelationExtractor() { return relationExtractor_; }
    std::shared_ptr<OntologyLearner> getOntologyLearner() { return ontologyLearner_; }
    std::shared_ptr<RuleGenerator> getRuleGenerator() { return ruleGenerator_; }

private:
    StoragePtr storage_;
    AutoModelConfig config_;

    std::shared_ptr<LLMInterface> llm_;
    std::shared_ptr<EntityExtractor> entityExtractor_;
    std::shared_ptr<RelationExtractor> relationExtractor_;
    std::shared_ptr<OntologyLearner> ontologyLearner_;
    std::shared_ptr<RuleGenerator> ruleGenerator_;
    std::shared_ptr<NeuralReasoner> neuralReasoner_;

    // Conflict action parsing
    std::vector<ConflictAction> parseConflictActions(const String& llmResponse);

    // 学习状态
    bool llmInitialized_ = false;
    bool embeddingsTrained_ = false;
    std::queue<Triple> pendingLearning_;  // 待学习的三元组

    // Provenance tracking: triple hash -> provenance
    std::unordered_map<size_t, Provenance> provenanceIndex_;

    // Alignment weights
    float alignWeightEmbedding_ = 0.5f;
    float alignWeightStructural_ = 0.3f;
    float alignWeightLabel_ = 0.2f;

    // Helper: compute triple hash
    static size_t tripleHash(const Triple& t);

    // Helper: compute Levenshtein distance
    static int levenshteinDistance(const String& s1, const String& s2);

    // Helper: compute Jaccard coefficient
    float jaccardCoefficient(const String& entity1, const String& entity2) const;
};

// ============================================================================
// 便捷函数
// ============================================================================

/// 快速从文本建模
ExtractionResult quickBuild(StoragePtr storage, const String& text,
                           const String& apiKey = "");

/// 从自然语言查询
std::vector<Triple> naturalLanguageQuery(StoragePtr storage,
                                         const String& query,
                                         const String& apiKey = "");

} // namespace ontology
