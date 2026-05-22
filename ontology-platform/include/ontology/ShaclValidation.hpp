#pragma once

#include "Core.hpp"
#include "Storage.hpp"
#include "Inference.hpp"
#include "Temporal.hpp"
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace ontology {

// ============================================================================
// SHACL 形状约束验证引擎
// 对标: Apache Jena SHACL, pySHACL, TopBraid SHACL API
// 实现 W3C SHACL (Shapes Constraint Language) 核心验证功能
// ============================================================================

/// SHACL 验证结果严重性
enum class Severity {
    Info, Warning, Violation
};

/// SHACL 验证结果
struct ShaclValidationResult {
    String shapeId;          // 触发的形状
    String constraintId;     // 触发的约束
    String focusNode;        // 被验证的节点
    String path;             // 属性路径
    String value;            // 导致违规的值
    String message;          // 人类可读消息
    Severity severity = Severity::Violation;
    String sourceConstraint; // 源约束组件

    Json toJson() const {
        Json j;
        j["shape"] = shapeId;
        j["constraint"] = constraintId;
        j["focusNode"] = focusNode;
        j["path"] = path;
        j["value"] = value;
        j["message"] = message;
        j["severity"] = severity == Severity::Violation ? "violation" :
                        severity == Severity::Warning ? "warning" : "info";
        return j;
    }
};

/// SHACL 属性约束
struct ShaclPropertyShape {
    String id;
    String path;                 // 属性路径 (关系/属性 ID)

    // Cardinality constraints
    int minCount = -1;           // 最小出现次数 (-1 = 无约束)
    int maxCount = -1;           // 最大出现次数 (-1 = 无约束)

    // Value type constraints
    String datatype;             // 期望的数据类型
    String classId;              // 期望的类 (sh:class)
    std::vector<String> inValues; // 枚举值列表 (sh:in)
    bool hasValue = false;       // 是否有固定值约束
    String fixedValue;           // 固定值 (sh:hasValue)

    // Value range constraints
    std::optional<double> minValue;    // 最小值 (sh:minInclusive)
    std::optional<double> maxValue;    // 最大值 (sh:maxInclusive)
    std::optional<double> exclusiveMinValue; // 最小值 (sh:minExclusive)
    std::optional<double> exclusiveMaxValue; // 最大值 (sh:maxExclusive)
    int minLength = -1;          // 最小字符串长度 (sh:minLength)
    int maxLength = -1;          // 最大字符串长度 (sh:maxLength)
    String pattern;              // 正则表达式 (sh:pattern)
    String patternFlags;         // 正则标志 (sh:flags)

    // Node constraints
    String nodeKind;             // sh:nodeKind: "IRI", "Literal", "BlankNode", "IRIOrLiteral"
    bool uniqueLang = false;     // sh:uniqueLang

    // Logical constraints
    std::vector<String> andShapes;  // sh:and
    std::vector<String> orShapes;   // sh:or
    String notShape;                // sh:not
    std::vector<String> qualifiedValueShapes; // sh:qualifiedValueShape
    int qualifiedMinCount = -1;
    int qualifiedMaxCount = -1;

    Severity severity = Severity::Violation;
    String message;              // 自定义消息

    Json toJson() const {
        Json j;
        j["id"] = id;
        j["path"] = path;
        if (minCount >= 0) j["minCount"] = minCount;
        if (maxCount >= 0) j["maxCount"] = maxCount;
        if (!datatype.empty()) j["datatype"] = datatype;
        if (!classId.empty()) j["class"] = classId;
        if (!inValues.empty()) j["in"] = inValues;
        if (minLength >= 0) j["minLength"] = minLength;
        if (maxLength >= 0) j["maxLength"] = maxLength;
        if (!pattern.empty()) j["pattern"] = pattern;
        if (!message.empty()) j["message"] = message;
        return j;
    }
};

/// SHACL 节点形状
struct ShaclNodeShape {
    String id;
    String targetClass;          // sh:targetClass
    std::vector<String> targetNodes; // sh:targetNode
    std::vector<String> targetSubjectsOf; // sh:targetSubjectsOf
    std::vector<String> targetObjectsOf;  // sh:targetObjectsOf

    // Node-level constraints
    bool closed = false;         // sh:closed
    std::vector<String> ignoredProperties; // sh:ignoredProperties (if closed)
    bool deactivated = false;    // sh:deactivated

    // Property shapes
    std::vector<ShaclPropertyShape> properties;

    // Logical constraints at node level
    std::vector<String> andShapes;
    std::vector<String> orShapes;
    String notShape;
    String xoneShape;            // sh:xone (exactly one of)

    Severity severity = Severity::Violation;
    String message;

    Json toJson() const {
        Json j;
        j["id"] = id;
        j["targetClass"] = targetClass;
        j["closed"] = closed;
        Json props = Json::array();
        for (const auto& p : properties) props.push_back(p.toJson());
        j["properties"] = props;
        return j;
    }

    static ShaclNodeShape fromJson(const Json& j) {
        ShaclNodeShape shape;
        shape.id = j.value("id", "");
        shape.targetClass = j.value("targetClass", "");
        shape.closed = j.value("closed", false);
        shape.notShape = j.value("notShape", "");
        shape.xoneShape = j.value("xoneShape", "");

        if (j.contains("targetNodes") && j["targetNodes"].is_array()) {
            for (const auto& v : j["targetNodes"]) shape.targetNodes.push_back(v.get<String>());
        }
        if (j.contains("targetSubjectsOf") && j["targetSubjectsOf"].is_array()) {
            for (const auto& v : j["targetSubjectsOf"]) shape.targetSubjectsOf.push_back(v.get<String>());
        }
        if (j.contains("targetObjectsOf") && j["targetObjectsOf"].is_array()) {
            for (const auto& v : j["targetObjectsOf"]) shape.targetObjectsOf.push_back(v.get<String>());
        }
        if (j.contains("andShapes") && j["andShapes"].is_array()) {
            for (const auto& v : j["andShapes"]) shape.andShapes.push_back(v.get<String>());
        }
        if (j.contains("orShapes") && j["orShapes"].is_array()) {
            for (const auto& v : j["orShapes"]) shape.orShapes.push_back(v.get<String>());
        }
        if (j.contains("ignoredProperties") && j["ignoredProperties"].is_array()) {
            for (const auto& v : j["ignoredProperties"]) shape.ignoredProperties.push_back(v.get<String>());
        }

        if (j.contains("properties") && j["properties"].is_array()) {
            for (const auto& pj : j["properties"]) {
                ShaclPropertyShape ps;
                ps.id = pj.value("id", "");
                ps.path = pj.value("path", "");
                ps.minCount = pj.value("minCount", -1);
                ps.maxCount = pj.value("maxCount", -1);
                ps.datatype = pj.value("datatype", "");
                ps.classId = pj.value("class", "");
                ps.hasValue = pj.value("hasValue", false);
                ps.fixedValue = pj.value("fixedValue", "");
                ps.uniqueLang = pj.value("uniqueLang", false);
                ps.minLength = pj.value("minLength", -1);
                ps.maxLength = pj.value("maxLength", -1);
                ps.pattern = pj.value("pattern", "");
                ps.patternFlags = pj.value("patternFlags", "");
                ps.nodeKind = pj.value("nodeKind", "");
                ps.message = pj.value("message", "");
                ps.qualifiedMinCount = pj.value("qualifiedMinCount", -1);
                ps.qualifiedMaxCount = pj.value("qualifiedMaxCount", -1);
                if (pj.contains("in") && pj["in"].is_array()) {
                    for (const auto& v : pj["in"]) ps.inValues.push_back(v.get<String>());
                }
                if (pj.contains("minValue")) ps.minValue = pj["minValue"].get<double>();
                if (pj.contains("maxValue")) ps.maxValue = pj["maxValue"].get<double>();
                if (pj.contains("exclusiveMinValue")) ps.exclusiveMinValue = pj["exclusiveMinValue"].get<double>();
                if (pj.contains("exclusiveMaxValue")) ps.exclusiveMaxValue = pj["exclusiveMaxValue"].get<double>();
                if (pj.contains("qualifiedValueShapes") && pj["qualifiedValueShapes"].is_array()) {
                    for (const auto& v : pj["qualifiedValueShapes"]) ps.qualifiedValueShapes.push_back(v.get<String>());
                }
                shape.properties.push_back(ps);
            }
        }
        return shape;
    }
};

/// SHACL 验证报告
struct ShaclValidationReport {
    bool conforms = true;
    std::vector<ShaclValidationResult> results;

    int violationCount() const {
        int c = 0;
        for (const auto& r : results) if (r.severity == Severity::Violation) c++;
        return c;
    }
    int warningCount() const {
        int c = 0;
        for (const auto& r : results) if (r.severity == Severity::Warning) c++;
        return c;
    }

    Json toJson() const {
        Json j;
        j["conforms"] = conforms;
        Json arr = Json::array();
        for (const auto& r : results) arr.push_back(r.toJson());
        j["results"] = arr;
        j["violations"] = violationCount();
        j["warnings"] = warningCount();
        return j;
    }
};

class ShaclValidator {
public:
    explicit ShaclValidator(StoragePtr storage);

    /// 添加/删除形状
    void addShape(const ShaclNodeShape& shape);
    void removeShape(const String& shapeId);
    std::vector<ShaclNodeShape> getShapes() const;

    /// 从 JSON 加载形状
    void loadShapesFromJson(const Json& shapes);

    /// 验证整个本体
    ShaclValidationReport validate();

    /// 验证特定节点
    ShaclValidationReport validateNode(const String& nodeId);

    /// 验证特定类的所有实例
    ShaclValidationReport validateClass(const String& classId);

    /// 增量验证: 验证受影响的节点 (给定变更集)
    ShaclValidationReport validateIncremental(
        const std::vector<Triple>& addedTriples,
        const std::vector<Triple>& removedTriples
    );

private:
    StoragePtr storage_;
    std::unordered_map<String, ShaclNodeShape> shapes_;
    mutable std::mutex mutex_;

    // Core validation logic
    std::vector<ShaclValidationResult> validateProperty(
        const String& focusNode,
        const ShaclPropertyShape& property,
        const ShaclNodeShape& shape
    );

    // Cardinality validation
    std::vector<ShaclValidationResult> validateCardinality(
        const String& focusNode,
        const ShaclPropertyShape& property
    );

    // Value type validation
    std::vector<ShaclValidationResult> validateValueType(
        const String& focusNode,
        const ShaclPropertyShape& property
    );

    // Range validation
    std::vector<ShaclValidationResult> validateRange(
        const String& focusNode,
        const ShaclPropertyShape& property
    );

    // Pattern validation
    std::vector<ShaclValidationResult> validatePattern(
        const String& focusNode,
        const ShaclPropertyShape& property
    );

    // Closed shape validation
    std::vector<ShaclValidationResult> validateClosed(
        const String& focusNode,
        const ShaclNodeShape& shape
    );

    // Qualified value shape validation
    std::vector<ShaclValidationResult> validateQualifiedValueShape(
        const String& focusNode,
        const ShaclPropertyShape& property
    );

    // Logical constraint validation (and/or/not/xone)
    std::vector<ShaclValidationResult> validateLogicalConstraints(
        const String& focusNode,
        const ShaclNodeShape& shape
    );

    // Helper: get all values of a property for a node
    std::vector<String> getPropertyValues(
        const String& nodeId,
        const String& propertyPath
    ) const;

    // Helper: get affected nodes from a triple change
    std::vector<String> getAffectedNodes(
        const std::vector<Triple>& added,
        const std::vector<Triple>& removed
    ) const;
};

// ============================================================================
// 增量推理缓存
// 对标: Apache Jena TDB inference cache, RDF4J incremental reasoning
// ============================================================================

class IncrementalReasoner {
public:
    struct Config {
        int maxCacheSize = 10000;
        int cacheTtlSeconds = 3600;     // 缓存TTL
        bool enableForwardChaining = true;
        bool enableBackwardChaining = false;
        int maxInferenceDepth = 5;
        bool validateWithShacl = true;
    };

    struct InferenceDelta {
        std::vector<Triple> addedFacts;     // 新增推理事实
        std::vector<Triple> removedFacts;   // 需要撤回的推理事实
        std::vector<ShaclValidationResult> violations; // 新增违规
        Json metadata;
    };

    IncrementalReasoner(
        StoragePtr storage,
        std::shared_ptr<SymbolicReasoner> symbolic = nullptr,
        std::shared_ptr<ShaclValidator> shaclValidator = nullptr
    );

    /// 处理增量变更
    InferenceDelta processChange(
        const std::vector<Triple>& addedTriples,
        const std::vector<Triple>& removedTriples
    );

    /// 添加三元组并触发增量推理
    InferenceDelta addTriple(const Triple& triple);

    /// 删除三元组并触发增量推理
    InferenceDelta removeTriple(const Triple& triple);

    /// 获取推理缓存统计
    Json getCacheStats() const;

    /// 清空缓存
    void clearCache();

    /// 设置配置
    void setConfig(const Config& config) { config_ = config; }

private:
    Config config_;
    StoragePtr storage_;
    std::shared_ptr<SymbolicReasoner> symbolic_;
    std::shared_ptr<ShaclValidator> shaclValidator_;

    // 真值维护系统: 支持基于正当性的增量推理回溯
    TruthMaintenanceSystem tms_;

    // 推理缓存: (subject, predicate) -> inferred objects
    std::unordered_map<String, std::vector<std::pair<String, float>>> inferenceCache_;
    // 反向索引: object -> sources (for retraction)
    std::unordered_map<String, std::vector<String>> justificationIndex_;
    mutable std::mutex mutex_;

    // Forward chaining
    std::vector<Triple> forwardChain(const std::vector<Triple>& triggers);
    std::vector<Triple> applyAxiom(const Triple& trigger, const Axiom& axiom);

    // Retraction (TMS-based: 级联撤回依赖的派生事实)
    std::vector<Triple> retract(const std::vector<Triple>& removed);

    // Cache management
    void updateCache(const String& subject, const String& predicate,
                     const std::vector<std::pair<String, float>>& values);
    std::optional<std::vector<std::pair<String, float>>> getCached(
        const String& subject, const String& predicate) const;
    void invalidateCache(const String& subject, const String& predicate);
};

} // namespace ontology
