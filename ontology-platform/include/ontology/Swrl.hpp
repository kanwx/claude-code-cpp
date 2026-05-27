#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "Core.hpp"
#include "Storage.hpp"

namespace ontology {

// 变量绑定类型
using Binding = std::unordered_map<String, String>;
using Bindings = std::vector<Binding>;

// ============================================================================
// SWRL 原子类型
// ============================================================================

enum class SwrlAtomType {
    // 类原子: C(x)
    ClassAtom,

    // 对象属性原子: R(x, y)
    ObjectPropertyAtom,

    // 数据属性原子: P(x, v)
    DataPropertyAtom,

    // 个体值原子: value(x, individual)
    IndividualValueAtom,

    // 数据值原子: value(x, literal)
    DataValueAtom,

    // 内置原子: builtIn(arg1, arg2, ...)
    BuiltInAtom
};

// ============================================================================
// SWRL 变量
// ============================================================================

struct SwrlVariable {
    String name;
    bool isAnonymous = false;

    String toString() const {
        return isAnonymous ? "_" : "?" + name;
    }
};

// ============================================================================
// SWRL 原子
// ============================================================================

struct SwrlAtom {
    SwrlAtomType type;

    // 类原子
    String classId;
    String classArgument;       // 变量或个体名

    // 属性原子
    String propertyId;
    String argument1;           // 第一参数 (变量或个体)
    String argument2;           // 第二参数 (变量、个体或字面量)

    // 内置原子
    String builtInName;
    std::vector<String> builtInArgs;

    // 是否涉及变量
    bool hasVariables() const;

    // 获取所有变量
    std::vector<String> getVariables() const;

    // 字符串表示
    String toString() const;
};

// ============================================================================
// SWRL 规则
// ============================================================================

struct SwrlRule {
    String id;
    String name;
    String description;

    // 规则体: premise -> conclusion
    std::vector<SwrlAtom> body;         // 前提 (IF 部分)
    std::vector<SwrlAtom> head;         // 结论 (THEN 部分)

    // 置信度和优先级
    float confidence = 1.0f;
    float priority = 1.0f;

    // 是否启用
    bool enabled = true;

    // 元数据
    Json metadata;

    // 获取所有变量
    std::vector<String> getAllVariables() const;

    // 字符串表示
    String toString() const;

    // 序列化
    Json toJson() const;
    static SwrlRule fromJson(const Json& j);
};

// ============================================================================
// ProofNode — backward chainer proof tree node (defined here to avoid circular deps)
// ============================================================================

struct ProofNode {
    SwrlAtom goal;                          // The goal being proven
    SwrlRule* matchedRule = nullptr;        // Rule used (null = fact)
    Binding bindings;                        // Variable bindings at this step
    std::vector<ProofNode> subGoals;        // Sub-goals that needed proving
    bool proven = false;                    // Whether this goal was successfully proven

    Json toJson() const;
};

// Forward declaration
class SwrlBackwardChainer;

// ============================================================================
// SWRL 解析器
// ============================================================================

class SwrlParser {
public:
    SwrlParser();

    /// 解析 SWRL 规则字符串
    std::optional<SwrlRule> parse(const String& ruleString);

    /// 解析规则文件
    std::vector<SwrlRule> parseFile(const String& path);

    /// 设置命名空间前缀
    void addPrefix(const String& prefix, const String& iri);

private:
    // 词法分析
    struct Token {
        enum class Type {
            // 关键字
            IF, THEN, AND, OR, NOT,
            // 标点
            LPAREN, RPAREN,      // ( )
            COMMA,               // ,
            ARROW,               // ->
            DOT,                 // .
            // 值
            VARIABLE,            // ?x
            CLASS,               // 类名
            PROPERTY,            // 属性名
            INDIVIDUAL,          // 个体名
            LITERAL,             // 字面量
            BUILTIN,             // swrlb:...
            EOF_
        };

        Type type;
        String value;
    };

    std::vector<Token> tokenize(const String& input);
    SwrlAtom parseAtom(std::vector<Token>& tokens, size_t& pos);
    String resolveName(const String& name);

    // 自然语言解析支持
    bool isNaturalLanguage(const String& text);
    std::optional<SwrlRule> parseNaturalLanguage(const String& text);
    bool parseTransitivityRule(const String& text, SwrlRule& rule);
    bool parseHierarchyRule(const String& text, SwrlRule& rule);
    bool parsePropertyRule(const String& text, SwrlRule& rule);
    bool parseConditionalRule(const String& text, SwrlRule& rule);

    std::unordered_map<String, String> prefixes_;
};

// ============================================================================
// SWRL 执行引擎
// ============================================================================

class SwrlEngine {
public:
    SwrlEngine(StoragePtr storage);

    /// 添加规则
    void addRule(const SwrlRule& rule);
    void removeRule(const String& ruleId);
    void clearRules();

    /// 获取所有规则
    std::vector<SwrlRule> getRules() const;

    /// 执行推理
    /// 返回新推导出的事实
    std::vector<Triple> infer(int maxIterations = 100);

    /// 执行单条规则
    std::vector<Triple> applyRule(const SwrlRule& rule);

    /// 检查规则是否可应用
    bool canApply(const SwrlRule& rule) const;

    /// 设置执行模式
    enum class ExecutionMode {
        Forward,    // 前向链接
        Backward,   // 后向链接
        Hybrid      // 混合
    };
    void setMode(ExecutionMode mode) { mode_ = mode; }

    /// Strategy for infer() — replaces ExecutionMode for new API
    enum class Strategy { ForwardOnly, BackwardOnly, Hybrid };
    void setStrategy(Strategy s);
    Strategy strategy() const;

    /// Backward chaining: prove goal atoms, return all satisfying bindings
    Bindings backwardChain(const std::vector<SwrlAtom>& goal, int maxDepth = 10);

    /// Build proof tree for explanation
    ProofNode explain(const std::vector<SwrlAtom>& goal, int maxDepth = 10);

    /// 设置最大推理步数
    void setMaxIterations(int max) { maxIterations_ = max; }

private:
    // 变量绑定
    using Binding = std::unordered_map<String, String>;
    using Bindings = std::vector<Binding>;

    // 模式匹配
    Bindings matchAtoms(const std::vector<SwrlAtom>& atoms, const Binding& initial = {}) const;

    // 匹配单个原子
    Bindings matchAtom(const SwrlAtom& atom, const Binding& binding) const;

    // 应用绑定到原子
    Triple applyBinding(const SwrlAtom& atom, const Binding& binding);

    // 检查内置条件
    bool checkBuiltIn(const SwrlAtom& atom, const Binding& binding) const;

    // 前向链接推理
    std::vector<Triple> forwardChaining(int maxIterations);

    // 后向链接推理
    std::vector<Triple> backwardChaining(const String& goal, int maxDepth);

    StoragePtr storage_;
    std::unordered_map<String, SwrlRule> rules_;
    ExecutionMode mode_ = ExecutionMode::Forward;
    int maxIterations_ = 100;

    std::unique_ptr<SwrlBackwardChainer> backwardChainer_;
    Strategy strategy_ = Strategy::ForwardOnly;

    // Helper to sync rules to backward chainer
    void updateBackwardChainerRules();
};

// ============================================================================
// SWRL 内置函数库
// ============================================================================

class SwrlBuiltIns {
public:
    /// 比较函数
    static bool equal(const String& a, const String& b);
    static bool notEqual(const String& a, const String& b);
    static bool lessThan(const String& a, const String& b);
    static bool lessThanOrEqual(const String& a, const String& b);
    static bool greaterThan(const String& a, const String& b);
    static bool greaterThanOrEqual(const String& a, const String& b);

    /// 字符串函数
    static bool stringEqualIgnoreCase(const String& a, const String& b);
    static bool stringConcat(const String& a, const String& b, String& result);
    static bool stringLength(const String& s, int& length);
    static bool substring(const String& s, const String& startIndex, const String& len, String& result);
    static bool upperCase(const String& s, String& result);
    static bool lowerCase(const String& s, String& result);
    static bool contains(const String& s, const String& substr);
    static bool startsWith(const String& s, const String& prefix);
    static bool endsWith(const String& s, const String& suffix);
    static bool matches(const String& s, const String& pattern);

    /// 数值函数
    static bool add(const String& a, const String& b, String& result);
    static bool subtract(const String& a, const String& b, String& result);
    static bool multiply(const String& a, const String& b, String& result);
    static bool divide(const String& a, const String& b, String& result);
    static bool mod(const String& a, const String& b, String& result);
    static bool abs(const String& a, String& result);
    static bool sqrt(const String& a, String& result);
    static bool ceiling(const String& a, String& result);
    static bool floor(const String& a, String& result);
    static bool round(const String& a, String& result);

    /// 日期时间函数
    static bool year(const String& date, String& result);
    static bool month(const String& date, String& result);
    static bool day(const String& date, String& result);
    static bool hours(const String& time, String& result);
    static bool minutes(const String& time, String& result);
    static bool seconds(const String& time, String& result);

    /// 列表函数
    static bool list(const std::vector<String>& items, std::vector<String>& result);
    static bool member(const String& item, const std::vector<String>& list);
    static bool length(const std::vector<String>& list, int& len);

    /// 执行内置函数
    static bool execute(
        const String& name,
        const std::vector<String>& args,
        std::vector<String>& results
    );

    /// Temporal functions (Allen algebra)
    static bool temporalBefore(const String& t1, const String& t2);
    static bool temporalAfter(const String& t1, const String& t2);
    static bool temporalOverlaps(const String& t1_start, const String& t1_end,
                                  const String& t2_start, const String& t2_end);
    static bool temporalDuring(const String& t1_start, const String& t1_end,
                                const String& t2_start, const String& t2_end);
    static bool temporalContains(const String& t1_start, const String& t1_end,
                                  const String& t2_start, const String& t2_end);
};

// ============================================================================
// SWRL 解释器 - 生成人类可读的解释
// ============================================================================

class SwrlExplainer {
public:
    /// 生成规则应用解释
    String explainRuleApplication(
        const SwrlRule& rule,
        const Binding& binding,
        const std::vector<Triple>& inferredFacts
    );

    /// 生成推理证明
    String generateProof(
        const Triple& fact,
        const std::vector<SwrlRule>& rules,
        const std::vector<Triple>& facts
    );
};

} // namespace ontology

// Include full SwrlBackwardChainer definition after all SWRL types are defined.
// This avoids circular deps: SwrlBackwardChainer.hpp includes Swrl.hpp, but
// by this point Swrl.hpp's #pragma once guard prevents re-inclusion.
#include "SwrlBackwardChainer.hpp"
