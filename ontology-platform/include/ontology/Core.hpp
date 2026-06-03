#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <algorithm>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <stack>
#include <nlohmann/json.hpp>

namespace ontology {

using Json = nlohmann::json;
using String = std::string;

// ============================================================================
// 类表达式前置声明
// ============================================================================

struct ClassExpression;
using ClassExpressionPtr = std::shared_ptr<ClassExpression>;

// ============================================================================
// 核心类型定义
// ============================================================================

/// 概念/类 (支持复杂类表达式)
struct Class {
    String id;
    String name;
    String description;

    // 基本类关系 (向后兼容)
    std::vector<String> superClasses;       // Cache: populated from subClassOf triples. Use storage->getSuperClasses(id) for traversal.
    std::vector<String> equivalentClasses; // 等价类
    std::vector<String> disjointClasses;   // 不相交类
    std::vector<String> properties;        // 属性定义

    // OWL 2 类表达式 (新增)
    ClassExpressionPtr definition;         // 类定义: C ≡ Expression
    std::vector<ClassExpressionPtr> superExpressions; // C ⊑ Expression
    std::vector<ClassExpressionPtr> equivalentExpressions; // C ≡ Expression

    Json metadata;

    bool isSubClassOf(const String& classId) const {
        return std::find(superClasses.begin(), superClasses.end(), classId) != superClasses.end();
    }

    // 是否有复杂定义
    bool hasComplexDefinition() const {
        return definition != nullptr || !superExpressions.empty() || !equivalentExpressions.empty();
    }

    Json toJson() const {
        Json j;
        j["id"] = id;
        j["name"] = name;
        j["description"] = description;
        j["superClasses"] = Json::array();
        for (const auto& s : superClasses) {
            j["superClasses"].push_back(s);
        }
        if (!validFrom.empty()) j["validFrom"] = validFrom;
        if (!validTo.empty()) j["validTo"] = validTo;
        return j;
    }

    // 时态有效性 (ISO 8601, 空值 = 永久有效)
    String validFrom;
    String validTo;

    bool isValidAt(const String& timestamp) const {
        if (validFrom.empty() && validTo.empty()) return true;
        if (!validFrom.empty() && timestamp < validFrom) return false;
        if (!validTo.empty() && timestamp > validTo) return false;
        return true;
    }
    bool isAlwaysValid() const { return validFrom.empty() && validTo.empty(); }
};

/// 关系/属性
struct Relation {
    String id;
    String name;
    String description;

    String domain;              // 定义域
    String range;               // 值域

    // 关系特性
    bool isFunctional = false;      // 函数性 (一对一)
    bool isInverseFunctional = false;
    bool isTransitive = false;      // 传递性
    bool isSymmetric = false;       // 对称性
    bool isReflexive = false;       // 自反性
    bool isAntisymmetric = false;   // 反对称性

    String inverseProperty;         // 逆关系
    std::vector<String> superProperties; // 父关系

    // 神经嵌入
    std::vector<float> embedding;

    Json metadata;

    Json toJson() const {
        Json j;
        j["id"] = id;
        j["name"] = name;
        j["description"] = description;
        j["domain"] = domain;
        j["range"] = range;
        j["isFunctional"] = isFunctional;
        j["isInverseFunctional"] = isInverseFunctional;
        j["isTransitive"] = isTransitive;
        j["isSymmetric"] = isSymmetric;
        j["isReflexive"] = isReflexive;
        j["isAntisymmetric"] = isAntisymmetric;
        if (!inverseProperty.empty()) j["inverseProperty"] = inverseProperty;
        if (!superProperties.empty()) j["superProperties"] = superProperties;
        if (!embedding.empty()) j["embedding"] = embedding;
        if (!metadata.is_null()) j["metadata"] = metadata;
        return j;
    }
};

/// 属性定义
struct Property {
    String id;
    String name;
    String description;

    enum class DataType {
        String, Integer, Float, Boolean,
        DateTime, Date, Duration,
        URI, Any, Custom
    } dataType;

    String customType;
    bool isRequired = false;
    bool isUnique = false;
    String defaultValue;

    std::vector<String> enumValues;
    std::optional<double> minValue;
    std::optional<double> maxValue;

    Json metadata;
};

/// 实例/个体
struct Individual {
    String id;
    String name;
    String classId;             // 类型

    std::unordered_map<String, Json> properties;      // 数据属性
    std::unordered_map<String, std::vector<String>> relations; // 对象关系

    // 神经嵌入 (向量表示)
    std::vector<float> embedding;

    float importance = 1.0f;     // 重要性权重
    Json metadata;

    // 时态有效性
    String validFrom;
    String validTo;

    bool isValidAt(const String& timestamp) const {
        if (validFrom.empty() && validTo.empty()) return true;
        if (!validFrom.empty() && timestamp < validFrom) return false;
        if (!validTo.empty() && timestamp > validTo) return false;
        return true;
    }
    bool isAlwaysValid() const { return validFrom.empty() && validTo.empty(); }

    // 辅助方法
    std::vector<String> getRelated(const String& relationId) const {
        auto it = relations.find(relationId);
        return it != relations.end() ? it->second : std::vector<String>{};
    }

    template<typename T>
    std::optional<T> getProperty(const String& propId) const {
        auto it = properties.find(propId);
        if (it != properties.end()) {
            try {
                return it->second.get<T>();
            } catch (const std::exception&) { return std::nullopt; }
        }
        return std::nullopt;
    }
};

/// 三元组 (知识的基本单元)
struct Triple {
    String subject;
    String predicate;
    String object;
    bool isLiteral = false;

    float confidence = 1.0f;
    float weight = 1.0f;
    String source;
    String provenance;          // 来源追溯

    // 神经相关
    std::vector<float> embedding; // 三元组嵌入

    // 时态有效性 (ISO 8601, 空值 = 永久有效)
    String validFrom;
    String validTo;
    String recordedAt;  // 事务时间: 何时被记录到系统中

    bool isValidAt(const String& timestamp) const {
        if (validFrom.empty() && validTo.empty()) return true;
        if (!validFrom.empty() && timestamp < validFrom) return false;
        if (!validTo.empty() && timestamp > validTo) return false;
        return true;
    }
    bool isAlwaysValid() const { return validFrom.empty() && validTo.empty(); }

    String toString() const {
        return subject + " --[" + predicate + "]--> " + object;
    }

    Json toJson() const {
        Json j = {
            {"subject", subject},
            {"predicate", predicate},
            {"object", object},
            {"isLiteral", isLiteral},
            {"confidence", confidence}
        };
        if (weight != 1.0f) j["weight"] = weight;
        if (!source.empty()) j["source"] = source;
        if (!provenance.empty()) j["provenance"] = provenance;
        if (!validFrom.empty()) j["validFrom"] = validFrom;
        if (!validTo.empty()) j["validTo"] = validTo;
        if (!embedding.empty()) j["embedding"] = embedding;
        return j;
    }
};

/// 公理/规则
struct Axiom {
    String id;
    String description;

    enum class Type {
        SubClassOf,         // A ⊑ B
        EquivalentClass,    // A ≡ B
        DisjointClass,      // A ⊓ B ⊑ ⊥
        SubProperty,        // R ⊑ S
        Domain,             // ∃R.⊤ ⊑ A
        Range,              // ⊤ ⊑ ∀R.A
        Transitive,         // R⁺ ⊑ R
        Functional,         // ⊤ ⊑ ≤1R
        Inverse,            // R ≡ S⁻
        Symmetric,          // R ≡ R⁻
        Reflexive,          // ⊤ ⊑ R(?,?)
        Irreflexive,        // ⊤ ⊑ ¬R(?,?)
        Antisymmetric,      // R(x,y) ∧ R(y,x) → x=y
        HasValue,           // ⊤ ⊑ R(?,v)
        AllValuesFrom,      // ⊤ ⊑ ∀R.A
        SomeValuesFrom,     // ⊤ ⊑ ∃R.A
        CustomSWRL,         // SWRL 规则
        CustomDL            // 描述逻辑表达式
    } type;

    String premise;         // 前提 (JSON/DL 表达式)
    String conclusion;      // 结论

    float confidence = 1.0f;
    float priority = 1.0f;  // 规则优先级

    Json metadata;

    // 时态有效性
    String validFrom;
    String validTo;

    bool isValidAt(const String& timestamp) const {
        if (validFrom.empty() && validTo.empty()) return true;
        if (!validFrom.empty() && timestamp < validFrom) return false;
        if (!validTo.empty() && timestamp > validTo) return false;
        return true;
    }
    bool isAlwaysValid() const { return validFrom.empty() && validTo.empty(); }
};

/// 查询定义
struct Query {
    String text;            // 原始查询文本

    // 结构化查询
    std::vector<String> selectClasses;
    std::vector<String> selectRelations;
    std::vector<String> selectIndividuals;

    // 约束条件
    struct Constraint {
        String subject;
        String predicate;
        String object;
        String op;          // =, !=, <, >, <=, >=, contains, regex
        Json value;
    };
    std::vector<Constraint> constraints;

    // 推理选项
    bool enableInference = true;
    int inferenceDepth = 2;
    int limit = 100;
    int offset = 0;
};

/// 查询结果
struct QueryResult {
    std::vector<Individual> individuals;
    std::vector<Triple> triples;
    std::vector<String> inferredFacts;

    float score = 0.0f;
    String explanation;
    Json metadata;

    bool empty() const {
        return individuals.empty() && triples.empty() && inferredFacts.empty();
    }

    size_t size() const {
        return individuals.size() + triples.size();
    }
};

// ============================================================================
// 本体定义 (完整本体)
// ============================================================================

struct Ontology {
    String id;
    String name;
    String description;
    String version;
    String baseIRI;

    // 核心组件
    std::unordered_map<String, Class> classes;
    std::unordered_map<String, Relation> relations;
    std::unordered_map<String, Property> properties;
    std::unordered_map<String, Individual> individuals;
    std::unordered_map<String, Axiom> axioms;

    // 三元组存储
    std::vector<Triple> triples;

    // 索引
    std::unordered_map<String, std::vector<String>> classIndex;       // classId -> individualIds
    std::unordered_map<String, std::vector<size_t>> subjectIndex;     // subject -> triple indices
    std::unordered_map<String, std::vector<size_t>> predicateIndex;   // predicate -> triple indices
    std::unordered_map<String, std::vector<size_t>> objectIndex;      // object -> triple indices

    // 统计
    size_t classCount() const { return classes.size(); }
    size_t relationCount() const { return relations.size(); }
    size_t individualCount() const { return individuals.size(); }
    size_t tripleCount() const { return triples.size(); }
    size_t axiomCount() const { return axioms.size(); }

    // 序列化
    Json toJson() const;
    static std::optional<Ontology> fromJson(const Json& j);
};

} // namespace ontology
