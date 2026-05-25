#include <ontology/ClassExpression.hpp>
#include <ontology/Storage.hpp>
#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace ontology {

// ============================================================================
// 静态工厂方法
// ============================================================================

ClassExpressionPtr ClassExpression::atomic(const String& name) {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = ExpressionType::Atomic;
    expr->className = name;
    return expr;
}

ClassExpressionPtr ClassExpression::top() {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = ExpressionType::Top;
    expr->className = "owl:Thing";
    return expr;
}

ClassExpressionPtr ClassExpression::bottom() {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = ExpressionType::Bottom;
    expr->className = "owl:Nothing";
    return expr;
}

ClassExpressionPtr ClassExpression::intersection(
    const std::vector<ClassExpressionPtr>& ops
) {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = ExpressionType::Intersection;
    expr->operands = ops;
    return expr;
}

ClassExpressionPtr ClassExpression::union_(
    const std::vector<ClassExpressionPtr>& ops
) {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = ExpressionType::Union;
    expr->operands = ops;
    return expr;
}

ClassExpressionPtr ClassExpression::complement(ClassExpressionPtr expr) {
    auto result = std::make_shared<ClassExpression>();
    result->type = ExpressionType::Complement;
    result->complementOf = expr;
    return result;
}

ClassExpressionPtr ClassExpression::oneOf(const std::vector<String>& individuals) {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = ExpressionType::OneOf;
    expr->individuals = individuals;
    return expr;
}

ClassExpressionPtr ClassExpression::someValuesFrom(
    const String& property,
    ClassExpressionPtr filler
) {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = ExpressionType::ObjectSomeValuesFrom;
    expr->property = property;
    expr->filler = filler;
    return expr;
}

ClassExpressionPtr ClassExpression::allValuesFrom(
    const String& property,
    ClassExpressionPtr filler
) {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = ExpressionType::ObjectAllValuesFrom;
    expr->property = property;
    expr->filler = filler;
    return expr;
}

ClassExpressionPtr ClassExpression::hasValue(
    const String& property,
    const String& value
) {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = ExpressionType::ObjectHasValue;
    expr->property = property;
    expr->value = value;
    return expr;
}

ClassExpressionPtr ClassExpression::minCardinality(
    const String& property,
    int n,
    ClassExpressionPtr filler
) {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = ExpressionType::ObjectMinCardinality;
    expr->property = property;
    expr->cardinality = n;
    expr->filler = filler;
    return expr;
}

ClassExpressionPtr ClassExpression::maxCardinality(
    const String& property,
    int n,
    ClassExpressionPtr filler
) {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = ExpressionType::ObjectMaxCardinality;
    expr->property = property;
    expr->cardinality = n;
    expr->filler = filler;
    return expr;
}

ClassExpressionPtr ClassExpression::exactCardinality(
    const String& property,
    int n,
    ClassExpressionPtr filler
) {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = ExpressionType::ObjectExactCardinality;
    expr->property = property;
    expr->cardinality = n;
    expr->filler = filler;
    return expr;
}

// ============================================================================
// 序列化
// ============================================================================

Json ClassExpression::toJson() const {
    Json j;
    j["type"] = static_cast<int>(type);

    switch (type) {
        case ExpressionType::Atomic:
        case ExpressionType::Top:
        case ExpressionType::Bottom:
            j["className"] = className;
            break;

        case ExpressionType::Intersection:
        case ExpressionType::Union:
            j["operands"] = Json::array();
            for (const auto& op : operands) {
                j["operands"].push_back(op->toJson());
            }
            break;

        case ExpressionType::Complement:
            j["complementOf"] = complementOf->toJson();
            break;

        case ExpressionType::OneOf:
            j["individuals"] = individuals;
            break;

        case ExpressionType::ObjectSomeValuesFrom:
        case ExpressionType::ObjectAllValuesFrom:
            j["property"] = property;
            j["filler"] = filler->toJson();
            break;

        case ExpressionType::ObjectHasValue:
            j["property"] = property;
            j["value"] = value;
            break;

        case ExpressionType::ObjectHasSelf:
            j["property"] = property;
            break;

        case ExpressionType::ObjectMinCardinality:
        case ExpressionType::ObjectMaxCardinality:
        case ExpressionType::ObjectExactCardinality:
            j["property"] = property;
            j["cardinality"] = cardinality;
            if (filler) {
                j["filler"] = filler->toJson();
            }
            break;

        case ExpressionType::DataSomeValuesFrom:
        case ExpressionType::DataAllValuesFrom:
            j["property"] = property;
            j["dataRange"] = dataRange;
            break;

        case ExpressionType::DataHasValue:
            j["property"] = property;
            j["value"] = value;
            break;

        case ExpressionType::DataMinCardinality:
        case ExpressionType::DataMaxCardinality:
        case ExpressionType::DataExactCardinality:
            j["property"] = property;
            j["cardinality"] = cardinality;
            break;
    }

    return j;
}

ClassExpressionPtr ClassExpression::fromJson(const Json& j) {
    auto expr = std::make_shared<ClassExpression>();
    expr->type = static_cast<ExpressionType>(j.value("type", 0));

    switch (expr->type) {
        case ExpressionType::Atomic:
        case ExpressionType::Top:
        case ExpressionType::Bottom:
            expr->className = j.value("className", "");
            break;

        case ExpressionType::Intersection:
        case ExpressionType::Union:
            if (j.contains("operands")) {
                for (const auto& op : j["operands"]) {
                    expr->operands.push_back(fromJson(op));
                }
            }
            break;

        case ExpressionType::Complement:
            if (j.contains("complementOf")) {
                expr->complementOf = fromJson(j["complementOf"]);
            }
            break;

        case ExpressionType::OneOf:
            expr->individuals = j.value("individuals", std::vector<String>{});
            break;

        case ExpressionType::ObjectSomeValuesFrom:
        case ExpressionType::ObjectAllValuesFrom:
            expr->property = j.value("property", "");
            if (j.contains("filler")) {
                expr->filler = fromJson(j["filler"]);
            }
            break;

        case ExpressionType::ObjectHasValue:
            expr->property = j.value("property", "");
            expr->value = j.value("value", "");
            break;

        case ExpressionType::ObjectHasSelf:
            expr->property = j.value("property", "");
            break;

        case ExpressionType::ObjectMinCardinality:
        case ExpressionType::ObjectMaxCardinality:
        case ExpressionType::ObjectExactCardinality:
            expr->property = j.value("property", "");
            expr->cardinality = j.value("cardinality", 0);
            if (j.contains("filler")) {
                expr->filler = fromJson(j["filler"]);
            }
            break;

        default:
            break;
    }

    return expr;
}

// ============================================================================
// Manchester OWL 语法字符串
// ============================================================================

String ClassExpression::toManchesterString() const {
    std::ostringstream oss;

    switch (type) {
        case ExpressionType::Atomic:
            oss << className;
            break;

        case ExpressionType::Top:
            oss << "owl:Thing";
            break;

        case ExpressionType::Bottom:
            oss << "owl:Nothing";
            break;

        case ExpressionType::Intersection:
            for (size_t i = 0; i < operands.size(); ++i) {
                if (i > 0) oss << " AND ";
                if (operands[i]->isComplex()) {
                    oss << "(" << operands[i]->toManchesterString() << ")";
                } else {
                    oss << operands[i]->toManchesterString();
                }
            }
            break;

        case ExpressionType::Union:
            for (size_t i = 0; i < operands.size(); ++i) {
                if (i > 0) oss << " OR ";
                if (operands[i]->isComplex()) {
                    oss << "(" << operands[i]->toManchesterString() << ")";
                } else {
                    oss << operands[i]->toManchesterString();
                }
            }
            break;

        case ExpressionType::Complement:
            oss << "NOT ";
            if (complementOf->isComplex()) {
                oss << "(" << complementOf->toManchesterString() << ")";
            } else {
                oss << complementOf->toManchesterString();
            }
            break;

        case ExpressionType::OneOf:
            oss << "{";
            for (size_t i = 0; i < individuals.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << individuals[i];
            }
            oss << "}";
            break;

        case ExpressionType::ObjectSomeValuesFrom:
            oss << property << " SOME ";
            if (filler->isComplex()) {
                oss << "(" << filler->toManchesterString() << ")";
            } else {
                oss << filler->toManchesterString();
            }
            break;

        case ExpressionType::ObjectAllValuesFrom:
            oss << property << " ONLY ";
            if (filler->isComplex()) {
                oss << "(" << filler->toManchesterString() << ")";
            } else {
                oss << filler->toManchesterString();
            }
            break;

        case ExpressionType::ObjectHasValue:
            oss << property << " VALUE " << value;
            break;

        case ExpressionType::ObjectHasSelf:
            oss << property << " SELF";
            break;

        case ExpressionType::ObjectMinCardinality:
            oss << property << " MIN " << cardinality;
            if (filler) {
                oss << " " << filler->toManchesterString();
            }
            break;

        case ExpressionType::ObjectMaxCardinality:
            oss << property << " MAX " << cardinality;
            if (filler) {
                oss << " " << filler->toManchesterString();
            }
            break;

        case ExpressionType::ObjectExactCardinality:
            oss << property << " EXACTLY " << cardinality;
            if (filler) {
                oss << " " << filler->toManchesterString();
            }
            break;

        case ExpressionType::DataSomeValuesFrom:
            oss << property << " SOME " << dataRange;
            break;

        case ExpressionType::DataAllValuesFrom:
            oss << property << " ONLY " << dataRange;
            break;

        case ExpressionType::DataHasValue:
            oss << property << " VALUE " << value;
            break;

        default:
            oss << "Unknown";
    }

    return oss.str();
}

// ============================================================================
// OWL 2 Functional Syntax
// ============================================================================

String ClassExpression::toFunctionalSyntax() const {
    std::ostringstream oss;

    switch (type) {
        case ExpressionType::Atomic:
            oss << ":" << className;
            break;

        case ExpressionType::Top:
            oss << "owl:Thing";
            break;

        case ExpressionType::Bottom:
            oss << "owl:Nothing";
            break;

        case ExpressionType::Intersection:
            oss << "ObjectIntersectionOf(";
            for (size_t i = 0; i < operands.size(); ++i) {
                if (i > 0) oss << " ";
                oss << operands[i]->toFunctionalSyntax();
            }
            oss << ")";
            break;

        case ExpressionType::Union:
            oss << "ObjectUnionOf(";
            for (size_t i = 0; i < operands.size(); ++i) {
                if (i > 0) oss << " ";
                oss << operands[i]->toFunctionalSyntax();
            }
            oss << ")";
            break;

        case ExpressionType::Complement:
            oss << "ObjectComplementOf(" << complementOf->toFunctionalSyntax() << ")";
            break;

        case ExpressionType::OneOf:
            oss << "ObjectOneOf(";
            for (size_t i = 0; i < individuals.size(); ++i) {
                if (i > 0) oss << " ";
                oss << ":" << individuals[i];
            }
            oss << ")";
            break;

        case ExpressionType::ObjectSomeValuesFrom:
            oss << "ObjectSomeValuesFrom(:" << property << " "
                << filler->toFunctionalSyntax() << ")";
            break;

        case ExpressionType::ObjectAllValuesFrom:
            oss << "ObjectAllValuesFrom(:" << property << " "
                << filler->toFunctionalSyntax() << ")";
            break;

        case ExpressionType::ObjectHasValue:
            oss << "ObjectHasValue(:" << property << " :" << value << ")";
            break;

        case ExpressionType::ObjectHasSelf:
            oss << "ObjectHasSelf(:" << property << ")";
            break;

        case ExpressionType::ObjectMinCardinality:
            oss << "ObjectMinCardinality(" << cardinality << " :" << property;
            if (filler) {
                oss << " " << filler->toFunctionalSyntax();
            }
            oss << ")";
            break;

        case ExpressionType::ObjectMaxCardinality:
            oss << "ObjectMaxCardinality(" << cardinality << " :" << property;
            if (filler) {
                oss << " " << filler->toFunctionalSyntax();
            }
            oss << ")";
            break;

        case ExpressionType::ObjectExactCardinality:
            oss << "ObjectExactCardinality(" << cardinality << " :" << property;
            if (filler) {
                oss << " " << filler->toFunctionalSyntax();
            }
            oss << ")";
            break;

        default:
            oss << "Unknown";
    }

    return oss.str();
}

// ============================================================================
// 获取引用的类名和属性名
// ============================================================================

std::unordered_set<String> ClassExpression::getClassNames() const {
    std::unordered_set<String> names;

    switch (type) {
        case ExpressionType::Atomic:
            names.insert(className);
            break;

        case ExpressionType::Intersection:
        case ExpressionType::Union:
            for (const auto& op : operands) {
                auto subNames = op->getClassNames();
                names.insert(subNames.begin(), subNames.end());
            }
            break;

        case ExpressionType::Complement:
            if (complementOf) {
                names = complementOf->getClassNames();
            }
            break;

        case ExpressionType::ObjectSomeValuesFrom:
        case ExpressionType::ObjectAllValuesFrom:
        case ExpressionType::ObjectMinCardinality:
        case ExpressionType::ObjectMaxCardinality:
        case ExpressionType::ObjectExactCardinality:
            if (filler) {
                names = filler->getClassNames();
            }
            break;

        default:
            break;
    }

    return names;
}

std::unordered_set<String> ClassExpression::getPropertyNames() const {
    std::unordered_set<String> names;

    switch (type) {
        case ExpressionType::Intersection:
        case ExpressionType::Union:
            for (const auto& op : operands) {
                auto subNames = op->getPropertyNames();
                names.insert(subNames.begin(), subNames.end());
            }
            break;

        case ExpressionType::Complement:
            if (complementOf) {
                names = complementOf->getPropertyNames();
            }
            break;

        case ExpressionType::ObjectSomeValuesFrom:
        case ExpressionType::ObjectAllValuesFrom:
        case ExpressionType::ObjectHasValue:
        case ExpressionType::ObjectHasSelf:
        case ExpressionType::ObjectMinCardinality:
        case ExpressionType::ObjectMaxCardinality:
        case ExpressionType::ObjectExactCardinality:
        case ExpressionType::DataSomeValuesFrom:
        case ExpressionType::DataAllValuesFrom:
        case ExpressionType::DataHasValue:
        case ExpressionType::DataMinCardinality:
        case ExpressionType::DataMaxCardinality:
        case ExpressionType::DataExactCardinality:
            names.insert(property);
            if (filler) {
                auto subNames = filler->getPropertyNames();
                names.insert(subNames.begin(), subNames.end());
            }
            break;

        default:
            break;
    }

    return names;
}

// ============================================================================
// 等价判断 (简化版)
// ============================================================================

bool ClassExpression::isEquivalent(const ClassExpression& other) const {
    // 完整的等价判断需要推理，这里只做结构等价
    if (type != other.type) return false;

    switch (type) {
        case ExpressionType::Atomic:
        case ExpressionType::Top:
        case ExpressionType::Bottom:
            return className == other.className;

        case ExpressionType::OneOf:
            if (individuals.size() != other.individuals.size()) return false;
            for (const auto& ind : individuals) {
                if (std::find(other.individuals.begin(), other.individuals.end(), ind) == other.individuals.end()) {
                    return false;
                }
            }
            return true;

        case ExpressionType::ObjectHasValue:
            return property == other.property && value == other.value;

        case ExpressionType::ObjectHasSelf:
            return property == other.property;

        case ExpressionType::ObjectMinCardinality:
        case ExpressionType::ObjectMaxCardinality:
        case ExpressionType::ObjectExactCardinality:
            if (property != other.property || cardinality != other.cardinality) return false;
            if (filler && other.filler) {
                return filler->isEquivalent(*other.filler);
            }
            return !filler && !other.filler;

        default:
            // 复杂表达式的等价判断需要推理支持
            return false;
    }
}

bool ClassExpression::isEquivalent(const ClassExpression& other, TripleStore* tbox) const {
    if (!tbox) return isEquivalent(other);  // fall back to structural
    return ClassExpressionEvaluator::isSubsumedBy(*this, other, tbox)
        && ClassExpressionEvaluator::isSubsumedBy(other, *this, tbox);
}

} // namespace ontology
