#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace ontology {

class TripleStore;  // forward declaration for TBox-aware overloads

using String = std::string;
using Json = nlohmann::json;

// ============================================================================
// 类表达式类型 (OWL 2 Class Expression)
// ============================================================================

enum class ExpressionType {
    // 原子类
    Atomic,                    // 命名类 A

    // 类构造子
    Intersection,              // ObjectIntersectionOf (A ⊓ B ⊓ ...)
    Union,                     // ObjectUnionOf (A ⊔ B ⊔ ...)
    Complement,                // ObjectComplementOf (¬A)
    OneOf,                     // ObjectOneOf ({a, b, c})

    // 对象属性限制
    ObjectSomeValuesFrom,      // ObjectSomeValuesFrom (∃R.C)
    ObjectAllValuesFrom,       // ObjectAllValuesFrom (∀R.C)
    ObjectHasValue,            // ObjectHasValue (∃R.{a})
    ObjectHasSelf,             // ObjectHasSelf (∃R.Self)

    // 对象属性基数限制
    ObjectMinCardinality,      // ObjectMinCardinality (≥n R.C)
    ObjectMaxCardinality,      // ObjectMaxCardinality (≤n R.C)
    ObjectExactCardinality,    // ObjectExactCardinality (=n R.C)

    // 数据属性限制
    DataSomeValuesFrom,        // DataSomeValuesFrom (∃R.d)
    DataAllValuesFrom,         // DataAllValuesFrom (∀R.d)
    DataHasValue,              // DataHasValue (∃R.{v})

    // 数据属性基数限制
    DataMinCardinality,        // DataMinCardinality (≥n R)
    DataMaxCardinality,        // DataMaxCardinality (≤n R)
    DataExactCardinality,      // DataExactCardinality (=n R)

    // 特殊类
    Top,                       // owl:Thing (⊤)
    Bottom                     // owl:Nothing (⊥)
};

// ============================================================================
// 类表达式
// ============================================================================

struct ClassExpression {
    ExpressionType type = ExpressionType::Atomic;

    // 原子类名 (Atomic, Top, Bottom)
    String className;

    // 操作数 (Intersection, Union)
    std::vector<std::shared_ptr<ClassExpression>> operands;

    // 补集操作数 (Complement)
    std::shared_ptr<ClassExpression> complementOf;

    // 枚举个体 (OneOf)
    std::vector<String> individuals;

    // 属性限制
    String property;                              // 对象/数据属性
    std::shared_ptr<ClassExpression> filler;      // 填充类表达式
    String value;                                 // HasValue 的值
    int cardinality = 0;                          // 基数

    // 数据范围 (DataSomeValuesFrom, DataAllValuesFrom)
    String dataRange;                             // xsd:string, xsd:integer, etc.

    // 构造函数
    ClassExpression() = default;

    explicit ClassExpression(const String& name)
        : type(ExpressionType::Atomic), className(name) {}

    // 静态工厂方法
    static std::shared_ptr<ClassExpression> atomic(const String& name);
    static std::shared_ptr<ClassExpression> top();
    static std::shared_ptr<ClassExpression> bottom();
    static std::shared_ptr<ClassExpression> intersection(
        const std::vector<std::shared_ptr<ClassExpression>>& ops);
    static std::shared_ptr<ClassExpression> union_(
        const std::vector<std::shared_ptr<ClassExpression>>& ops);
    static std::shared_ptr<ClassExpression> complement(
        std::shared_ptr<ClassExpression> expr);
    static std::shared_ptr<ClassExpression> oneOf(
        const std::vector<String>& individuals);
    static std::shared_ptr<ClassExpression> someValuesFrom(
        const String& property, std::shared_ptr<ClassExpression> filler);
    static std::shared_ptr<ClassExpression> allValuesFrom(
        const String& property, std::shared_ptr<ClassExpression> filler);
    static std::shared_ptr<ClassExpression> hasValue(
        const String& property, const String& value);
    static std::shared_ptr<ClassExpression> minCardinality(
        const String& property, int n, std::shared_ptr<ClassExpression> filler = nullptr);
    static std::shared_ptr<ClassExpression> maxCardinality(
        const String& property, int n, std::shared_ptr<ClassExpression> filler = nullptr);
    static std::shared_ptr<ClassExpression> exactCardinality(
        const String& property, int n, std::shared_ptr<ClassExpression> filler = nullptr);

    // 序列化
    Json toJson() const;
    static std::shared_ptr<ClassExpression> fromJson(const Json& j);

    // 字符串表示 (Manchester OWL 语法)
    String toManchesterString() const;

    // OWL 2 Functional Syntax
    String toFunctionalSyntax() const;

    // 等价判断
    bool isEquivalent(const ClassExpression& other) const;

    // 等价判断 (TBox-aware, uses TripleStore for reasoning)
    bool isEquivalent(const ClassExpression& other, TripleStore* tbox) const;

    // 获取所有引用的类名
    std::unordered_set<String> getClassNames() const;

    // 获取所有引用的属性名
    std::unordered_set<String> getPropertyNames() const;

    // 是否是原子类
    bool isAtomic() const { return type == ExpressionType::Atomic; }

    // 是否是复杂表达式
    bool isComplex() const { return !isAtomic() && type != ExpressionType::Top && type != ExpressionType::Bottom; }
};

// 类型别名
using ClassExpressionPtr = std::shared_ptr<ClassExpression>;

// ============================================================================
// 数据范围 (Data Range)
// ============================================================================

enum class DataRangeType {
    Datatype,           // xsd:string, xsd:integer, etc.
    Intersection,       // DatatypeIntersection
    Union,              // DatatypeUnion
    Complement,         // DatatypeComplement
    OneOf,              // DataOneOf {v1, v2, ...}
    FacetRestriction    // with facets (minInclusive, maxExclusive, etc.)
};

struct DataRange {
    DataRangeType type = DataRangeType::Datatype;
    String datatype;                                    // xsd:string, xsd:integer, etc.
    std::vector<std::shared_ptr<DataRange>> operands;  // Intersection, Union
    std::shared_ptr<DataRange> complementOf;            // Complement
    std::vector<String> values;                         // OneOf

    // Facet 限制
    struct Facet {
        String name;    // minInclusive, maxExclusive, pattern, etc.
        String value;
    };
    std::vector<Facet> facets;
};

using DataRangePtr = std::shared_ptr<DataRange>;

// ============================================================================
// Manchester OWL 语法解析器
// ============================================================================

class ManchesterParser {
public:
    // 解析 Manchester OWL 语法
    static ClassExpressionPtr parse(const String& input);

    // 示例输入:
    // - "Person"
    // - "Person AND Employee"
    // - "Person OR Organization"
    // - "NOT Person"
    // - "hasChild SOME Person"
    // - "hasChild ONLY Person"
    // - "hasChild VALUE john"
    // - "hasChild MIN 2 Person"
    // - "hasChild EXACTLY 1 Person"
    // - "{john, mary, tom}"
    // - "Person AND (hasChild SOME Person) AND (hasAge SOME xsd:integer)"

private:
    struct Token {
        enum Type {
            CLASS,          // 类名
            PROPERTY,       // 属性名
            INDIVIDUAL,     // 个体名
            AND, OR, NOT,   // 逻辑操作
            SOME, ONLY,     // 存在/全称限制
            VALUE, SELF,    // HasValue, HasSelf
            MIN, MAX, EXACTLY,  // 基数限制
            LBRACE, RBRACE, // { }
            LPAREN, RPAREN, // ( )
            COMMA,          // ,
            INTEGER,        // 整数
            DATATYPE,       // 数据类型
            EOF_
        };

        Type type;
        String value;
        int intValue = 0;
    };

    static std::vector<Token> tokenize(const String& input);
    static ClassExpressionPtr parseExpression(std::vector<Token>& tokens, size_t& pos);
    static ClassExpressionPtr parsePrimary(std::vector<Token>& tokens, size_t& pos);
    static ClassExpressionPtr parseRestriction(std::vector<Token>& tokens, size_t& pos, const String& property);
};

// ============================================================================
// 类表达式求值器
// ============================================================================

class ClassExpressionEvaluator {
public:
    // 检查个体是否满足类表达式
    static bool satisfies(
        const String& individualId,
        const ClassExpression& expr,
        const Json& facts,              // 三元组事实
        const Json& classAssertions,    // 类断言
        const Json& propertyAssertions  // 属性断言
    );

    // 获取满足类表达式的所有个体
    static std::vector<String> getIndividuals(
        const ClassExpression& expr,
        const Json& facts,
        const Json& classAssertions,
        const Json& propertyAssertions,
        const std::vector<String>& allIndividuals
    );

    // 检查类表达式是否为空 (unsatisfiable)
    static bool isEmpty(const ClassExpression& expr);

    // 检查类表达式是否为全集
    static bool isUniversal(const ClassExpression& expr);

    // 检查两个类表达式是否包含关系 (C1 ⊑ C2)
    static bool isSubsumedBy(
        const ClassExpression& expr1,
        const ClassExpression& expr2,
        const Json& classHierarchy      // 类层次结构
    );

    // 计算类表达式的规范形式
    static ClassExpressionPtr normalize(const ClassExpression& expr);

    // 计算类表达式的补集
    static ClassExpressionPtr complement(const ClassExpression& expr);

    // 计算两个类表达式的交集
    static ClassExpressionPtr intersection(const ClassExpression& expr1, const ClassExpression& expr2);

    // 计算两个类表达式的并集
    static ClassExpressionPtr union_(const ClassExpression& expr1, const ClassExpression& expr2);

    // TBox-aware overloads (TripleStore provides rdfs:subClassOf, owl:disjointWith, owl:equivalentClass)
    static bool isSubsumedBy(
        const ClassExpression& expr1,
        const ClassExpression& expr2,
        TripleStore* tbox
    );

    static bool isEmpty(const ClassExpression& expr, TripleStore* tbox);
    static bool isUniversal(const ClassExpression& expr, TripleStore* tbox);

    // TBox-aware helper: check if two classes are disjoint
    static bool areDisjoint(const String& classA, const String& classB, TripleStore* tbox);

    // TBox-aware helper: get superclasses (transitive closure of rdfs:subClassOf)
    static std::unordered_set<String> getSuperClasses(const String& className, TripleStore* tbox);
};

// ============================================================================
// OWL 功能语法解析器
// ============================================================================

class FunctionalSyntaxParser {
public:
    // 解析 OWL 2 Functional Syntax
    static ClassExpressionPtr parse(const String& input);

    // 示例输入:
    // - "owl:Thing"
    // - "owl:Nothing"
    // - "ObjectIntersectionOf( :Person :Employee )"
    // - "ObjectUnionOf( :Person :Organization )"
    // - "ObjectComplementOf( :Person )"
    // - "ObjectSomeValuesFrom( :hasChild :Person )"
    // - "ObjectAllValuesFrom( :hasChild :Person )"
    // - "ObjectHasValue( :hasChild :john )"
    // - "ObjectMinCardinality( 2 :hasChild :Person )"
    // - "ObjectOneOf( :john :mary :tom )"
};

} // namespace ontology
