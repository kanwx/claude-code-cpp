#include <ontology/ClassExpression.hpp>
#include <ontology/Storage.hpp>
#include <sstream>
#include <cctype>
#include <stdexcept>

namespace ontology {

// ============================================================================
// Manchester OWL 语法解析器实现
// ============================================================================

ClassExpressionPtr ManchesterParser::parse(const String& input) {
    auto tokens = tokenize(input);
    size_t pos = 0;
    return parseExpression(tokens, pos);
}

std::vector<ManchesterParser::Token> ManchesterParser::tokenize(const String& input) {
    std::vector<Token> tokens;
    std::istringstream iss(input);
    String word;

    while (iss >> word) {
        Token tok;
        tok.value = word;

        // 转换为大写进行比较
        String upper = word;
        for (char& c : upper) c = std::toupper(c);

        if (upper == "AND" || upper == "&&") {
            tok.type = Token::AND;
        } else if (upper == "OR" || upper == "||") {
            tok.type = Token::OR;
        } else if (upper == "NOT" || upper == "!") {
            tok.type = Token::NOT;
        } else if (upper == "SOME") {
            tok.type = Token::SOME;
        } else if (upper == "ONLY") {
            tok.type = Token::ONLY;
        } else if (upper == "VALUE") {
            tok.type = Token::VALUE;
        } else if (upper == "SELF") {
            tok.type = Token::SELF;
        } else if (upper == "MIN") {
            tok.type = Token::MIN;
        } else if (upper == "MAX") {
            tok.type = Token::MAX;
        } else if (upper == "EXACTLY") {
            tok.type = Token::EXACTLY;
        } else if (word == "{") {
            tok.type = Token::LBRACE;
        } else if (word == "}") {
            tok.type = Token::RBRACE;
        } else if (word == "(") {
            tok.type = Token::LPAREN;
        } else if (word == ")") {
            tok.type = Token::RPAREN;
        } else if (word == ",") {
            tok.type = Token::COMMA;
        } else if (word == "owl:Thing" || word == "Thing") {
            tok.type = Token::CLASS;
            tok.value = "owl:Thing";
        } else if (word == "owl:Nothing" || word == "Nothing") {
            tok.type = Token::CLASS;
            tok.value = "owl:Nothing";
        } else {
            // 检查是否为整数
            bool isInt = true;
            for (char c : word) {
                if (!std::isdigit(c) && c != '-') {
                    isInt = false;
                    break;
                }
            }

            if (isInt && !word.empty()) {
                tok.type = Token::INTEGER;
                tok.intValue = std::stoi(word);
            } else if (word.find("xsd:") == 0) {
                tok.type = Token::DATATYPE;
            } else if (word.find(":") == 0) {
                // 带前缀的名称 :ClassName
                tok.type = Token::CLASS;
                tok.value = word.substr(1);
            } else {
                // 默认为类名
                tok.type = Token::CLASS;
            }
        }

        tokens.push_back(tok);
    }

    tokens.push_back({Token::EOF_, "", 0});
    return tokens;
}

ClassExpressionPtr ManchesterParser::parseExpression(
    std::vector<Token>& tokens,
    size_t& pos
) {
    // 解析 OR 表达式 (最低优先级)
    auto left = parsePrimary(tokens, pos);

    while (pos < tokens.size() && tokens[pos].type == Token::OR) {
        pos++; // skip OR
        auto right = parsePrimary(tokens, pos);
        left = ClassExpression::union_({left, right});
    }

    // 解析 AND 表达式
    // 注意: Manchester 语法中 AND 优先级高于 OR
    // 这里简化处理，AND 和 OR 左结合

    return left;
}

ClassExpressionPtr ManchesterParser::parsePrimary(
    std::vector<Token>& tokens,
    size_t& pos
) {
    if (pos >= tokens.size()) {
        throw std::runtime_error("Unexpected end of input");
    }

    const auto& tok = tokens[pos];

    switch (tok.type) {
        case Token::CLASS:
            pos++;
            if (tok.value == "owl:Thing") {
                return ClassExpression::top();
            } else if (tok.value == "owl:Nothing") {
                return ClassExpression::bottom();
            }
            return ClassExpression::atomic(tok.value);

        case Token::NOT:
            pos++;
            return ClassExpression::complement(parsePrimary(tokens, pos));

        case Token::LPAREN: {
            pos++; // skip (
            auto expr = parseExpression(tokens, pos);
            if (pos >= tokens.size() || tokens[pos].type != Token::RPAREN) {
                throw std::runtime_error("Expected )");
            }
            pos++; // skip )
            return expr;
        }

        case Token::LBRACE: {
            // OneOf {a, b, c}
            pos++; // skip {
            std::vector<String> individuals;
            while (pos < tokens.size() && tokens[pos].type != Token::RBRACE) {
                if (tokens[pos].type == Token::CLASS || tokens[pos].type == Token::INDIVIDUAL) {
                    individuals.push_back(tokens[pos].value);
                    pos++;
                } else if (tokens[pos].type == Token::COMMA) {
                    pos++; // skip ,
                } else {
                    throw std::runtime_error("Expected individual in OneOf");
                }
            }
            if (pos >= tokens.size() || tokens[pos].type != Token::RBRACE) {
                throw std::runtime_error("Expected }");
            }
            pos++; // skip }
            return ClassExpression::oneOf(individuals);
        }

        case Token::PROPERTY: {
            // 属性限制
            String property = tok.value;
            pos++;
            return parseRestriction(tokens, pos, property);
        }

        default:
            // 尝试作为属性名处理
            if (tok.type == Token::CLASS) {
                // 可能是属性限制的开头，回退作为属性处理
                String property = tok.value;
                pos++;
                return parseRestriction(tokens, pos, property);
            }
            throw std::runtime_error("Unexpected token: " + tok.value);
    }
}

ClassExpressionPtr ManchesterParser::parseRestriction(
    std::vector<Token>& tokens,
    size_t& pos,
    const String& property
) {
    if (pos >= tokens.size()) {
        throw std::runtime_error("Expected restriction keyword");
    }

    const auto& tok = tokens[pos];

    switch (tok.type) {
        case Token::SOME: {
            pos++; // skip SOME
            auto filler = parsePrimary(tokens, pos);
            return ClassExpression::someValuesFrom(property, filler);
        }

        case Token::ONLY: {
            pos++; // skip ONLY
            auto filler = parsePrimary(tokens, pos);
            return ClassExpression::allValuesFrom(property, filler);
        }

        case Token::VALUE: {
            pos++; // skip VALUE
            if (pos >= tokens.size()) {
                throw std::runtime_error("Expected value after VALUE");
            }
            String value = tokens[pos].value;
            pos++;
            return ClassExpression::hasValue(property, value);
        }

        case Token::SELF: {
            pos++; // skip SELF
            auto expr = std::make_shared<ClassExpression>();
            expr->type = ExpressionType::ObjectHasSelf;
            expr->property = property;
            return expr;
        }

        case Token::MIN: {
            pos++; // skip MIN
            if (pos >= tokens.size() || tokens[pos].type != Token::INTEGER) {
                throw std::runtime_error("Expected integer after MIN");
            }
            int n = tokens[pos].intValue;
            pos++;
            ClassExpressionPtr filler;
            if (pos < tokens.size() && tokens[pos].type != Token::AND &&
                tokens[pos].type != Token::OR && tokens[pos].type != Token::RPAREN &&
                tokens[pos].type != Token::EOF_) {
                filler = parsePrimary(tokens, pos);
            }
            return ClassExpression::minCardinality(property, n, filler);
        }

        case Token::MAX: {
            pos++; // skip MAX
            if (pos >= tokens.size() || tokens[pos].type != Token::INTEGER) {
                throw std::runtime_error("Expected integer after MAX");
            }
            int n = tokens[pos].intValue;
            pos++;
            ClassExpressionPtr filler;
            if (pos < tokens.size() && tokens[pos].type != Token::AND &&
                tokens[pos].type != Token::OR && tokens[pos].type != Token::RPAREN &&
                tokens[pos].type != Token::EOF_) {
                filler = parsePrimary(tokens, pos);
            }
            return ClassExpression::maxCardinality(property, n, filler);
        }

        case Token::EXACTLY: {
            pos++; // skip EXACTLY
            if (pos >= tokens.size() || tokens[pos].type != Token::INTEGER) {
                throw std::runtime_error("Expected integer after EXACTLY");
            }
            int n = tokens[pos].intValue;
            pos++;
            ClassExpressionPtr filler;
            if (pos < tokens.size() && tokens[pos].type != Token::AND &&
                tokens[pos].type != Token::OR && tokens[pos].type != Token::RPAREN &&
                tokens[pos].type != Token::EOF_) {
                filler = parsePrimary(tokens, pos);
            }
            return ClassExpression::exactCardinality(property, n, filler);
        }

        default:
            // 不是限制，返回原子类
            return ClassExpression::atomic(property);
    }
}

// ============================================================================
// OWL 2 Functional Syntax 解析器实现
// ============================================================================

ClassExpressionPtr FunctionalSyntaxParser::parse(const String& input) {
    // 简化实现：支持基本 OWL 2 Functional Syntax
    // 完整实现需要完整的词法分析器和递归下降解析器

    String trimmed = input;
    // 去除前后空格
    while (!trimmed.empty() && std::isspace(trimmed.front())) trimmed.erase(0, 1);
    while (!trimmed.empty() && std::isspace(trimmed.back())) trimmed.pop_back();

    // owl:Thing
    if (trimmed == "owl:Thing") {
        return ClassExpression::top();
    }

    // owl:Nothing
    if (trimmed == "owl:Nothing") {
        return ClassExpression::bottom();
    }

    // 原子类 (带冒号前缀)
    if (trimmed.find(":") == 0) {
        return ClassExpression::atomic(trimmed.substr(1));
    }

    // ObjectIntersectionOf(...)
    if (trimmed.find("ObjectIntersectionOf(") == 0) {
        // 提取参数
        size_t start = 20; // "ObjectIntersectionOf(" 的长度
        size_t end = trimmed.rfind(")");
        if (end == String::npos) {
            throw std::runtime_error("Missing ) in ObjectIntersectionOf");
        }

        String content = trimmed.substr(start, end - start);
        // 简化：按空格分割（实际需要处理嵌套括号）
        std::vector<ClassExpressionPtr> operands;
        std::istringstream iss(content);
        String token;
        while (iss >> token) {
            operands.push_back(parse(token));
        }

        return ClassExpression::intersection(operands);
    }

    // ObjectUnionOf(...)
    if (trimmed.find("ObjectUnionOf(") == 0) {
        size_t start = 14;
        size_t end = trimmed.rfind(")");
        if (end == String::npos) {
            throw std::runtime_error("Missing ) in ObjectUnionOf");
        }

        String content = trimmed.substr(start, end - start);
        std::vector<ClassExpressionPtr> operands;
        std::istringstream iss(content);
        String token;
        while (iss >> token) {
            operands.push_back(parse(token));
        }

        return ClassExpression::union_(operands);
    }

    // ObjectComplementOf(...)
    if (trimmed.find("ObjectComplementOf(") == 0) {
        size_t start = 19;
        size_t end = trimmed.rfind(")");
        if (end == String::npos) {
            throw std::runtime_error("Missing ) in ObjectComplementOf");
        }

        String content = trimmed.substr(start, end - start);
        return ClassExpression::complement(parse(content));
    }

    // ObjectSomeValuesFrom(:property :Class)
    if (trimmed.find("ObjectSomeValuesFrom(") == 0) {
        size_t start = 21;
        size_t end = trimmed.rfind(")");
        if (end == String::npos) {
            throw std::runtime_error("Missing ) in ObjectSomeValuesFrom");
        }

        String content = trimmed.substr(start, end - start);
        std::istringstream iss(content);
        String propStr, fillerStr;
        iss >> propStr >> fillerStr;

        // 去除冒号前缀
        String property = (propStr.find(":") == 0) ? propStr.substr(1) : propStr;

        return ClassExpression::someValuesFrom(property, parse(fillerStr));
    }

    // ObjectAllValuesFrom(:property :Class)
    if (trimmed.find("ObjectAllValuesFrom(") == 0) {
        size_t start = 20;
        size_t end = trimmed.rfind(")");
        if (end == String::npos) {
            throw std::runtime_error("Missing ) in ObjectAllValuesFrom");
        }

        String content = trimmed.substr(start, end - start);
        std::istringstream iss(content);
        String propStr, fillerStr;
        iss >> propStr >> fillerStr;

        String property = (propStr.find(":") == 0) ? propStr.substr(1) : propStr;

        return ClassExpression::allValuesFrom(property, parse(fillerStr));
    }

    // ObjectHasValue(:property :individual)
    if (trimmed.find("ObjectHasValue(") == 0) {
        size_t start = 15;
        size_t end = trimmed.rfind(")");
        if (end == String::npos) {
            throw std::runtime_error("Missing ) in ObjectHasValue");
        }

        String content = trimmed.substr(start, end - start);
        std::istringstream iss(content);
        String propStr, valStr;
        iss >> propStr >> valStr;

        String property = (propStr.find(":") == 0) ? propStr.substr(1) : propStr;
        String value = (valStr.find(":") == 0) ? valStr.substr(1) : valStr;

        return ClassExpression::hasValue(property, value);
    }

    // ObjectOneOf(:a :b :c)
    if (trimmed.find("ObjectOneOf(") == 0) {
        size_t start = 12;
        size_t end = trimmed.rfind(")");
        if (end == String::npos) {
            throw std::runtime_error("Missing ) in ObjectOneOf");
        }

        String content = trimmed.substr(start, end - start);
        std::vector<String> individuals;
        std::istringstream iss(content);
        String token;
        while (iss >> token) {
            individuals.push_back((token.find(":") == 0) ? token.substr(1) : token);
        }

        return ClassExpression::oneOf(individuals);
    }

    // ObjectMinCardinality(n :property :Class)
    if (trimmed.find("ObjectMinCardinality(") == 0) {
        size_t start = 21;
        size_t end = trimmed.rfind(")");
        if (end == String::npos) {
            throw std::runtime_error("Missing ) in ObjectMinCardinality");
        }

        String content = trimmed.substr(start, end - start);
        std::istringstream iss(content);
        int n;
        String propStr;
        iss >> n >> propStr;

        String property = (propStr.find(":") == 0) ? propStr.substr(1) : propStr;

        String fillerStr;
        if (iss >> fillerStr) {
            return ClassExpression::minCardinality(property, n, parse(fillerStr));
        }
        return ClassExpression::minCardinality(property, n);
    }

    // ObjectMaxCardinality(n :property :Class)
    if (trimmed.find("ObjectMaxCardinality(") == 0) {
        size_t start = 21;
        size_t end = trimmed.rfind(")");
        if (end == String::npos) {
            throw std::runtime_error("Missing ) in ObjectMaxCardinality");
        }

        String content = trimmed.substr(start, end - start);
        std::istringstream iss(content);
        int n;
        String propStr;
        iss >> n >> propStr;

        String property = (propStr.find(":") == 0) ? propStr.substr(1) : propStr;

        String fillerStr;
        if (iss >> fillerStr) {
            return ClassExpression::maxCardinality(property, n, parse(fillerStr));
        }
        return ClassExpression::maxCardinality(property, n);
    }

    // ObjectExactCardinality(n :property :Class)
    if (trimmed.find("ObjectExactCardinality(") == 0) {
        size_t start = 23;
        size_t end = trimmed.rfind(")");
        if (end == String::npos) {
            throw std::runtime_error("Missing ) in ObjectExactCardinality");
        }

        String content = trimmed.substr(start, end - start);
        std::istringstream iss(content);
        int n;
        String propStr;
        iss >> n >> propStr;

        String property = (propStr.find(":") == 0) ? propStr.substr(1) : propStr;

        String fillerStr;
        if (iss >> fillerStr) {
            return ClassExpression::exactCardinality(property, n, parse(fillerStr));
        }
        return ClassExpression::exactCardinality(property, n);
    }

    // 默认为原子类
    return ClassExpression::atomic(trimmed);
}

// ============================================================================
// 类表达式求值器实现
// ============================================================================

bool ClassExpressionEvaluator::satisfies(
    const String& individualId,
    const ClassExpression& expr,
    const Json& facts,
    const Json& classAssertions,
    const Json& propertyAssertions
) {
    switch (expr.type) {
        case ExpressionType::Top:
            return true; // 所有个体都属于 owl:Thing

        case ExpressionType::Bottom:
            return false; // 没有个体属于 owl:Nothing

        case ExpressionType::Atomic: {
            // 检查个体是否被断言为该类的实例
            if (classAssertions.contains(expr.className)) {
                for (const auto& ind : classAssertions[expr.className]) {
                    if (ind.get<String>() == individualId) {
                        return true;
                    }
                }
            }
            return false;
        }

        case ExpressionType::Intersection: {
            // 个体必须满足所有操作数
            for (const auto& op : expr.operands) {
                if (!satisfies(individualId, *op, facts, classAssertions, propertyAssertions)) {
                    return false;
                }
            }
            return true;
        }

        case ExpressionType::Union: {
            // 个体至少满足一个操作数
            for (const auto& op : expr.operands) {
                if (satisfies(individualId, *op, facts, classAssertions, propertyAssertions)) {
                    return true;
                }
            }
            return false;
        }

        case ExpressionType::Complement: {
            // 个体不满足补集操作数
            return !satisfies(individualId, *expr.complementOf, facts, classAssertions, propertyAssertions);
        }

        case ExpressionType::OneOf: {
            // 个体是否在枚举列表中
            for (const auto& ind : expr.individuals) {
                if (ind == individualId) {
                    return true;
                }
            }
            return false;
        }

        case ExpressionType::ObjectSomeValuesFrom: {
            // 存在通过 property 连接的个体满足 filler
            if (propertyAssertions.contains(expr.property)) {
                for (const auto& assertion : propertyAssertions[expr.property]) {
                    if (assertion["subject"].get<String>() == individualId) {
                        String related = assertion["object"].get<String>();
                        if (satisfies(related, *expr.filler, facts, classAssertions, propertyAssertions)) {
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        case ExpressionType::ObjectAllValuesFrom: {
            // 所有通过 property 连接的个体都满足 filler
            if (propertyAssertions.contains(expr.property)) {
                for (const auto& assertion : propertyAssertions[expr.property]) {
                    if (assertion["subject"].get<String>() == individualId) {
                        String related = assertion["object"].get<String>();
                        if (!satisfies(related, *expr.filler, facts, classAssertions, propertyAssertions)) {
                            return false;
                        }
                    }
                }
            }
            return true;
        }

        case ExpressionType::ObjectHasValue: {
            // 存在通过 property 连接到指定个体
            if (propertyAssertions.contains(expr.property)) {
                for (const auto& assertion : propertyAssertions[expr.property]) {
                    if (assertion["subject"].get<String>() == individualId &&
                        assertion["object"].get<String>() == expr.value) {
                        return true;
                    }
                }
            }
            return false;
        }

        case ExpressionType::ObjectMinCardinality: {
            // 至少有 n 个通过 property 连接的个体满足 filler
            int count = 0;
            if (propertyAssertions.contains(expr.property)) {
                for (const auto& assertion : propertyAssertions[expr.property]) {
                    if (assertion["subject"].get<String>() == individualId) {
                        String related = assertion["object"].get<String>();
                        if (!expr.filler || satisfies(related, *expr.filler, facts, classAssertions, propertyAssertions)) {
                            count++;
                        }
                    }
                }
            }
            return count >= expr.cardinality;
        }

        case ExpressionType::ObjectMaxCardinality: {
            // 最多有 n 个通过 property 连接的个体满足 filler
            int count = 0;
            if (propertyAssertions.contains(expr.property)) {
                for (const auto& assertion : propertyAssertions[expr.property]) {
                    if (assertion["subject"].get<String>() == individualId) {
                        String related = assertion["object"].get<String>();
                        if (!expr.filler || satisfies(related, *expr.filler, facts, classAssertions, propertyAssertions)) {
                            count++;
                        }
                    }
                }
            }
            return count <= expr.cardinality;
        }

        case ExpressionType::ObjectExactCardinality: {
            // 恰好有 n 个通过 property 连接的个体满足 filler
            int count = 0;
            if (propertyAssertions.contains(expr.property)) {
                for (const auto& assertion : propertyAssertions[expr.property]) {
                    if (assertion["subject"].get<String>() == individualId) {
                        String related = assertion["object"].get<String>();
                        if (!expr.filler || satisfies(related, *expr.filler, facts, classAssertions, propertyAssertions)) {
                            count++;
                        }
                    }
                }
            }
            return count == expr.cardinality;
        }

        default:
            return false;
    }
}

std::vector<String> ClassExpressionEvaluator::getIndividuals(
    const ClassExpression& expr,
    const Json& facts,
    const Json& classAssertions,
    const Json& propertyAssertions,
    const std::vector<String>& allIndividuals
) {
    std::vector<String> result;

    for (const auto& ind : allIndividuals) {
        if (satisfies(ind, expr, facts, classAssertions, propertyAssertions)) {
            result.push_back(ind);
        }
    }

    return result;
}

bool ClassExpressionEvaluator::isEmpty(const ClassExpression& expr) {
    // 简化检测
    return expr.type == ExpressionType::Bottom;
}

bool ClassExpressionEvaluator::isUniversal(const ClassExpression& expr) {
    return expr.type == ExpressionType::Top;
}

ClassExpressionPtr ClassExpressionEvaluator::normalize(const ClassExpression& expr) {
    switch (expr.type) {
        case ExpressionType::Atomic:
        case ExpressionType::Top:
        case ExpressionType::Bottom:
            // 原子和常量已规范化
            return std::make_shared<ClassExpression>(expr);

        case ExpressionType::Complement: {
            // 规范化补集，应用双重否定消除
            auto normalizedInner = normalize(*expr.complementOf);
            if (normalizedInner->type == ExpressionType::Complement) {
                // ¬¬A = A
                return normalizedInner->complementOf;
            }
            auto result = ClassExpression::complement(normalizedInner);
            // 应用 De Morgan 定律
            return normalize(*result);
        }

        case ExpressionType::Intersection: {
            // 规范化交集，展平嵌套的交集，移除 Top，检查 Bottom
            std::vector<ClassExpressionPtr> normalizedOperands;
            for (const auto& op : expr.operands) {
                auto normalized = normalize(*op);
                if (normalized->type == ExpressionType::Bottom) {
                    // A ⊓ ⊥ = ⊥
                    return ClassExpression::bottom();
                }
                if (normalized->type == ExpressionType::Top) {
                    // A ⊓ ⊤ = A，跳过
                    continue;
                }
                if (normalized->type == ExpressionType::Intersection) {
                    // 展平嵌套的交集
                    for (const auto& nested : normalized->operands) {
                        normalizedOperands.push_back(nested);
                    }
                } else {
                    normalizedOperands.push_back(normalized);
                }
            }
            if (normalizedOperands.empty()) {
                return ClassExpression::top();
            }
            if (normalizedOperands.size() == 1) {
                return normalizedOperands[0];
            }
            // 检查重复和互补对
            std::sort(normalizedOperands.begin(), normalizedOperands.end(),
                [](const auto& a, const auto& b) { return a->toFunctionalSyntax() < b->toFunctionalSyntax(); });
            normalizedOperands.erase(
                std::unique(normalizedOperands.begin(), normalizedOperands.end(),
                    [](const auto& a, const auto& b) { return a->toFunctionalSyntax() == b->toFunctionalSyntax(); }),
                normalizedOperands.end());
            // 检查 A 和 ¬A 同时存在
            for (size_t i = 0; i < normalizedOperands.size(); ++i) {
                if (normalizedOperands[i]->type == ExpressionType::Complement) {
                    for (size_t j = 0; j < normalizedOperands.size(); ++j) {
                        if (i != j && normalizedOperands[i]->complementOf->toFunctionalSyntax() == normalizedOperands[j]->toFunctionalSyntax()) {
                            return ClassExpression::bottom();
                        }
                    }
                }
            }
            return ClassExpression::intersection(normalizedOperands);
        }

        case ExpressionType::Union: {
            // 规范化并集，展平嵌套的并集，移除 Bottom，检查 Top
            std::vector<ClassExpressionPtr> normalizedOperands;
            for (const auto& op : expr.operands) {
                auto normalized = normalize(*op);
                if (normalized->type == ExpressionType::Top) {
                    // A ⊔ ⊤ = ⊤
                    return ClassExpression::top();
                }
                if (normalized->type == ExpressionType::Bottom) {
                    // A ⊔ ⊥ = A，跳过
                    continue;
                }
                if (normalized->type == ExpressionType::Union) {
                    // 展平嵌套的并集
                    for (const auto& nested : normalized->operands) {
                        normalizedOperands.push_back(nested);
                    }
                } else {
                    normalizedOperands.push_back(normalized);
                }
            }
            if (normalizedOperands.empty()) {
                return ClassExpression::bottom();
            }
            if (normalizedOperands.size() == 1) {
                return normalizedOperands[0];
            }
            // 移除重复
            std::sort(normalizedOperands.begin(), normalizedOperands.end(),
                [](const auto& a, const auto& b) { return a->toFunctionalSyntax() < b->toFunctionalSyntax(); });
            normalizedOperands.erase(
                std::unique(normalizedOperands.begin(), normalizedOperands.end(),
                    [](const auto& a, const auto& b) { return a->toFunctionalSyntax() == b->toFunctionalSyntax(); }),
                normalizedOperands.end());
            return ClassExpression::union_(normalizedOperands);
        }

        case ExpressionType::ObjectSomeValuesFrom: {
            // 规范化 ∃R.C
            auto normalizedFiller = normalize(*expr.filler);
            if (normalizedFiller->type == ExpressionType::Bottom) {
                // ∃R.⊥ = ⊥
                return ClassExpression::bottom();
            }
            return ClassExpression::someValuesFrom(expr.property, normalizedFiller);
        }

        case ExpressionType::ObjectAllValuesFrom: {
            // 规范化 ∀R.C
            auto normalizedFiller = normalize(*expr.filler);
            if (normalizedFiller->type == ExpressionType::Top) {
                // ∀R.⊤ = ⊤
                return ClassExpression::top();
            }
            return ClassExpression::allValuesFrom(expr.property, normalizedFiller);
        }

        case ExpressionType::ObjectHasValue:
            // ∃R.{a} 已规范化
            return std::make_shared<ClassExpression>(expr);

        case ExpressionType::ObjectMinCardinality: {
            // 规范化 ≥n R.C
            if (expr.cardinality == 0) {
                return ClassExpression::top();
            }
            auto normalizedFiller = normalize(*expr.filler);
            return ClassExpression::minCardinality(expr.property, expr.cardinality, normalizedFiller);
        }

        case ExpressionType::ObjectMaxCardinality: {
            // 规范化 ≤n R.C
            auto normalizedFiller = normalize(*expr.filler);
            return ClassExpression::maxCardinality(expr.property, expr.cardinality, normalizedFiller);
        }

        case ExpressionType::ObjectExactCardinality: {
            // =n R.C 转换为 (≥n R.C) ⊓ (≤n R.C)
            auto normalizedFiller = normalize(*expr.filler);
            auto min = ClassExpression::minCardinality(expr.property, expr.cardinality, normalizedFiller);
            auto max = ClassExpression::maxCardinality(expr.property, expr.cardinality, normalizedFiller);
            return normalize(*ClassExpression::intersection({min, max}));
        }

        case ExpressionType::DataSomeValuesFrom:
        case ExpressionType::DataAllValuesFrom:
        case ExpressionType::DataHasValue:
        case ExpressionType::OneOf:
            // 数据属性和枚举类已规范化
            return std::make_shared<ClassExpression>(expr);

        default:
            return std::make_shared<ClassExpression>(expr);
    }
}

ClassExpressionPtr ClassExpressionEvaluator::complement(const ClassExpression& expr) {
    // 双重否定消除
    if (expr.type == ExpressionType::Complement) {
        return expr.complementOf;
    }

    // De Morgan 定律
    if (expr.type == ExpressionType::Intersection) {
        std::vector<ClassExpressionPtr> negated;
        for (const auto& op : expr.operands) {
            negated.push_back(complement(*op));
        }
        return ClassExpression::union_(negated);
    }

    if (expr.type == ExpressionType::Union) {
        std::vector<ClassExpressionPtr> negated;
        for (const auto& op : expr.operands) {
            negated.push_back(complement(*op));
        }
        return ClassExpression::intersection(negated);
    }

    // Top 和 Bottom 互为补集
    if (expr.type == ExpressionType::Top) {
        return ClassExpression::bottom();
    }
    if (expr.type == ExpressionType::Bottom) {
        return ClassExpression::top();
    }

    // 默认：取补集
    return ClassExpression::complement(std::make_shared<ClassExpression>(expr));
}

ClassExpressionPtr ClassExpressionEvaluator::intersection(
    const ClassExpression& expr1,
    const ClassExpression& expr2
) {
    // 简化规则
    if (isEmpty(expr1) || isEmpty(expr2)) {
        return ClassExpression::bottom();
    }

    if (isUniversal(expr1)) {
        return std::make_shared<ClassExpression>(expr2);
    }

    if (isUniversal(expr2)) {
        return std::make_shared<ClassExpression>(expr1);
    }

    // 如果两者都是交集，合并操作数
    if (expr1.type == ExpressionType::Intersection && expr2.type == ExpressionType::Intersection) {
        std::vector<ClassExpressionPtr> ops = expr1.operands;
        ops.insert(ops.end(), expr2.operands.begin(), expr2.operands.end());
        return ClassExpression::intersection(ops);
    }

    if (expr1.type == ExpressionType::Intersection) {
        std::vector<ClassExpressionPtr> ops = expr1.operands;
        ops.push_back(std::make_shared<ClassExpression>(expr2));
        return ClassExpression::intersection(ops);
    }

    if (expr2.type == ExpressionType::Intersection) {
        std::vector<ClassExpressionPtr> ops = expr2.operands;
        ops.push_back(std::make_shared<ClassExpression>(expr1));
        return ClassExpression::intersection(ops);
    }

    // 默认
    return ClassExpression::intersection({
        std::make_shared<ClassExpression>(expr1),
        std::make_shared<ClassExpression>(expr2)
    });
}

ClassExpressionPtr ClassExpressionEvaluator::union_(
    const ClassExpression& expr1,
    const ClassExpression& expr2
) {
    // 简化规则
    if (isUniversal(expr1) || isUniversal(expr2)) {
        return ClassExpression::top();
    }

    if (isEmpty(expr1)) {
        return std::make_shared<ClassExpression>(expr2);
    }

    if (isEmpty(expr2)) {
        return std::make_shared<ClassExpression>(expr1);
    }

    // 如果两者都是并集，合并操作数
    if (expr1.type == ExpressionType::Union && expr2.type == ExpressionType::Union) {
        std::vector<ClassExpressionPtr> ops = expr1.operands;
        ops.insert(ops.end(), expr2.operands.begin(), expr2.operands.end());
        return ClassExpression::union_(ops);
    }

    // 默认
    return ClassExpression::union_({
        std::make_shared<ClassExpression>(expr1),
        std::make_shared<ClassExpression>(expr2)
    });
}

bool ClassExpressionEvaluator::isSubsumedBy(
    const ClassExpression& expr1,
    const ClassExpression& expr2,
    const Json& classHierarchy
) {
    // C ⊑ D 等价于 C ⊓ ¬D 为空
    // 简化实现：只处理基本类层次

    if (isUniversal(expr2) || isEmpty(expr1)) {
        return true;
    }

    if (expr1.type == ExpressionType::Atomic && expr2.type == ExpressionType::Atomic) {
        // 检查类层次
        if (classHierarchy.contains(expr1.className)) {
            for (const auto& super : classHierarchy[expr1.className]) {
                if (super.get<String>() == expr2.className) {
                    return true;
                }
            }
        }
        return expr1.className == expr2.className;
    }

    if (expr1.type == ExpressionType::Intersection) {
        // A ⊓ B ⊑ C 如果 A ⊑ C 或 B ⊑ C
        for (const auto& op : expr1.operands) {
            if (isSubsumedBy(*op, expr2, classHierarchy)) {
                return true;
            }
        }
    }

    if (expr2.type == ExpressionType::Union) {
        // C ⊑ A ⊔ B 如果 C ⊑ A 或 C ⊑ B
        for (const auto& op : expr2.operands) {
            if (isSubsumedBy(expr1, *op, classHierarchy)) {
                return true;
            }
        }
    }

    return false;
}

// ============================================================================
// TBox-aware ClassExpressionEvaluator overloads
// ============================================================================

std::unordered_set<String> ClassExpressionEvaluator::getSuperClasses(
    const String& className, TripleStore* tbox)
{
    std::unordered_set<String> supers;
    if (!tbox) return supers;

    std::vector<String> queue = {className};
    while (!queue.empty()) {
        String current = queue.back();
        queue.pop_back();
        auto triples = tbox->findBySP(
            current, "http://www.w3.org/2000/01/rdf-schema#subClassOf");
        for (const auto& t : triples) {
            if (supers.insert(t.object).second) {
                queue.push_back(t.object);
            }
        }
    }
    return supers;
}

bool ClassExpressionEvaluator::areDisjoint(
    const String& classA, const String& classB, TripleStore* tbox)
{
    if (!tbox) return false;
    auto fwd = tbox->findBySP(classA, "http://www.w3.org/2002/07/owl#disjointWith");
    for (const auto& t : fwd) {
        if (t.object == classB) return true;
    }
    auto rev = tbox->findBySP(classB, "http://www.w3.org/2002/07/owl#disjointWith");
    for (const auto& t : rev) {
        if (t.object == classA) return true;
    }
    return false;
}

bool ClassExpressionEvaluator::isSubsumedBy(
    const ClassExpression& expr1, const ClassExpression& expr2, TripleStore* tbox)
{
    // X subsumes X always
    if (&expr1 == &expr2) return true;
    if (expr1.type == expr2.type && expr1.className == expr2.className
        && expr1.type == ExpressionType::Atomic) return true;

    // Bottom subsumes anything
    if (expr1.type == ExpressionType::Bottom) return true;
    // anything subsumes Top
    if (expr2.type == ExpressionType::Top) return true;

    switch (expr1.type) {
        case ExpressionType::Atomic: {
            // Check owl:equivalentClass
            if (tbox) {
                auto eqTriples = tbox->findBySP(
                    expr1.className, "http://www.w3.org/2002/07/owl#equivalentClass");
                for (const auto& t : eqTriples) {
                    if (t.object == expr2.className) return true;
                }
                auto eqRev = tbox->findByPO(
                    "http://www.w3.org/2002/07/owl#equivalentClass", expr1.className);
                for (const auto& t : eqRev) {
                    if (t.subject == expr2.className) return true;
                }
            }
            // Check rdfs:subClassOf transitive closure
            if (expr2.type == ExpressionType::Atomic && tbox) {
                auto supers = getSuperClasses(expr1.className, tbox);
                if (supers.count(expr2.className)) return true;
            }
            // Check if expr2 is Complement and expr1 is disjoint with complement operand
            if (expr2.type == ExpressionType::Complement && expr2.complementOf && tbox) {
                if (expr2.complementOf->type == ExpressionType::Atomic) {
                    if (areDisjoint(expr1.className, expr2.complementOf->className, tbox))
                        return true;
                }
            }
            return false;
        }

        case ExpressionType::Intersection: {
            // (A AND B) subsumes C if A subsumes C OR B subsumes C
            // (sound but incomplete: sufficient condition)
            for (const auto& op : expr1.operands) {
                if (isSubsumedBy(*op, expr2, tbox)) return true;
            }
            return false;
        }

        case ExpressionType::Union: {
            // (A OR B) subsumes C iff A subsumes C OR B subsumes C
            for (const auto& op : expr1.operands) {
                if (isSubsumedBy(*op, expr2, tbox)) return true;
            }
            return false;
        }

        case ExpressionType::Complement: {
            // NOT A subsumes NOT B iff B subsumes A
            if (expr2.type == ExpressionType::Complement && expr2.complementOf
                && expr1.complementOf) {
                return isSubsumedBy(*expr2.complementOf, *expr1.complementOf, tbox);
            }
            // NOT A subsumes B iff A and B are disjoint
            if (expr1.complementOf && expr1.complementOf->type == ExpressionType::Atomic && tbox) {
                if (expr2.type == ExpressionType::Atomic) {
                    return areDisjoint(expr1.complementOf->className, expr2.className, tbox);
                }
            }
            return false;
        }

        case ExpressionType::ObjectSomeValuesFrom: {
            if (expr2.type == ExpressionType::ObjectSomeValuesFrom
                && expr1.property == expr2.property && expr1.filler && expr2.filler) {
                return isSubsumedBy(*expr1.filler, *expr2.filler, tbox);
            }
            return false;
        }

        case ExpressionType::ObjectAllValuesFrom: {
            if (expr2.type == ExpressionType::ObjectAllValuesFrom
                && expr1.property == expr2.property && expr1.filler && expr2.filler) {
                return isSubsumedBy(*expr2.filler, *expr1.filler, tbox);
            }
            return false;
        }

        case ExpressionType::ObjectMinCardinality: {
            if (expr2.type == ExpressionType::ObjectMinCardinality
                && expr1.property == expr2.property) {
                return expr1.cardinality >= expr2.cardinality;
            }
            return false;
        }

        case ExpressionType::ObjectHasValue: {
            if (expr2.type == ExpressionType::ObjectHasValue
                && expr1.property == expr2.property && expr1.value == expr2.value) {
                return true;
            }
            return false;
        }

        default:
            return false;
    }
}

bool ClassExpressionEvaluator::isEmpty(const ClassExpression& expr, TripleStore* tbox) {
    switch (expr.type) {
        case ExpressionType::Bottom:
            return true;
        case ExpressionType::Top:
            return false;
        case ExpressionType::Atomic:
            return expr.className == "http://www.w3.org/2002/07/owl#Nothing";
        case ExpressionType::Intersection: {
            if (tbox) {
                for (size_t i = 0; i < expr.operands.size(); ++i) {
                    for (size_t j = i + 1; j < expr.operands.size(); ++j) {
                        auto& a = expr.operands[i];
                        auto& b = expr.operands[j];
                        if (a->type == ExpressionType::Complement && a->complementOf) {
                            if (isSubsumedBy(*b, *a->complementOf, tbox)) return true;
                        }
                        if (b->type == ExpressionType::Complement && b->complementOf) {
                            if (isSubsumedBy(*a, *b->complementOf, tbox)) return true;
                        }
                        if (a->type == ExpressionType::Atomic && b->type == ExpressionType::Atomic) {
                            if (areDisjoint(a->className, b->className, tbox)) return true;
                        }
                    }
                }
            }
            for (const auto& op : expr.operands) {
                if (isEmpty(*op, tbox)) return true;
            }
            return false;
        }
        case ExpressionType::Complement: {
            if (!expr.complementOf) return false;
            return isUniversal(*expr.complementOf, tbox);
        }
        case ExpressionType::ObjectAllValuesFrom: {
            if (expr.filler && isEmpty(*expr.filler, tbox)) return true;
            return false;
        }
        default:
            return false;
    }
}

bool ClassExpressionEvaluator::isUniversal(const ClassExpression& expr, TripleStore* tbox) {
    switch (expr.type) {
        case ExpressionType::Top:
            return true;
        case ExpressionType::Atomic:
            return expr.className == "http://www.w3.org/2002/07/owl#Thing";
        case ExpressionType::Complement: {
            if (!expr.complementOf) return false;
            return isEmpty(*expr.complementOf, tbox);
        }
        case ExpressionType::Union: {
            for (const auto& op : expr.operands) {
                if (isUniversal(*op, tbox)) return true;
            }
            return false;
        }
        default:
            return false;
    }
}

} // namespace ontology
