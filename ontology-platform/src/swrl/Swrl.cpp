#include <ontology/Swrl.hpp>
#include <ontology/SwrlBackwardChainer.hpp>
#include <ontology/Storage.hpp>
#include <regex>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <cmath>
#include <unordered_set>
#include <ontology/Temporal.hpp>
#include <spdlog/spdlog.h>

namespace ontology {

// ============================================================================
// SWRL Atom 实现
// ============================================================================

bool SwrlAtom::hasVariables() const {
    auto vars = getVariables();
    return !vars.empty();
}

std::vector<String> SwrlAtom::getVariables() const {
    std::vector<String> vars;

    auto checkVar = [&](const String& arg) {
        if (!arg.empty() && arg[0] == '?') {
            vars.push_back(arg.substr(1));
        }
    };

    switch (type) {
        case SwrlAtomType::ClassAtom:
            checkVar(classArgument);
            break;

        case SwrlAtomType::ObjectPropertyAtom:
        case SwrlAtomType::DataPropertyAtom:
            checkVar(argument1);
            checkVar(argument2);
            break;

        case SwrlAtomType::BuiltInAtom:
            for (const auto& arg : builtInArgs) {
                checkVar(arg);
            }
            break;
    }

    return vars;
}

String SwrlAtom::toString() const {
    std::ostringstream oss;

    switch (type) {
        case SwrlAtomType::ClassAtom:
            oss << classId << "(" << classArgument << ")";
            break;

        case SwrlAtomType::ObjectPropertyAtom:
            oss << propertyId << "(" << argument1 << ", " << argument2 << ")";
            break;

        case SwrlAtomType::DataPropertyAtom:
            oss << propertyId << "(" << argument1 << ", " << argument2 << ")";
            break;

        case SwrlAtomType::BuiltInAtom:
            oss << builtInName << "(";
            for (size_t i = 0; i < builtInArgs.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << builtInArgs[i];
            }
            oss << ")";
            break;
    }

    return oss.str();
}

// ============================================================================
// SWRL Rule 实现
// ============================================================================

std::vector<String> SwrlRule::getAllVariables() const {
    std::unordered_set<String> varSet;

    for (const auto& atom : body) {
        auto vars = atom.getVariables();
        varSet.insert(vars.begin(), vars.end());
    }

    for (const auto& atom : head) {
        auto vars = atom.getVariables();
        varSet.insert(vars.begin(), vars.end());
    }

    return std::vector<String>(varSet.begin(), varSet.end());
}

String SwrlRule::toString() const {
    std::ostringstream oss;

    // Body
    for (size_t i = 0; i < body.size(); ++i) {
        if (i > 0) oss << " ∧ ";
        oss << body[i].toString();
    }

    oss << " → ";

    // Head
    for (size_t i = 0; i < head.size(); ++i) {
        if (i > 0) oss << " ∧ ";
        oss << head[i].toString();
    }

    return oss.str();
}

Json SwrlRule::toJson() const {
    Json j;
    j["id"] = id;
    j["name"] = name;
    j["description"] = description;
    j["confidence"] = confidence;
    j["priority"] = priority;
    j["enabled"] = enabled;

    j["body"] = Json::array();
    for (const auto& atom : body) {
        Json a;
        a["type"] = static_cast<int>(atom.type);
        a["classId"] = atom.classId;
        a["classArgument"] = atom.classArgument;
        a["propertyId"] = atom.propertyId;
        a["argument1"] = atom.argument1;
        a["argument2"] = atom.argument2;
        j["body"].push_back(a);
    }

    j["head"] = Json::array();
    for (const auto& atom : head) {
        Json a;
        a["type"] = static_cast<int>(atom.type);
        a["classId"] = atom.classId;
        a["classArgument"] = atom.classArgument;
        a["propertyId"] = atom.propertyId;
        a["argument1"] = atom.argument1;
        a["argument2"] = atom.argument2;
        j["head"].push_back(a);
    }

    return j;
}

SwrlRule SwrlRule::fromJson(const Json& j) {
    SwrlRule rule;
    rule.id = j.value("id", "");
    rule.name = j.value("name", "");
    rule.description = j.value("description", "");
    rule.confidence = j.value("confidence", 1.0f);
    rule.priority = j.value("priority", 1.0f);
    rule.enabled = j.value("enabled", true);

    if (j.contains("body")) {
        for (const auto& a : j["body"]) {
            SwrlAtom atom;
            atom.type = static_cast<SwrlAtomType>(a.value("type", 0));
            atom.classId = a.value("classId", "");
            atom.classArgument = a.value("classArgument", "");
            atom.propertyId = a.value("propertyId", "");
            atom.argument1 = a.value("argument1", "");
            atom.argument2 = a.value("argument2", "");
            rule.body.push_back(atom);
        }
    }

    if (j.contains("head")) {
        for (const auto& a : j["head"]) {
            SwrlAtom atom;
            atom.type = static_cast<SwrlAtomType>(a.value("type", 0));
            atom.classId = a.value("classId", "");
            atom.classArgument = a.value("classArgument", "");
            atom.propertyId = a.value("propertyId", "");
            atom.argument1 = a.value("argument1", "");
            atom.argument2 = a.value("argument2", "");
            rule.head.push_back(atom);
        }
    }

    return rule;
}

// ============================================================================
// SWRL Parser 实现
// ============================================================================

SwrlParser::SwrlParser() {
    prefixes_["swrlb"] = "http://www.w3.org/2003/11/swrlb#";
}

std::optional<SwrlRule> SwrlParser::parse(const String& ruleString) {
    // 检测是否为自然语言
    if (isNaturalLanguage(ruleString)) {
        return parseNaturalLanguage(ruleString);
    }

    SwrlRule rule;
    rule.id = "rule_" + std::to_string(std::hash<String>{}(ruleString));

    auto tokens = tokenize(ruleString);
    size_t pos = 0;

    bool inBody = true;

    while (pos < tokens.size() && tokens[pos].type != Token::Type::EOF_) {
        const auto& tok = tokens[pos];

        if (tok.type == Token::Type::ARROW) {
            inBody = false;
            pos++;
            continue;
        }

        if (tok.type == Token::Type::AND) {
            pos++;
            continue;
        }

        if (tok.type == Token::Type::CLASS || tok.type == Token::Type::PROPERTY ||
            tok.type == Token::Type::BUILTIN) {

            auto atom = parseAtom(tokens, pos);

            if (inBody) {
                rule.body.push_back(atom);
            } else {
                rule.head.push_back(atom);
            }
            continue;
        }

        pos++;
    }

    if (rule.body.empty() || rule.head.empty()) {
        return std::nullopt;
    }

    return rule;
}

bool SwrlParser::isNaturalLanguage(const String& text) {
    // 检测是否包含自然语言特征
    // 如果没有箭头、括号等技术符号，认为是自然语言
    if (text.find("->") != String::npos) return false;
    if (text.find('(') != String::npos && text.find(')') != String::npos) return false;

    // 包含中文或自然语言关键词
    for (char c : text) {
        if ((unsigned char)c > 127) return true;  // 中文字符
    }

    // 检测自然语言关键词
    String lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("如果") != String::npos || lower.find("那么") != String::npos ||
        lower.find("if ") != String::npos || lower.find("then ") != String::npos ||
        lower.find("当 ") != String::npos || lower.find("则 ") != String::npos ||
        lower.find("所有") != String::npos || lower.find("凡是") != String::npos) {
        return true;
    }

    return false;
}

std::optional<SwrlRule> SwrlParser::parseNaturalLanguage(const String& text) {
    SwrlRule rule;
    rule.id = "rule_nl_" + std::to_string(std::hash<String>{}(text));
    rule.description = text;

    String lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // 解析传递性规则: "如果 A 关系 B 且 B 关系 C，则 A 关系 C"
    if (parseTransitivityRule(text, rule)) {
        return rule;
    }

    // 解析层次规则: "所有 A 都是 B"
    if (parseHierarchyRule(text, rule)) {
        return rule;
    }

    // 解析属性规则: "如果 A 关系1 B，则 A 关系2 B"
    if (parsePropertyRule(text, rule)) {
        return rule;
    }

    // 解析条件规则: "如果 A 是 X，则 A 关系 B"
    if (parseConditionalRule(text, rule)) {
        return rule;
    }

    return std::nullopt;
}

bool SwrlParser::parseTransitivityRule(const String& text, SwrlRule& rule) {
    // 模式: "...传递性..." 或 "如果...且...则..."
    std::regex transPattern1("(.+?)的?传递性");
    std::regex transPattern2("如果\\s*(.+?)\\s*(.+?)\\s*(.+?)\\s*且\\s*\\3\\s*\\2\\s*(.+?)\\s*则\\s*\\1\\s*\\2\\s*\\4");

    std::smatch match;
    if (std::regex_search(text, match, transPattern1)) {
        String pred = match[1].str();
        // 去除前后空格
        pred.erase(0, pred.find_first_not_of(" \t"));
        pred.erase(pred.find_last_not_of(" \t") + 1);

        rule.name = pred + "传递性";

        SwrlAtom a1, a2, a3;
        a1.type = SwrlAtomType::ObjectPropertyAtom;
        a1.propertyId = pred;
        a1.argument1 = "?x";
        a1.argument2 = "?y";

        a2.type = SwrlAtomType::ObjectPropertyAtom;
        a2.propertyId = pred;
        a2.argument1 = "?y";
        a2.argument2 = "?z";

        a3.type = SwrlAtomType::ObjectPropertyAtom;
        a3.propertyId = pred;
        a3.argument1 = "?x";
        a3.argument2 = "?z";

        rule.body = {a1, a2};
        rule.head = {a3};
        return true;
    }

    return false;
}

bool SwrlParser::parseHierarchyRule(const String& text, SwrlRule& rule) {
    // 模式: "所有 A 都是 B" 或 "A 是 B 的子类"
    std::regex pattern1("所有\\s*(.+?)\\s*都是\\s*(.+)");
    std::regex pattern2("(.+?)\\s*是\\s*(.+?)\\s*的?子类");
    std::regex pattern3("every\\s+(.+?)\\s+is\\s+a\\s+(.+)");

    std::smatch match;
    if (std::regex_search(text, match, pattern1) || std::regex_search(text, match, pattern2)) {
        String subClass = match[1].str();
        String superClass = match[2].str();

        // 清理
        subClass.erase(0, subClass.find_first_not_of(" \t"));
        subClass.erase(subClass.find_last_not_of(" \t") + 1);
        superClass.erase(0, superClass.find_first_not_of(" \t"));
        superClass.erase(superClass.find_last_not_of(" \t") + 1);

        rule.name = subClass + "是" + superClass + "的子类";

        SwrlAtom body, head;
        body.type = SwrlAtomType::ClassAtom;
        body.classId = subClass;
        body.classArgument = "?x";

        head.type = SwrlAtomType::ClassAtom;
        head.classId = superClass;
        head.classArgument = "?x";

        rule.body = {body};
        rule.head = {head};
        return true;
    }

    return false;
}

bool SwrlParser::parsePropertyRule(const String& text, SwrlRule& rule) {
    // 模式: "如果 A 关系1 B，则 A 关系2 B"
    std::regex pattern("如果\\s*(.+?)\\s*(.+?)\\s*(.+?)\\s*，?则\\s*\\1\\s*(.+?)\\s*\\3");

    std::smatch match;
    if (std::regex_search(text, match, pattern)) {
        String subj = match[1].str();
        String pred1 = match[2].str();
        String obj = match[3].str();
        String pred2 = match[4].str();

        rule.name = pred1 + "蕴含" + pred2;

        SwrlAtom body, head;
        body.type = SwrlAtomType::ObjectPropertyAtom;
        body.propertyId = pred1;
        body.argument1 = "?x";
        body.argument2 = "?y";

        head.type = SwrlAtomType::ObjectPropertyAtom;
        head.propertyId = pred2;
        head.argument1 = "?x";
        head.argument2 = "?y";

        rule.body = {body};
        rule.head = {head};
        return true;
    }

    return false;
}

bool SwrlParser::parseConditionalRule(const String& text, SwrlRule& rule) {
    // 模式: "如果 A 是 X，则 A 关系 B"
    std::regex pattern("如果\\s*(.+?)\\s*是\\s*(.+?)\\s*，?则\\s*\\1\\s*(.+?)\\s*(.+)");

    std::smatch match;
    if (std::regex_search(text, match, pattern)) {
        String subj = match[1].str();
        String cls = match[2].str();
        String pred = match[3].str();
        String obj = match[4].str();

        rule.name = cls + "的" + pred + "规则";

        SwrlAtom body, head;
        body.type = SwrlAtomType::ClassAtom;
        body.classId = cls;
        body.classArgument = "?x";

        head.type = SwrlAtomType::ObjectPropertyAtom;
        head.propertyId = pred;
        head.argument1 = "?x";
        head.argument2 = obj;

        rule.body = {body};
        rule.head = {head};
        return true;
    }

    return false;
}

std::vector<SwrlRule> SwrlParser::parseFile(const String& path) {
    std::vector<SwrlRule> rules;

    std::ifstream file(path);
    if (!file.is_open()) return rules;

    String line;
    while (std::getline(file, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') continue;

        auto rule = parse(line);
        if (rule) {
            rules.push_back(*rule);
        }
    }

    return rules;
}

void SwrlParser::addPrefix(const String& prefix, const String& iri) {
    prefixes_[prefix] = iri;
}

std::vector<SwrlParser::Token> SwrlParser::tokenize(const String& input) {
    std::vector<Token> tokens;
    size_t i = 0;

    auto skipWs = [&]() {
        while (i < input.size() && std::isspace(input[i])) i++;
    };

    while (i < input.size()) {
        skipWs();
        if (i >= input.size()) break;

        char c = input[i];

        // 箭头
        if (c == '-' && i + 1 < input.size() && input[i+1] == '>') {
            tokens.push_back({Token::Type::ARROW, "->"});
            i += 2;
            continue;
        }

        // 标点
        if (c == '(') { tokens.push_back({Token::Type::LPAREN, "("}); i++; continue; }
        if (c == ')') { tokens.push_back({Token::Type::RPAREN, ")"}); i++; continue; }
        if (c == ',') { tokens.push_back({Token::Type::COMMA, ","}); i++; continue; }
        if (c == '.') { tokens.push_back({Token::Type::DOT, "."}); i++; continue; }

        // 变量 ?x
        if (c == '?') {
            i++;
            String name;
            while (i < input.size() && (std::isalnum(input[i]) || input[i] == '_')) {
                name += input[i++];
            }
            tokens.push_back({Token::Type::VARIABLE, "?" + name});
            continue;
        }

        // 标识符
        if (std::isalpha(c) || c == '_') {
            String name;
            while (i < input.size() && (std::isalnum(input[i]) || input[i] == '_' || input[i] == ':')) {
                name += input[i++];
            }

            // 判断类型
            if (name.find("swrlb:") == 0) {
                tokens.push_back({Token::Type::BUILTIN, name});
            } else if (name == "AND" || name == "and") {
                tokens.push_back({Token::Type::AND, name});
            } else if (name == "OR" || name == "or") {
                tokens.push_back({Token::Type::OR, name});
            } else if (name == "IF" || name == "if") {
                tokens.push_back({Token::Type::IF, name});
            } else if (name == "THEN" || name == "then") {
                tokens.push_back({Token::Type::THEN, name});
            } else if (std::isupper(name[0])) {
                // 类名通常首字母大写
                tokens.push_back({Token::Type::CLASS, name});
            } else if (name.find(':') != String::npos) {
                // 带前缀的名称，可能是属性
                tokens.push_back({Token::Type::PROPERTY, name});
            } else {
                // 属性或个体
                tokens.push_back({Token::Type::PROPERTY, name});
            }
            continue;
        }

        // 字面量 "..."
        if (c == '"') {
            i++;
            String lit;
            while (i < input.size() && input[i] != '"') {
                lit += input[i++];
            }
            if (i < input.size()) i++;
            tokens.push_back({Token::Type::LITERAL, lit});
            continue;
        }

        // 数字
        if (std::isdigit(c)) {
            String num;
            while (i < input.size() && (std::isdigit(input[i]) || input[i] == '.')) {
                num += input[i++];
            }
            tokens.push_back({Token::Type::LITERAL, num});
            continue;
        }

        i++;
    }

    tokens.push_back({Token::Type::EOF_, ""});
    return tokens;
}

SwrlAtom SwrlParser::parseAtom(std::vector<Token>& tokens, size_t& pos) {
    SwrlAtom atom;

    const auto& nameTok = tokens[pos];

    if (nameTok.type == Token::Type::BUILTIN) {
        atom.type = SwrlAtomType::BuiltInAtom;
        atom.builtInName = nameTok.value;
        pos++;

        if (tokens[pos].type == Token::Type::LPAREN) pos++;

        while (tokens[pos].type != Token::Type::RPAREN &&
               tokens[pos].type != Token::Type::EOF_) {

            if (tokens[pos].type == Token::Type::COMMA) {
                pos++;
                continue;
            }

            atom.builtInArgs.push_back(tokens[pos].value);
            pos++;
        }

        if (tokens[pos].type == Token::Type::RPAREN) pos++;

    } else if (nameTok.type == Token::Type::CLASS) {
        atom.type = SwrlAtomType::ClassAtom;
        atom.classId = nameTok.value;
        pos++;

        if (tokens[pos].type == Token::Type::LPAREN) pos++;

        atom.classArgument = tokens[pos].value;
        pos++;

        if (tokens[pos].type == Token::Type::RPAREN) pos++;

    } else if (nameTok.type == Token::Type::PROPERTY) {
        atom.propertyId = nameTok.value;
        pos++;

        if (tokens[pos].type == Token::Type::LPAREN) pos++;

        atom.argument1 = tokens[pos].value;
        pos++;

        if (tokens[pos].type == Token::Type::COMMA) pos++;

        atom.argument2 = tokens[pos].value;
        pos++;

        if (tokens[pos].type == Token::Type::RPAREN) pos++;

        // 判断是对象属性还是数据属性
        if (atom.argument2[0] == '"' || std::isdigit(atom.argument2[0])) {
            atom.type = SwrlAtomType::DataPropertyAtom;
        } else {
            atom.type = SwrlAtomType::ObjectPropertyAtom;
        }
    }

    return atom;
}

String SwrlParser::resolveName(const String& name) {
    auto pos = name.find(':');
    if (pos == String::npos) return name;

    String prefix = name.substr(0, pos);
    String local = name.substr(pos + 1);

    auto it = prefixes_.find(prefix);
    if (it != prefixes_.end()) {
        return it->second + local;
    }

    return name;
}

// ============================================================================
// SWRL Engine 实现
// ============================================================================

SwrlEngine::SwrlEngine(StoragePtr storage)
    : storage_(storage), backwardChainer_(std::make_unique<SwrlBackwardChainer>(storage)) {}

void SwrlEngine::addRule(const SwrlRule& rule) {
    rules_[rule.id] = rule;
    updateBackwardChainerRules();
}

void SwrlEngine::removeRule(const String& ruleId) {
    rules_.erase(ruleId);
    updateBackwardChainerRules();
}

void SwrlEngine::clearRules() {
    rules_.clear();
    updateBackwardChainerRules();
}

std::vector<SwrlRule> SwrlEngine::getRules() const {
    std::vector<SwrlRule> result;
    for (const auto& [id, rule] : rules_) {
        result.push_back(rule);
    }
    return result;
}

std::vector<Triple> SwrlEngine::applyRule(const SwrlRule& rule) {
    std::vector<Triple> newFacts;

    // 匹配规则体
    auto bindings = matchAtoms(rule.body);

    // 应用每个绑定到规则头
    for (const auto& binding : bindings) {
        for (const auto& atom : rule.head) {
            auto triple = applyBinding(atom, binding);
            if (!triple.subject.empty() && !triple.predicate.empty() && !triple.object.empty()) {
                triple.confidence = rule.confidence;
                triple.source = "swrl:" + rule.id;
                newFacts.push_back(triple);
            }
        }
    }

    return newFacts;
}

bool SwrlEngine::canApply(const SwrlRule& rule) const {
    auto bindings = matchAtoms(rule.body);
    return !bindings.empty();
}

SwrlEngine::Bindings SwrlEngine::matchAtoms(const std::vector<SwrlAtom>& atoms, const Binding& initial) const {
    Bindings solutions;
    solutions.push_back(initial);

    for (const auto& atom : atoms) {
        Bindings newSolutions;

        for (const auto& binding : solutions) {
            auto matches = matchAtom(atom, binding);
            newSolutions.insert(newSolutions.end(), matches.begin(), matches.end());
        }

        solutions = std::move(newSolutions);

        if (solutions.empty()) break;
    }

    return solutions;
}

SwrlEngine::Bindings SwrlEngine::matchAtom(const SwrlAtom& atom, const Binding& binding) const {
    Bindings results;

    switch (atom.type) {
        case SwrlAtomType::ClassAtom: {
            String arg = atom.classArgument;
            if (arg[0] == '?') {
                String var = arg.substr(1);
                auto it = binding.find(var);
                if (it != binding.end()) {
                    // 变量已绑定，检查类型
                    auto ind = storage_->getIndividual(it->second);
                    if (ind && ind->classId == atom.classId) {
                        results.push_back(binding);
                    }
                } else {
                    // 变量未绑定，查找该类的所有实例
                    auto individuals = storage_->getIndividualsByClass(atom.classId);
                    for (const auto& ind : individuals) {
                        auto newBinding = binding;
                        newBinding[var] = ind.id;
                        results.push_back(newBinding);
                    }
                }
            } else {
                // 常量，检查是否属于该类
                auto ind = storage_->getIndividual(arg);
                if (ind && ind->classId == atom.classId) {
                    results.push_back(binding);
                }
            }
            break;
        }

        case SwrlAtomType::ObjectPropertyAtom: {
            String arg1 = atom.argument1;
            String arg2 = atom.argument2;

            // 解析参数
            String val1, val2;
            bool var1 = arg1[0] == '?', var2 = arg2[0] == '?';
            String varName1 = var1 ? arg1.substr(1) : "";
            String varName2 = var2 ? arg2.substr(1) : "";

            if (!var1) val1 = arg1;
            else {
                auto it = binding.find(varName1);
                if (it != binding.end()) val1 = it->second;
            }

            if (!var2) val2 = arg2;
            else {
                auto it = binding.find(varName2);
                if (it != binding.end()) val2 = it->second;
            }

            // 查询属性
            std::vector<Triple> triples;

            if (!val1.empty() && !val2.empty()) {
                // 检查特定三元组是否存在
                auto t = storage_->queryTriples(ontology::TripleStore::TriplePattern{val1, atom.propertyId, val2, false, false, false});
                if (!t.empty()) {
                    results.push_back(binding);
                }
            } else if (!val1.empty()) {
                // 查询 val1 作为主体的所有三元组
                auto t = storage_->queryTriples(ontology::TripleStore::TriplePattern{val1, atom.propertyId, "", false, false, true});
                for (const auto& tr : t) {
                    auto newBinding = binding;
                    if (var2) newBinding[varName2] = tr.object;
                    results.push_back(newBinding);
                }
            } else if (!val2.empty()) {
                // 查询 val2 作为客体的所有三元组
                auto t = storage_->queryTriples(ontology::TripleStore::TriplePattern{"", atom.propertyId, val2, true, false, false});
                for (const auto& tr : t) {
                    auto newBinding = binding;
                    if (var1) newBinding[varName1] = tr.subject;
                    results.push_back(newBinding);
                }
            } else {
                // 两个都是未绑定变量，查询所有
                auto t = storage_->queryTriples(ontology::TripleStore::TriplePattern{"", atom.propertyId, "", true, false, true});
                for (const auto& tr : t) {
                    auto newBinding = binding;
                    if (var1) newBinding[varName1] = tr.subject;
                    if (var2) newBinding[varName2] = tr.object;
                    results.push_back(newBinding);
                }
            }
            break;
        }

        case SwrlAtomType::BuiltInAtom: {
            if (checkBuiltIn(atom, binding)) {
                results.push_back(binding);
            }
            break;
        }

        default:
            results.push_back(binding);
    }

    return results;
}

Triple SwrlEngine::applyBinding(const SwrlAtom& atom, const Binding& binding) {
    Triple triple;

    auto resolve = [&](const String& arg) -> String {
        if (!arg.empty() && arg[0] == '?') {
            String var = arg.substr(1);
            auto it = binding.find(var);
            return it != binding.end() ? it->second : "";
        }
        return arg;
    };

    switch (atom.type) {
        case SwrlAtomType::ObjectPropertyAtom:
            triple.subject = resolve(atom.argument1);
            triple.predicate = atom.propertyId;
            triple.object = resolve(atom.argument2);
            break;

        case SwrlAtomType::DataPropertyAtom:
            triple.subject = resolve(atom.argument1);
            triple.predicate = atom.propertyId;
            triple.object = resolve(atom.argument2);
            triple.isLiteral = true;
            break;

        default:
            break;
    }

    return triple;
}

bool SwrlEngine::checkBuiltIn(const SwrlAtom& atom, const Binding& binding) const {
    auto resolve = [&](const String& arg) -> String {
        if (!arg.empty() && arg[0] == '?') {
            String var = arg.substr(1);
            auto it = binding.find(var);
            return it != binding.end() ? it->second : "";
        }
        return arg;
    };

    std::vector<String> args;
    for (const auto& arg : atom.builtInArgs) {
        args.push_back(resolve(arg));
    }

    std::vector<String> results;
    return SwrlBuiltIns::execute(atom.builtInName, args, results);
}

std::vector<Triple> SwrlEngine::forwardChaining(int maxIterations) {
    std::vector<Triple> allNewFacts;

    for (int iter = 0; iter < maxIterations; ++iter) {
        std::vector<Triple> newFacts;

        for (const auto& [id, rule] : rules_) {
            if (!rule.enabled) continue;

            auto inferred = applyRule(rule);
            for (const auto& t : inferred) {
                // 检查是否已存在
                auto existing = storage_->queryTriples(ontology::TripleStore::TriplePattern{t.subject, t.predicate, t.object});
                if (existing.empty()) {
                    newFacts.push_back(t);
                }
            }
        }

        if (newFacts.empty()) break;

        // 添加新事实到存储
        for (const auto& t : newFacts) {
            storage_->addTriple(t);
        }

        allNewFacts.insert(allNewFacts.end(), newFacts.begin(), newFacts.end());
    }

    return allNewFacts;
}

std::vector<Triple> SwrlEngine::backwardChaining(const String& goal, int maxDepth) {
    std::vector<Triple> proof;

    if (maxDepth <= 0) return proof;

    // 解析目标 (格式: subject|predicate|object)
    size_t pos1 = goal.find('|');
    size_t pos2 = goal.find('|', pos1 + 1);
    if (pos1 == String::npos || pos2 == String::npos) return proof;

    String goalSubject = goal.substr(0, pos1);
    String goalPredicate = goal.substr(pos1 + 1, pos2 - pos1 - 1);
    String goalObject = goal.substr(pos2 + 1);

    // 首先检查是否为已知事实
    auto existing = storage_->queryTriples(ontology::TripleStore::TriplePattern{goalSubject, goalPredicate, goalObject});
    if (!existing.empty()) {
        return existing;
    }

    // 尝试通过规则推导
    for (const auto& [id, rule] : rules_) {
        if (!rule.enabled) continue;

        // 检查规则头是否能产生目标
        for (const auto& headAtom : rule.head) {
            if (headAtom.propertyId == goalPredicate) {
                // 尝试匹配规则头变量
                Binding initBinding;

                if (!headAtom.argument1.empty() && headAtom.argument1[0] == '?') {
                    initBinding[headAtom.argument1.substr(1)] = goalSubject;
                } else if (headAtom.argument1 != goalSubject) {
                    continue;
                }

                if (!headAtom.argument2.empty() && headAtom.argument2[0] == '?') {
                    initBinding[headAtom.argument2.substr(1)] = goalObject;
                } else if (headAtom.argument2 != goalObject) {
                    continue;
                }

                // 尝试满足规则体
                auto bodyBindings = matchAtoms(rule.body, initBinding);

                for (const auto& binding : bodyBindings) {
                    // 对规则体中的每个原子，递归证明
                    bool allProved = true;
                    std::vector<Triple> subProofs;

                    for (const auto& bodyAtom : rule.body) {
                        if (bodyAtom.type == SwrlAtomType::ObjectPropertyAtom) {
                            String subj = binding.count(bodyAtom.argument1.substr(1)) ?
                                binding.at(bodyAtom.argument1.substr(1)) : bodyAtom.argument1;
                            String obj = binding.count(bodyAtom.argument2.substr(1)) ?
                                binding.at(bodyAtom.argument2.substr(1)) : bodyAtom.argument2;

                            String subGoal = subj + "|" + bodyAtom.propertyId + "|" + obj;
                            auto subProof = backwardChaining(subGoal, maxDepth - 1);

                            if (subProof.empty()) {
                                // 检查是否已存在
                                auto check = storage_->queryTriples(
                                    ontology::TripleStore::TriplePattern{subj, bodyAtom.propertyId, obj});
                                if (check.empty()) {
                                    allProved = false;
                                    break;
                                }
                            } else {
                                subProofs.insert(subProofs.end(), subProof.begin(), subProof.end());
                            }
                        }
                    }

                    if (allProved) {
                        // 构造目标三元组
                        Triple proved;
                        proved.subject = goalSubject;
                        proved.predicate = goalPredicate;
                        proved.object = goalObject;
                        proved.confidence = rule.confidence;
                        proved.source = "swrl_backward:" + rule.id;
                        proof.push_back(proved);
                        proof.insert(proof.end(), subProofs.begin(), subProofs.end());
                        return proof;
                    }
                }
            }
        }
    }

    return proof;
}

std::vector<Triple> SwrlEngine::infer(int maxIterations) {
    switch (strategy_) {
        case Strategy::ForwardOnly:
            return forwardChaining(maxIterations);
        case Strategy::BackwardOnly:
            // Backward only doesn't produce new facts without a goal
            return {};
        case Strategy::Hybrid:
            // Run forward chaining, then backward chainer available for queries
            return forwardChaining(maxIterations);
    }
    return forwardChaining(maxIterations);
}

// ============================================================================
// Strategy & Backward Chainer Integration
// ============================================================================

void SwrlEngine::updateBackwardChainerRules() {
    if (backwardChainer_) {
        static std::vector<SwrlRule> ruleVec;
        ruleVec.clear();
        for (const auto& [id, rule] : rules_) {
            ruleVec.push_back(rule);
        }
        backwardChainer_->setRules(ruleVec);
    }
}

Bindings SwrlEngine::backwardChain(const std::vector<SwrlAtom>& goal, int maxDepth) {
    if (!backwardChainer_) return {};
    return backwardChainer_->prove(goal, maxDepth);
}

ProofNode SwrlEngine::explain(const std::vector<SwrlAtom>& goal, int maxDepth) {
    if (!backwardChainer_) return ProofNode{};
    return backwardChainer_->buildProofTree(goal, maxDepth);
}

void SwrlEngine::setStrategy(Strategy s) { strategy_ = s; }
SwrlEngine::Strategy SwrlEngine::strategy() const { return strategy_; }

// ============================================================================
// SWRL BuiltIns 实现
// ============================================================================

bool SwrlBuiltIns::equal(const String& a, const String& b) {
    return a == b;
}

bool SwrlBuiltIns::notEqual(const String& a, const String& b) {
    return a != b;
}

bool SwrlBuiltIns::lessThan(const String& a, const String& b) {
    try {
        return std::stod(a) < std::stod(b);
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return a < b;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return a < b;
    }
}

bool SwrlBuiltIns::lessThanOrEqual(const String& a, const String& b) {
    return lessThan(a, b) || equal(a, b);
}

bool SwrlBuiltIns::greaterThan(const String& a, const String& b) {
    return !lessThanOrEqual(a, b);
}

bool SwrlBuiltIns::greaterThanOrEqual(const String& a, const String& b) {
    return !lessThan(a, b);
}

bool SwrlBuiltIns::stringEqualIgnoreCase(const String& a, const String& b) {
    String aLower = a, bLower = b;
    std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
    std::transform(bLower.begin(), bLower.end(), bLower.begin(), ::tolower);
    return aLower == bLower;
}

bool SwrlBuiltIns::stringConcat(const String& a, const String& b, String& result) {
    result = a + b;
    return true;
}

bool SwrlBuiltIns::stringLength(const String& s, int& length) {
    length = s.length();
    return true;
}

bool SwrlBuiltIns::contains(const String& s, const String& substr) {
    return s.find(substr) != String::npos;
}

bool SwrlBuiltIns::startsWith(const String& s, const String& prefix) {
    return s.find(prefix) == 0;
}

bool SwrlBuiltIns::endsWith(const String& s, const String& suffix) {
    if (suffix.length() > s.length()) return false;
    return s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0;
}

bool SwrlBuiltIns::matches(const String& s, const String& pattern) {
    try {
        std::regex re(pattern);
        return std::regex_match(s, re);
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return false;
    }
}

bool SwrlBuiltIns::substring(const String& s, const String& startIndex, const String& len, String& result) {
    try {
        int start = std::stoi(startIndex) - 1; // SPARQL/SWRL is 1-indexed
        int length = std::stoi(len);
        if (start < 0) start = 0;
        if (start >= static_cast<int>(s.size())) return false;
        if (start + length > static_cast<int>(s.size())) length = static_cast<int>(s.size()) - start;
        result = s.substr(start, length);
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return false;
    }
}

bool SwrlBuiltIns::upperCase(const String& s, String& result) {
    result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return true;
}

bool SwrlBuiltIns::lowerCase(const String& s, String& result) {
    result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return true;
}

bool SwrlBuiltIns::add(const String& a, const String& b, String& result) {
    try {
        result = std::to_string(std::stod(a) + std::stod(b));
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return false;
    }
}

bool SwrlBuiltIns::subtract(const String& a, const String& b, String& result) {
    try {
        result = std::to_string(std::stod(a) - std::stod(b));
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return false;
    }
}

bool SwrlBuiltIns::multiply(const String& a, const String& b, String& result) {
    try {
        result = std::to_string(std::stod(a) * std::stod(b));
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return false;
    }
}

bool SwrlBuiltIns::divide(const String& a, const String& b, String& result) {
    try {
        double bVal = std::stod(b);
        if (bVal == 0) return false;
        result = std::to_string(std::stod(a) / bVal);
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return false;
    }
}

bool SwrlBuiltIns::abs(const String& a, String& result) {
    try {
        result = std::to_string(std::abs(std::stod(a)));
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return false;
    }
}

bool SwrlBuiltIns::sqrt(const String& a, String& result) {
    try {
        double val = std::stod(a);
        if (val < 0) return false;
        result = std::to_string(std::sqrt(val));
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return false;
    }
}

bool SwrlBuiltIns::mod(const String& a, const String& b, String& result) {
    try {
        double bVal = std::stod(b);
        if (bVal == 0) return false;
        result = std::to_string(std::fmod(std::stod(a), bVal));
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return false;
    }
}

bool SwrlBuiltIns::ceiling(const String& a, String& result) {
    try {
        result = std::to_string(std::ceil(std::stod(a)));
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return false;
    }
}

bool SwrlBuiltIns::floor(const String& a, String& result) {
    try {
        result = std::to_string(std::floor(std::stod(a)));
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return false;
    }
}

bool SwrlBuiltIns::round(const String& a, String& result) {
    try {
        result = std::to_string(std::round(std::stod(a)));
        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("SWRL JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("SWRL rule error: {}", e.what());
        return false;
    }
}

bool SwrlBuiltIns::year(const String& date, String& result) {
    // 支持 ISO 8601 格式: YYYY-MM-DD
    size_t pos = date.find('-');
    if (pos != String::npos) {
        result = date.substr(0, pos);
        return true;
    }
    return false;
}

bool SwrlBuiltIns::month(const String& date, String& result) {
    // 支持 ISO 8601 格式: YYYY-MM-DD
    size_t pos1 = date.find('-');
    size_t pos2 = date.find('-', pos1 + 1);
    if (pos1 != String::npos && pos2 != String::npos) {
        result = date.substr(pos1 + 1, pos2 - pos1 - 1);
        return true;
    }
    return false;
}

bool SwrlBuiltIns::day(const String& date, String& result) {
    // 支持 ISO 8601 格式: YYYY-MM-DD
    size_t pos1 = date.find('-');
    size_t pos2 = date.find('-', pos1 + 1);
    if (pos1 != String::npos && pos2 != String::npos && pos2 + 1 < date.length()) {
        size_t end = date.find('T', pos2 + 1);
        if (end == String::npos) end = date.length();
        result = date.substr(pos2 + 1, end - pos2 - 1);
        return true;
    }
    return false;
}

bool SwrlBuiltIns::hours(const String& time, String& result) {
    // 支持 ISO 8601 时间: HH:MM:SS 或完整日期时间
    size_t posT = time.find('T');
    String t = (posT != String::npos) ? time.substr(posT + 1) : time;

    size_t pos = t.find(':');
    if (pos != String::npos) {
        result = t.substr(0, pos);
        return true;
    }
    return false;
}

bool SwrlBuiltIns::minutes(const String& time, String& result) {
    size_t posT = time.find('T');
    String t = (posT != String::npos) ? time.substr(posT + 1) : time;

    size_t pos1 = t.find(':');
    size_t pos2 = t.find(':', pos1 + 1);
    if (pos1 != String::npos && pos2 != String::npos) {
        result = t.substr(pos1 + 1, pos2 - pos1 - 1);
        return true;
    }
    return false;
}

bool SwrlBuiltIns::seconds(const String& time, String& result) {
    size_t posT = time.find('T');
    String t = (posT != String::npos) ? time.substr(posT + 1) : time;

    size_t pos1 = t.find(':');
    size_t pos2 = t.find(':', pos1 + 1);
    if (pos1 != String::npos && pos2 != String::npos && pos2 + 1 < t.length()) {
        result = t.substr(pos2 + 1);
        return true;
    }
    return false;
}

bool SwrlBuiltIns::list(const std::vector<String>& items, std::vector<String>& result) {
    result = items;
    return true;
}

bool SwrlBuiltIns::member(const String& item, const std::vector<String>& list) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

bool SwrlBuiltIns::length(const std::vector<String>& list, int& len) {
    len = list.size();
    return true;
}

bool SwrlBuiltIns::execute(
    const String& name,
    const std::vector<String>& args,
    std::vector<String>& results
) {
    String localName = name;
    if (localName.find("swrlb:") == 0) {
        localName = localName.substr(6);
    }

    // 比较函数
    if (localName == "equal" && args.size() >= 2) {
        return equal(args[0], args[1]);
    }
    if (localName == "notEqual" && args.size() >= 2) {
        return notEqual(args[0], args[1]);
    }
    if (localName == "lessThan" && args.size() >= 2) {
        return lessThan(args[0], args[1]);
    }
    if (localName == "lessThanOrEqual" && args.size() >= 2) {
        return lessThanOrEqual(args[0], args[1]);
    }
    if (localName == "greaterThan" && args.size() >= 2) {
        return greaterThan(args[0], args[1]);
    }
    if (localName == "greaterThanOrEqual" && args.size() >= 2) {
        return greaterThanOrEqual(args[0], args[1]);
    }

    // 字符串函数
    if (localName == "contains" && args.size() >= 2) {
        return contains(args[0], args[1]);
    }
    if (localName == "startsWith" && args.size() >= 2) {
        return startsWith(args[0], args[1]);
    }
    if (localName == "endsWith" && args.size() >= 2) {
        return endsWith(args[0], args[1]);
    }
    if (localName == "matches" && args.size() >= 2) {
        return matches(args[0], args[1]);
    }
    if (localName == "stringEqualIgnoreCase" && args.size() >= 2) {
        return stringEqualIgnoreCase(args[0], args[1]);
    }
    if (localName == "stringConcat" && args.size() >= 2) {
        String res;
        if (stringConcat(args[0], args[1], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "stringLength" && args.size() >= 1) {
        int len = 0;
        if (stringLength(args[0], len)) {
            results.push_back(std::to_string(len));
            return true;
        }
        return false;
    }
    if (localName == "substring" && args.size() >= 3) {
        String res;
        if (substring(args[0], args[1], args[2], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "upperCase" && args.size() >= 1) {
        String res;
        if (upperCase(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "lowerCase" && args.size() >= 1) {
        String res;
        if (lowerCase(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }

    // 数值函数
    if (localName == "add" && args.size() >= 2) {
        String res;
        if (add(args[0], args[1], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "subtract" && args.size() >= 2) {
        String res;
        if (subtract(args[0], args[1], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "multiply" && args.size() >= 2) {
        String res;
        if (multiply(args[0], args[1], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "divide" && args.size() >= 2) {
        String res;
        if (divide(args[0], args[1], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "abs" && args.size() >= 1) {
        String res;
        if (abs(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "sqrt" && args.size() >= 1) {
        String res;
        if (sqrt(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "mod" && args.size() >= 2) {
        String res;
        if (mod(args[0], args[1], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "ceiling" && args.size() >= 1) {
        String res;
        if (ceiling(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "floor" && args.size() >= 1) {
        String res;
        if (floor(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "round" && args.size() >= 1) {
        String res;
        if (round(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }

    // 日期时间函数
    if (localName == "year" && args.size() >= 1) {
        String res;
        if (year(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "month" && args.size() >= 1) {
        String res;
        if (month(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "day" && args.size() >= 1) {
        String res;
        if (day(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "hours" && args.size() >= 1) {
        String res;
        if (hours(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "minutes" && args.size() >= 1) {
        String res;
        if (minutes(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }
    if (localName == "seconds" && args.size() >= 1) {
        String res;
        if (seconds(args[0], res)) {
            results.push_back(res);
            return true;
        }
        return false;
    }

    // Temporal functions (Allen algebra)
    if (localName == "temporalBefore" || localName == "temporal:before") {
        if (args.size() >= 2) return temporalBefore(args[0], args[1]);
        return false;
    }
    if (localName == "temporalAfter" || localName == "temporal:after") {
        if (args.size() >= 2) return temporalAfter(args[0], args[1]);
        return false;
    }
    if (localName == "temporalOverlaps" || localName == "temporal:overlaps") {
        if (args.size() >= 4) return temporalOverlaps(args[0], args[1], args[2], args[3]);
        return false;
    }
    if (localName == "temporalDuring" || localName == "temporal:during") {
        if (args.size() >= 4) return temporalDuring(args[0], args[1], args[2], args[3]);
        return false;
    }
    if (localName == "temporalContains" || localName == "temporal:contains") {
        if (args.size() >= 4) return temporalContains(args[0], args[1], args[2], args[3]);
        return false;
    }

    return false;
}

// ============================================================================
// SWRL Explainer 实现
// ============================================================================

String SwrlExplainer::explainRuleApplication(
    const SwrlRule& rule,
    const Binding& binding,
    const std::vector<Triple>& inferredFacts
) {
    std::ostringstream oss;

    oss << "应用规则 [" << rule.name << "]:\n\n";

    // 显示绑定
    oss << "变量绑定:\n";
    for (const auto& [var, val] : binding) {
        oss << "  ?" << var << " = " << val << "\n";
    }
    oss << "\n";

    // 显示前提满足情况
    oss << "前提满足:\n";
    for (const auto& atom : rule.body) {
        oss << "  ✓ " << atom.toString() << "\n";
    }
    oss << "\n";

    // 显示结论
    oss << "推导结论:\n";
    for (const auto& t : inferredFacts) {
        oss << "  → " << t.subject << " " << t.predicate << " " << t.object << "\n";
    }

    return oss.str();
}

String SwrlExplainer::generateProof(
    const Triple& fact,
    const std::vector<SwrlRule>& rules,
    const std::vector<Triple>& facts
) {
    std::ostringstream oss;

    oss << "证明: " << fact.subject << " " << fact.predicate << " " << fact.object << "\n\n";

    // 检查是否为直接事实
    for (const auto& f : facts) {
        if (f.subject == fact.subject &&
            f.predicate == fact.predicate &&
            f.object == fact.object) {
            oss << "✓ 直接事实 (置信度: " << f.confidence << ")\n";
            return oss.str();
        }
    }

    // 检查是否可通过规则推导
    for (const auto& rule : rules) {
        for (const auto& atom : rule.head) {
            if (atom.propertyId == fact.predicate) {
                oss << "通过规则 [" << rule.name << "] 推导\n";
                oss << "规则: " << rule.toString() << "\n";
                oss << "置信度: " << rule.confidence << "\n";
                return oss.str();
            }
        }
    }

    oss << "✗ 无法证明\n";
    return oss.str();
}

// ============================================================================
// Temporal built-ins
// ============================================================================

bool SwrlBuiltIns::temporalBefore(const String& t1, const String& t2) {
    int64_t e1 = isoToEpochMs(t1);
    int64_t s2 = isoToEpochMs(t2);
    return e1 < s2;
}

bool SwrlBuiltIns::temporalAfter(const String& t1, const String& t2) {
    int64_t s1 = isoToEpochMs(t1);
    int64_t e2 = isoToEpochMs(t2);
    return s1 > e2;
}

bool SwrlBuiltIns::temporalOverlaps(const String& t1_start, const String& t1_end,
                                     const String& t2_start, const String& t2_end) {
    TemporalInterval i1{t1_start, t1_end};
    TemporalInterval i2{t2_start, t2_end};
    auto r = i1.relationTo(i2);
    return r == AllenRelation::Overlaps || r == AllenRelation::OverlappedBy;
}

bool SwrlBuiltIns::temporalDuring(const String& t1_start, const String& t1_end,
                                   const String& t2_start, const String& t2_end) {
    TemporalInterval i1{t1_start, t1_end};
    TemporalInterval i2{t2_start, t2_end};
    return i1.relationTo(i2) == AllenRelation::During;
}

bool SwrlBuiltIns::temporalContains(const String& t1_start, const String& t1_end,
                                     const String& t2_start, const String& t2_end) {
    TemporalInterval i1{t1_start, t1_end};
    TemporalInterval i2{t2_start, t2_end};
    return i1.relationTo(i2) == AllenRelation::Contains;
}

} // namespace ontology
