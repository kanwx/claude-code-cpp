#include <ontology/OntologyIO.hpp>
#include <sstream>
#include <fstream>
#include <regex>
#include <algorithm>

namespace ontology {

// ============================================================================
// 常量定义
// ============================================================================

const String OwlToOntologyConverter::OwlToOntologyConverter::OWL_NS = "http://www.w3.org/2002/07/owl#";
const String OwlToOntologyConverter::OwlToOntologyConverter::RDF_NS = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
const String OwlToOntologyConverter::OwlToOntologyConverter::RDFS_NS = "http://www.w3.org/2000/01/rdf-schema#";
const String OwlToOntologyConverter::OwlToOntologyConverter::XSD_NS = "http://www.w3.org/2001/XMLSchema#";

// ============================================================================
// OntologyParser 基类实现
// ============================================================================

std::optional<Ontology> OntologyParser::parseFile(const String& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse(buffer.str());
}

// ============================================================================
// OntologyWriter 基类实现
// ============================================================================

bool OntologyWriter::writeFile(const String& path, const Ontology& ontology) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << write(ontology);
    return true;
}

// ============================================================================
// Turtle 解析器实现
// ============================================================================

TurtleParser::TurtleParser() {
    // 默认前缀
    prefixes_["rdf"] = OwlToOntologyConverter::RDF_NS;
    prefixes_["rdfs"] = OwlToOntologyConverter::RDFS_NS;
    prefixes_["owl"] = OwlToOntologyConverter::OWL_NS;
    prefixes_["xsd"] = OwlToOntologyConverter::XSD_NS;
}

std::optional<Ontology> TurtleParser::parse(const String& content) {
    auto graph = parseRdf(content);
    if (!graph) {
        return std::nullopt;
    }

    return OwlToOntologyConverter::convert(*graph);
}

std::optional<RdfGraph> TurtleParser::parseRdf(const String& content) {
    RdfGraph graph;
    graph.prefixes = prefixes_;
    graph.baseIRI = baseIri_;

    auto tokens = tokenize(content);

    size_t pos = 0;
    while (pos < tokens.size()) {
        const auto& token = tokens[pos];

        // 处理 @prefix
        if (token.type == Token::PREFIX) {
            pos++;
            if (pos + 2 >= tokens.size()) break;

            String prefix = tokens[pos].value;
            // 移除冒号
            if (prefix.back() == ':') prefix.pop_back();

            pos++;
            String iri = parseIri(tokens[pos].value);
            pos++;

            graph.prefixes[prefix] = iri;
            prefixes_[prefix] = iri;

            // 跳过 .
            if (pos < tokens.size() && tokens[pos].type == Token::DOT) pos++;
            continue;
        }

        // 处理 @base
        if (token.type == Token::BASE) {
            pos++;
            if (pos >= tokens.size()) break;

            baseIri_ = parseIri(tokens[pos].value);
            graph.baseIRI = baseIri_;
            pos++;

            if (pos < tokens.size() && tokens[pos].type == Token::DOT) pos++;
            continue;
        }

        // 解析三元组
        if (token.type == Token::IRI || token.type == Token::PREFIXED_NAME ||
            token.type == Token::BLANK_NODE) {

            String subject;
            if (token.type == Token::IRI) {
                subject = parseIri(token.value);
            } else if (token.type == Token::PREFIXED_NAME) {
                subject = resolveIri(token.value);
            } else {
                subject = token.value;
            }
            pos++;

            // 解析谓词-对象列表
            while (pos < tokens.size() && tokens[pos].type != Token::DOT) {
                String predicate;
                if (tokens[pos].type == Token::A) {
                    predicate = OwlToOntologyConverter::RDF_NS + "type";
                    pos++;
                } else if (tokens[pos].type == Token::IRI) {
                    predicate = parseIri(tokens[pos].value);
                    pos++;
                } else if (tokens[pos].type == Token::PREFIXED_NAME) {
                    predicate = resolveIri(tokens[pos].value);
                    pos++;
                } else {
                    break;
                }

                // 解析对象
                while (pos < tokens.size()) {
                    RdfTriple triple;
                    triple.subject = subject;
                    triple.predicate = predicate;

                    const auto& objToken = tokens[pos];
                    if (objToken.type == Token::IRI) {
                        triple.object = parseIri(objToken.value);
                        triple.isLiteral = false;
                        pos++;
                    } else if (objToken.type == Token::PREFIXED_NAME) {
                        triple.object = resolveIri(objToken.value);
                        triple.isLiteral = false;
                        pos++;
                    } else if (objToken.type == Token::LITERAL) {
                        triple.object = objToken.value;
                        triple.isLiteral = true;
                        triple.datatype = objToken.datatype;
                        triple.language = objToken.language;
                        pos++;
                    } else if (objToken.type == Token::BLANK_NODE) {
                        triple.object = objToken.value;
                        triple.isLiteral = false;
                        pos++;
                    } else {
                        break;
                    }

                    graph.triples.push_back(triple);

                    // 检查列表分隔符
                    if (pos >= tokens.size()) break;

                    if (tokens[pos].type == Token::COMMA) {
                        // 相同主语和谓词，继续下一个对象
                        pos++;
                        continue;
                    } else if (tokens[pos].type == Token::SEMICOLON) {
                        // 相同主语，新谓词
                        pos++;
                        break;
                    } else if (tokens[pos].type == Token::DOT) {
                        // 三元组结束
                        pos++;
                        break;
                    } else {
                        // 可能是新谓词
                        break;
                    }
                }
            }
        } else {
            pos++;
        }
    }

    return graph;
}

std::vector<TurtleParser::Token> TurtleParser::tokenize(const String& content) {
    std::vector<Token> tokens;
    size_t i = 0;

    auto skipWs = [&]() {
        while (i < content.size() && (std::isspace(content[i]) || content[i] == '#')) {
            if (content[i] == '#') {
                // 跳过注释
                while (i < content.size() && content[i] != '\n') i++;
            }
            i++;
        }
    };

    while (i < content.size()) {
        skipWs();
        if (i >= content.size()) break;

        char c = content[i];

        // @prefix, @base
        if (c == '@') {
            i++;
            skipWs();

            String keyword;
            while (i < content.size() && std::isalpha(content[i])) {
                keyword += content[i++];
            }

            if (keyword == "prefix") {
                tokens.push_back({Token::PREFIX, ""});
            } else if (keyword == "base") {
                tokens.push_back({Token::BASE, ""});
            }
            continue;
        }

        // IRI <...>
        if (c == '<') {
            i++;
            String iri;
            while (i < content.size() && content[i] != '>') {
                iri += content[i++];
            }
            if (i < content.size()) i++; // skip >
            tokens.push_back({Token::IRI, iri});
            continue;
        }

        // 字符串字面量 "..." 或 """..."""
        if (c == '"') {
            i++;
            String literal;
            bool multiline = false;

            // 检查三引号
            if (i + 1 < content.size() && content[i] == '"' && content[i+1] == '"') {
                multiline = true;
                i += 2;
                while (i + 2 < content.size()) {
                    if (content[i] == '"' && content[i+1] == '"' && content[i+2] == '"') {
                        i += 3;
                        break;
                    }
                    literal += content[i++];
                }
            } else {
                while (i < content.size() && content[i] != '"') {
                    if (content[i] == '\\' && i + 1 < content.size()) {
                        i++;
                        switch (content[i]) {
                            case 'n': literal += '\n'; break;
                            case 't': literal += '\t'; break;
                            case '\\': literal += '\\'; break;
                            case '"': literal += '"'; break;
                            default: literal += content[i];
                        }
                    } else {
                        literal += content[i];
                    }
                    i++;
                }
                if (i < content.size()) i++; // skip "
            }

            Token tok{Token::LITERAL, literal};

            // 检查语言标签或数据类型
            skipWs();
            if (i < content.size()) {
                if (content[i] == '@') {
                    i++;
                    String lang;
                    while (i < content.size() && std::isalpha(content[i])) {
                        lang += content[i++];
                    }
                    tok.language = lang;
                } else if (content[i] == '^' && i + 1 < content.size() && content[i+1] == '^') {
                    i += 2;
                    skipWs();
                    if (content[i] == '<') {
                        i++;
                        String dt;
                        while (i < content.size() && content[i] != '>') {
                            dt += content[i++];
                        }
                        if (i < content.size()) i++;
                        tok.datatype = dt;
                    }
                }
            }

            tokens.push_back(tok);
            continue;
        }

        // 空白节点 _:
        if (c == '_' && i + 1 < content.size() && content[i+1] == ':') {
            i += 2;
            String id;
            while (i < content.size() && (std::isalnum(content[i]) || content[i] == '_')) {
                id += content[i++];
            }
            tokens.push_back({Token::BLANK_NODE, "_:" + id});
            continue;
        }

        // 标点符号
        if (c == '.') {
            tokens.push_back({Token::DOT, "."});
            i++;
            continue;
        }
        if (c == ',') {
            tokens.push_back({Token::COMMA, ","});
            i++;
            continue;
        }
        if (c == ';') {
            tokens.push_back({Token::SEMICOLON, ";"});
            i++;
            continue;
        }
        if (c == '[') {
            tokens.push_back({Token::LBRACKET, "["});
            i++;
            continue;
        }
        if (c == ']') {
            tokens.push_back({Token::RBRACKET, "]"});
            i++;
            continue;
        }
        if (c == '(') {
            tokens.push_back({Token::LPAREN, "("});
            i++;
            continue;
        }
        if (c == ')') {
            tokens.push_back({Token::RPAREN, ")"});
            i++;
            continue;
        }

        // 前缀名或关键字
        if (std::isalpha(c) || c == ':') {
            String name;
            while (i < content.size() && (std::isalnum(content[i]) || content[i] == '_' || content[i] == ':')) {
                name += content[i++];
            }

            // 检查 'a' (rdf:type 简写)
            if (name == "a") {
                tokens.push_back({Token::A, "a"});
            } else if (name.find(':') != String::npos) {
                tokens.push_back({Token::PREFIXED_NAME, name});
            } else {
                // 可能是关键字
                tokens.push_back({Token::PREFIXED_NAME, name + ":"});
            }
            continue;
        }

        i++;
    }

    tokens.push_back({Token::EOF_, ""});
    return tokens;
}

String TurtleParser::resolveIri(const String& prefixedName) {
    auto pos = prefixedName.find(':');
    if (pos == String::npos) {
        return prefixedName;
    }

    String prefix = prefixedName.substr(0, pos);
    String local = prefixedName.substr(pos + 1);

    auto it = prefixes_.find(prefix);
    if (it != prefixes_.end()) {
        return it->second + local;
    }

    return prefixedName;
}

String TurtleParser::parseIri(const String& iriRef) {
    // 已经是完整 IRI (去掉尖括号后的)
    if (iriRef.find("://") != String::npos) {
        return iriRef;
    }
    // 相对 IRI，需要结合 base
    if (!baseIri_.empty()) {
        return baseIri_ + iriRef;
    }
    return iriRef;
}

// ============================================================================
// Turtle 写入器实现
// ============================================================================

TurtleWriter::TurtleWriter() {
    prefixes_["rdf"] = OwlToOntologyConverter::RDF_NS;
    prefixes_["rdfs"] = OwlToOntologyConverter::RDFS_NS;
    prefixes_["owl"] = OwlToOntologyConverter::OWL_NS;
    prefixes_["xsd"] = OwlToOntologyConverter::XSD_NS;
}

String TurtleWriter::write(const Ontology& ontology) {
    std::ostringstream oss;

    // 写入前缀
    oss << "@prefix : <" << (ontology.baseIRI.empty() ? "http://example.org/ontology#" : ontology.baseIRI) << "> .\n";
    oss << "@prefix owl: <" << OwlToOntologyConverter::OWL_NS << "> .\n";
    oss << "@prefix rdf: <" << OwlToOntologyConverter::RDF_NS << "> .\n";
    oss << "@prefix rdfs: <" << OwlToOntologyConverter::RDFS_NS << "> .\n";
    oss << "@prefix xsd: <" << OwlToOntologyConverter::XSD_NS << "> .\n";
    oss << "\n";

    // 写入类
    for (const auto& [id, cls] : ontology.classes) {
        oss << ":" << cls.id << " rdf:type owl:Class ;\n";
        oss << "  rdfs:label \"" << escapeLiteral(cls.name) << "\" ;\n";
        if (!cls.description.empty()) {
            oss << "  rdfs:comment \"" << escapeLiteral(cls.description) << "\" ;\n";
        }
        for (const auto& super : cls.superClasses) {
            oss << "  rdfs:subClassOf :" << super << " ;\n";
        }
        for (const auto& eq : cls.equivalentClasses) {
            oss << "  owl:equivalentClass :" << eq << " ;\n";
        }
        for (const auto& dis : cls.disjointClasses) {
            oss << "  owl:disjointWith :" << dis << " ;\n";
        }
        oss << ".\n\n";
    }

    // 写入属性/关系
    for (const auto& [id, rel] : ontology.relations) {
        oss << ":" << rel.id << " rdf:type owl:ObjectProperty ;\n";
        oss << "  rdfs:label \"" << escapeLiteral(rel.name) << "\" ;\n";
        if (!rel.description.empty()) {
            oss << "  rdfs:comment \"" << escapeLiteral(rel.description) << "\" ;\n";
        }
        if (!rel.domain.empty()) {
            oss << "  rdfs:domain :" << rel.domain << " ;\n";
        }
        if (!rel.range.empty()) {
            oss << "  rdfs:range :" << rel.range << " ;\n";
        }
        if (rel.isTransitive) {
            oss << "  rdf:type owl:TransitiveProperty ;\n";
        }
        if (rel.isSymmetric) {
            oss << "  rdf:type owl:SymmetricProperty ;\n";
        }
        if (rel.isFunctional) {
            oss << "  rdf:type owl:FunctionalProperty ;\n";
        }
        if (!rel.inverseProperty.empty()) {
            oss << "  owl:inverseOf :" << rel.inverseProperty << " ;\n";
        }
        oss << ".\n\n";
    }

    // 写入个体
    for (const auto& [id, ind] : ontology.individuals) {
        oss << ":" << ind.id << " rdf:type :" << ind.classId << " ;\n";
        oss << "  rdfs:label \"" << escapeLiteral(ind.name) << "\" ;\n";

        // 关系
        for (const auto& [relId, targets] : ind.relations) {
            for (const auto& target : targets) {
                oss << "  :" << relId << " :" << target << " ;\n";
            }
        }
        oss << ".\n\n";
    }

    return oss.str();
}

String TurtleWriter::writeRdf(const RdfGraph& graph) {
    std::ostringstream oss;

    // 写入前缀
    for (const auto& [prefix, iri] : graph.prefixes) {
        oss << "@prefix " << prefix << ": <" << iri << "> .\n";
    }
    oss << "\n";

    // 写入三元组
    for (const auto& t : graph.triples) {
        oss << "<" << t.subject << "> <" << t.predicate << "> ";
        if (t.isLiteral) {
            oss << "\"" << escapeLiteral(t.object) << "\"";
            if (!t.datatype.empty()) {
                oss << "^^<" << t.datatype << ">";
            }
            if (!t.language.empty()) {
                oss << "@" << t.language;
            }
        } else {
            oss << "<" << t.object << ">";
        }
        oss << " .\n";
    }

    return oss.str();
}

String TurtleWriter::escapeLiteral(const String& literal) {
    String result;
    for (char c : literal) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\t': result += "\\t"; break;
            case '\r': result += "\\r"; break;
            default: result += c;
        }
    }
    return result;
}

// ============================================================================
// RDF/XML 写入器实现
// ============================================================================

RdfXmlWriter::RdfXmlWriter() {}

String RdfXmlWriter::write(const Ontology& ontology) {
    std::ostringstream oss;

    String base = ontology.baseIRI.empty() ? "http://example.org/ontology#" : ontology.baseIRI;

    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    oss << "<rdf:RDF\n";
    oss << "    xmlns:rdf=\"" << OwlToOntologyConverter::RDF_NS << "\"\n";
    oss << "    xmlns:rdfs=\"" << OwlToOntologyConverter::RDFS_NS << "\"\n";
    oss << "    xmlns:owl=\"" << OwlToOntologyConverter::OWL_NS << "\"\n";
    oss << "    xmlns:xsd=\"" << OwlToOntologyConverter::XSD_NS << "\"\n";
    oss << "    xmlns=\"" << base << "\">\n";
    oss << "\n";

    // 写入类
    for (const auto& [id, cls] : ontology.classes) {
        oss << indent(1) << "<owl:Class rdf:about=\"" << base << id << "\">\n";
        oss << indent(2) << "<rdfs:label>" << escapeXml(cls.name) << "</rdfs:label>\n";
        if (!cls.description.empty()) {
            oss << indent(2) << "<rdfs:comment>" << escapeXml(cls.description) << "</rdfs:comment>\n";
        }
        for (const auto& super : cls.superClasses) {
            oss << indent(2) << "<rdfs:subClassOf rdf:resource=\"" << base << super << "\"/>\n";
        }
        oss << indent(1) << "</owl:Class>\n\n";
    }

    // 写入属性
    for (const auto& [id, rel] : ontology.relations) {
        oss << indent(1) << "<owl:ObjectProperty rdf:about=\"" << base << id << "\">\n";
        oss << indent(2) << "<rdfs:label>" << escapeXml(rel.name) << "</rdfs:label>\n";
        if (!rel.domain.empty()) {
            oss << indent(2) << "<rdfs:domain rdf:resource=\"" << base << rel.domain << "\"/>\n";
        }
        if (!rel.range.empty()) {
            oss << indent(2) << "<rdfs:range rdf:resource=\"" << base << rel.range << "\"/>\n";
        }
        oss << indent(1) << "</owl:ObjectProperty>\n\n";
    }

    // 写入个体
    for (const auto& [id, ind] : ontology.individuals) {
        oss << indent(1) << "<" << ind.classId << " rdf:about=\"" << base << id << "\">\n";
        oss << indent(2) << "<rdfs:label>" << escapeXml(ind.name) << "</rdfs:label>\n";
        for (const auto& [relId, targets] : ind.relations) {
            for (const auto& target : targets) {
                oss << indent(2) << "<" << relId << " rdf:resource=\"" << base << target << "\"/>\n";
            }
        }
        oss << indent(1) << "</" << ind.classId << ">\n\n";
    }

    oss << "</rdf:RDF>\n";
    return oss.str();
}

String RdfXmlWriter::writeRdf(const RdfGraph& graph) {
    std::ostringstream oss;

    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    oss << "<rdf:RDF\n";
    for (const auto& [prefix, iri] : graph.prefixes) {
        oss << "    xmlns:" << prefix << "=\"" << iri << "\"\n";
    }
    oss << ">\n";

    // 简化：按主语分组
    std::unordered_map<String, std::vector<RdfTriple>> grouped;
    for (const auto& t : graph.triples) {
        grouped[t.subject].push_back(t);
    }

    for (const auto& [subject, triples] : grouped) {
        oss << indent(1) << "<rdf:Description rdf:about=\"" << subject << "\">\n";
        for (const auto& t : triples) {
            if (t.isLiteral) {
                oss << indent(2) << "<" << t.predicate << ">" << escapeXml(t.object) << "</" << t.predicate << ">\n";
            } else {
                oss << indent(2) << "<" << t.predicate << " rdf:resource=\"" << t.object << "\"/>\n";
            }
        }
        oss << indent(1) << "</rdf:Description>\n\n";
    }

    oss << "</rdf:RDF>\n";
    return oss.str();
}

String RdfXmlWriter::escapeXml(const String& text) {
    String result;
    for (char c : text) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default: result += c;
        }
    }
    return result;
}

String RdfXmlWriter::indent(int level) {
    return String(level * indent_, ' ');
}

// ============================================================================
// RDF/XML 解析器实现 (简化版)
// ============================================================================

RdfXmlParser::RdfXmlParser() {}

std::optional<Ontology> RdfXmlParser::parse(const String& content) {
    auto graph = parseRdf(content);
    if (!graph) {
        return std::nullopt;
    }
    return OwlToOntologyConverter::convert(*graph);
}

std::optional<RdfGraph> RdfXmlParser::parseRdf(const String& content) {
    RdfGraph graph;

    // 简化的 XML 解析
    auto root = parseXml(content);
    if (!root) {
        return std::nullopt;
    }

    parseRdfElement(*root, graph);
    return graph;
}

std::shared_ptr<RdfXmlParser::XmlNode> RdfXmlParser::parseXml(const String& content) {
    // 完整的 XML 解析实现
    auto root = std::make_shared<XmlNode>();
    root->name = "root";

    size_t i = 0;

    // 跳过 XML 声明
    if (content.find("<?xml") == 0) {
        i = content.find("?>");
        if (i != String::npos) i += 2;
    }

    // 解析命名空间
    std::regex nsRegex("xmlns:([a-zA-Z0-9]+)=\"([^\"]+)\"");
    auto nsBegin = std::sregex_iterator(content.begin(), content.end(), nsRegex);
    auto nsEnd = std::sregex_iterator();
    for (auto it = nsBegin; it != nsEnd; ++it) {
        String prefix = (*it)[1].str();
        String iri = (*it)[2].str();
        namespaces_[prefix] = iri;
    }

    // 递归解析元素
    std::function<std::shared_ptr<XmlNode>(size_t&, size_t)> parseElement;
    parseElement = [&](size_t& pos, size_t end) -> std::shared_ptr<XmlNode> {
        auto node = std::make_shared<XmlNode>();

        // 跳过空白
        while (pos < end && std::isspace(content[pos])) pos++;
        if (pos >= end || content[pos] != '<') return nullptr;

        pos++; // 跳过 <

        // 检查结束标签
        if (content[pos] == '/') return nullptr;

        // 检查注释
        if (pos + 2 < content.size() && content[pos] == '!' && content[pos+1] == '-' && content[pos+2] == '-') {
            pos += 3;
            while (pos + 2 < content.size() && !(content[pos] == '-' && content[pos+1] == '-' && content[pos+2] == '>')) pos++;
            pos += 3;
            return nullptr;
        }

        // 读取元素名
        while (pos < end && !std::isspace(content[pos]) && content[pos] != '>' && content[pos] != '/') {
            node->name += content[pos++];
        }

        // 读取属性
        while (pos < end && content[pos] != '>' && content[pos] != '/') {
            while (pos < end && std::isspace(content[pos])) pos++;

            if (content[pos] == '>' || content[pos] == '/') break;

            String attrName, attrValue;
            while (pos < end && !std::isspace(content[pos]) && content[pos] != '=') {
                attrName += content[pos++];
            }

            if (content[pos] == '=') pos++;
            while (pos < end && std::isspace(content[pos])) pos++;

            if (content[pos] == '"') {
                pos++;
                while (pos < end && content[pos] != '"') {
                    attrValue += content[pos++];
                }
                if (pos < end) pos++;
            }

            if (!attrName.empty()) {
                node->attributes.push_back({attrName, attrValue});

                // 提取命名空间
                if (attrName.find("xmlns:") == 0) {
                    String prefix = attrName.substr(6);
                    namespaces_[prefix] = attrValue;
                }
            }
        }

        // 自闭合标签
        if (content[pos] == '/') {
            pos += 2; // 跳过 />
            return node;
        }

        if (content[pos] == '>') pos++;

        // 读取内容和子元素
        while (pos < end) {
            // 跳过空白
            size_t textStart = pos;
            while (pos < end && content[pos] != '<') {
                node->text += content[pos++];
            }

            // 去除文本两端空白
            while (!node->text.empty() && std::isspace(node->text.front())) node->text.erase(0, 1);
            while (!node->text.empty() && std::isspace(node->text.back())) node->text.pop_back();

            if (pos >= end) break;

            // 检查结束标签
            if (pos + 1 < content.size() && content[pos] == '<' && content[pos+1] == '/') {
                pos += 2;
                while (pos < end && content[pos] != '>') pos++;
                if (pos < end) pos++;
                break;
            }

            // 解析子元素
            auto child = parseElement(pos, end);
            if (child && !child->name.empty()) {
                node->children.push_back(child);
            } else {
                pos++;
            }
        }

        return node;
    };

    size_t pos = i;
    while (pos < content.size()) {
        auto child = parseElement(pos, content.size());
        if (child && !child->name.empty()) {
            root->children.push_back(child);
        } else {
            pos++;
        }
    }

    return root;
}

void RdfXmlParser::parseRdfElement(const XmlNode& node, RdfGraph& graph) {
    // 处理 rdf:RDF 根元素
    if (node.name == "rdf:RDF" || node.name.find("RDF") != String::npos) {
        for (const auto& child : node.children) {
            parseRdfElement(*child, graph);
        }
        return;
    }

    // 获取主体
    String subject;
    for (const auto& [name, value] : node.attributes) {
        if (name == "rdf:about" || name == "about") {
            subject = value;
        } else if (name == "rdf:ID") {
            subject = baseIri_ + "#" + value;
        }
    }

    // 如果没有 subject，可能是空白节点
    if (subject.empty()) {
        static int blankCounter = 0;
        subject = "_:b" + std::to_string(blankCounter++);
    }

    // 元素名作为类型
    if (node.name != "rdf:Description" && node.name != "Description") {
        String type = node.name;
        // 解析带命名空间的元素名
        auto colonPos = node.name.find(':');
        if (colonPos != String::npos) {
            String prefix = node.name.substr(0, colonPos);
            String local = node.name.substr(colonPos + 1);
            auto nsIt = namespaces_.find(prefix);
            if (nsIt != namespaces_.end()) {
                type = nsIt->second + local;
            }
        }
        graph.addTriple({subject, OwlToOntologyConverter::RDF_NS + "type", type, false, "", ""});
    }

    // 处理属性
    for (const auto& [name, value] : node.attributes) {
        // 跳过 RDF 特殊属性
        if (name == "rdf:about" || name == "rdf:ID" || name == "rdf:resource" ||
            name == "rdf:parseType" || name.find("xmlns") == 0) {
            continue;
        }

        // 解析属性名
        String predicate = name;
        auto colonPos = name.find(':');
        if (colonPos != String::npos) {
            String prefix = name.substr(0, colonPos);
            String local = name.substr(colonPos + 1);
            auto nsIt = namespaces_.find(prefix);
            if (nsIt != namespaces_.end()) {
                predicate = nsIt->second + local;
            }
        }

        // 判断是否为字面量
        bool isLiteral = value.find("://") == String::npos || name.find("rdf:") != 0;

        graph.addTriple({subject, predicate, value, isLiteral, "", ""});
    }

    // 处理子元素
    for (const auto& child : node.children) {
        String predicate = child->name;
        auto colonPos = child->name.find(':');
        if (colonPos != String::npos) {
            String prefix = child->name.substr(0, colonPos);
            String local = child->name.substr(colonPos + 1);
            auto nsIt = namespaces_.find(prefix);
            if (nsIt != namespaces_.end()) {
                predicate = nsIt->second + local;
            }
        }

        // 获取对象
        String object;
        bool isLiteral = true;

        // 检查 rdf:resource 属性
        for (const auto& [name, value] : child->attributes) {
            if (name == "rdf:resource" || name == "resource") {
                object = value;
                isLiteral = false;
            }
        }

        // 如果没有 resource，检查文本内容
        if (object.empty() && !child->text.empty()) {
            object = child->text;
            isLiteral = true;
        }

        // 检查是否有子元素作为嵌套资源
        if (object.empty() && !child->children.empty()) {
            // 嵌套资源
            static int nestedCounter = 0;
            object = "_:nested" + std::to_string(nestedCounter++);
            isLiteral = false;

            // 递归处理嵌套元素
            for (const auto& nested : child->children) {
                parseRdfElement(*nested, graph);
            }
        }

        if (!predicate.empty() && !object.empty()) {
            graph.addTriple({subject, predicate, object, isLiteral, "", ""});
        }

        // 处理子元素的属性
        for (const auto& [name, value] : child->attributes) {
            if (name == "rdf:about" || name == "rdf:ID" || name == "rdf:resource" ||
                name == "rdf:parseType" || name.find("xmlns") == 0) {
                continue;
            }

            String attrPred = name;
            String attrObj = value;
            bool attrIsLit = true;

            if (!object.empty() && !isLiteral) {
                // 子元素有 rdf:resource，添加属性到目标资源
                auto colonPos = name.find(':');
                if (colonPos != String::npos) {
                    String prefix = name.substr(0, colonPos);
                    String local = name.substr(colonPos + 1);
                    auto nsIt = namespaces_.find(prefix);
                    if (nsIt != namespaces_.end()) {
                        attrPred = nsIt->second + local;
                    }
                }
                graph.addTriple({object, attrPred, attrObj, attrIsLit, "", ""});
            }
        }
    }
}

// ============================================================================
// N-Triples 实现
// ============================================================================

std::optional<Ontology> NTriplesParser::parse(const String& content) {
    auto graph = parseRdf(content);
    if (!graph) return std::nullopt;
    return OwlToOntologyConverter::convert(*graph);
}

std::optional<RdfGraph> NTriplesParser::parseRdf(const String& content) {
    RdfGraph graph;

    std::istringstream iss(content);
    String line;

    while (std::getline(iss, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') continue;

        // 解析 N-Triple: <subject> <predicate> <object> .
        std::regex tripleRegex("<([^>]+)>\\s+<([^>]+)>\\s+(<([^>]+)>|\"([^\"]*)\")\\s*\\.");
        std::smatch match;

        if (std::regex_match(line, match, tripleRegex)) {
            RdfTriple t;
            t.subject = match[1].str();
            t.predicate = match[2].str();

            if (match[4].matched) {
                // 对象是 IRI
                t.object = match[4].str();
                t.isLiteral = false;
            } else if (match[5].matched) {
                // 对象是字面量
                t.object = match[5].str();
                t.isLiteral = true;
            }

            graph.triples.push_back(t);
        }
    }

    return graph;
}

String NTriplesWriter::write(const Ontology& ontology) {
    std::ostringstream oss;

    String base = ontology.baseIRI.empty() ? "http://example.org/ontology#" : ontology.baseIRI;

    // 写入类
    for (const auto& [id, cls] : ontology.classes) {
        oss << "<" << base << id << "> <" << OwlToOntologyConverter::RDF_NS << "type> <" << OwlToOntologyConverter::OWL_NS << "Class> .\n";
        oss << "<" << base << id << "> <" << OwlToOntologyConverter::RDFS_NS << "label> \"" << cls.name << "\" .\n";
        for (const auto& super : cls.superClasses) {
            oss << "<" << base << id << "> <" << OwlToOntologyConverter::RDFS_NS << "subClassOf> <" << base << super << "> .\n";
        }
    }

    // 写入个体
    for (const auto& [id, ind] : ontology.individuals) {
        oss << "<" << base << id << "> <" << OwlToOntologyConverter::RDF_NS << "type> <" << base << ind.classId << "> .\n";
        for (const auto& [relId, targets] : ind.relations) {
            for (const auto& target : targets) {
                oss << "<" << base << id << "> <" << base << relId << "> <" << base << target << "> .\n";
            }
        }
    }

    return oss.str();
}

String NTriplesWriter::writeRdf(const RdfGraph& graph) {
    std::ostringstream oss;

    for (const auto& t : graph.triples) {
        oss << "<" << t.subject << "> <" << t.predicate << "> ";
        if (t.isLiteral) {
            oss << "\"" << t.object << "\"";
        } else {
            oss << "<" << t.object << ">";
        }
        oss << " .\n";
    }

    return oss.str();
}

// ============================================================================
// OWL 到本体转换
// ============================================================================

Ontology OwlToOntologyConverter::convert(const RdfGraph& graph) {
    Ontology ontology;

    String base = graph.baseIRI;
    if (base.empty()) {
        base = "http://example.org/ontology#";
    }
    ontology.baseIRI = base;

    // 遍历三元组，识别 OWL 构造
    std::unordered_map<String, String> types;  // subject -> type

    // 第一遍：收集类型
    for (const auto& t : graph.triples) {
        if (t.predicate == OwlToOntologyConverter::RDF_NS + "type") {
            types[t.subject] = t.object;
        }
    }

    // 第二遍：构建本体
    for (const auto& t : graph.triples) {
        String localSubject = t.subject;
        if (localSubject.find(base) == 0) {
            localSubject = localSubject.substr(base.length());
        }

        String localObject = t.object;
        if (localObject.find(base) == 0) {
            localObject = localObject.substr(base.length());
        }

        String type = types[t.subject];

        // OWL Class
        if (type == OwlToOntologyConverter::OWL_NS + "Class" || t.predicate == OwlToOntologyConverter::RDFS_NS + "subClassOf") {
            Class& cls = ontology.classes[localSubject];
            cls.id = localSubject;

            if (t.predicate == OwlToOntologyConverter::RDFS_NS + "label") {
                cls.name = t.object;
            } else if (t.predicate == OwlToOntologyConverter::RDFS_NS + "comment") {
                cls.description = t.object;
            } else if (t.predicate == OwlToOntologyConverter::RDFS_NS + "subClassOf") {
                cls.superClasses.push_back(localObject);
            } else if (t.predicate == OwlToOntologyConverter::OWL_NS + "equivalentClass") {
                cls.equivalentClasses.push_back(localObject);
            } else if (t.predicate == OwlToOntologyConverter::OWL_NS + "disjointWith") {
                cls.disjointClasses.push_back(localObject);
            }
        }

        // Object Property
        else if (type == OwlToOntologyConverter::OWL_NS + "ObjectProperty") {
            Relation& rel = ontology.relations[localSubject];
            rel.id = localSubject;

            if (t.predicate == OwlToOntologyConverter::RDFS_NS + "label") {
                rel.name = t.object;
            } else if (t.predicate == OwlToOntologyConverter::RDFS_NS + "domain") {
                rel.domain = localObject;
            } else if (t.predicate == OwlToOntologyConverter::RDFS_NS + "range") {
                rel.range = localObject;
            } else if (t.predicate == OwlToOntologyConverter::OWL_NS + "inverseOf") {
                rel.inverseProperty = localObject;
            }
        }

        // Individual
        else if (t.predicate == OwlToOntologyConverter::RDF_NS + "type" && t.object != OwlToOntologyConverter::OWL_NS + "Class" &&
                 t.object != OwlToOntologyConverter::OWL_NS + "ObjectProperty" && t.object != OwlToOntologyConverter::OWL_NS + "DatatypeProperty") {
            Individual& ind = ontology.individuals[localSubject];
            ind.id = localSubject;
            ind.classId = localObject;
        }
        else if (!ontology.individuals.empty() &&
                 ontology.individuals.find(localSubject) != ontology.individuals.end()) {
            Individual& ind = ontology.individuals[localSubject];

            if (t.predicate == OwlToOntologyConverter::RDFS_NS + "label") {
                ind.name = t.object;
            } else if (!t.isLiteral) {
                // 对象属性
                String localPred = t.predicate;
                if (localPred.find(base) == 0) {
                    localPred = localPred.substr(base.length());
                }
                ind.relations[localPred].push_back(localObject);
            }
        }
    }

    return ontology;
}

// ============================================================================
// OntologyIO 实现
// ============================================================================

OntologyIO::Format OntologyIO::detectFormat(const String& filename) {
    auto pos = filename.rfind('.');
    if (pos == String::npos) return Format::Auto;

    String ext = filename.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == "ttl") return Format::Turtle;
    if (ext == "rdf" || ext == "owl") return Format::RdfXml;
    if (ext == "owx") return Format::OwlXml;
    if (ext == "nt") return Format::NTriples;
    if (ext == "jsonld") return Format::JsonLd;
    if (ext == "json") return Format::Json;

    return Format::Auto;
}

std::optional<Ontology> OntologyIO::parse(const String& content, Format format) {
    auto parser = getParser(format);
    if (!parser) return std::nullopt;
    return parser->parse(content);
}

std::optional<Ontology> OntologyIO::parseFile(const String& path) {
    Format format = detectFormat(path);
    auto parser = getParser(format);
    if (!parser) return std::nullopt;
    return parser->parseFile(path);
}

String OntologyIO::write(const Ontology& ontology, Format format) {
    auto writer = getWriter(format);
    if (!writer) return "";
    return writer->write(ontology);
}

bool OntologyIO::writeFile(const String& path, const Ontology& ontology, Format format) {
    if (format == Format::Auto) {
        format = detectFormat(path);
    }
    auto writer = getWriter(format);
    if (!writer) return false;
    return writer->writeFile(path, ontology);
}

std::unique_ptr<OntologyParser> OntologyIO::getParser(Format format) {
    switch (format) {
        case Format::Turtle:
            return std::make_unique<TurtleParser>();
        case Format::RdfXml:
            return std::make_unique<RdfXmlParser>();
        case Format::OwlXml:
            return std::make_unique<OwlXmlParser>();
        case Format::NTriples:
            return std::make_unique<NTriplesParser>();
        case Format::JsonLd:
            return std::make_unique<JsonLdParser>();
        default:
            return std::make_unique<TurtleParser>();
    }
}

std::unique_ptr<OntologyWriter> OntologyIO::getWriter(Format format) {
    switch (format) {
        case Format::Turtle:
            return std::make_unique<TurtleWriter>();
        case Format::RdfXml:
            return std::make_unique<RdfXmlWriter>();
        case Format::OwlXml:
            return std::make_unique<OwlXmlWriter>();
        case Format::NTriples:
            return std::make_unique<NTriplesWriter>();
        case Format::JsonLd:
            return std::make_unique<JsonLdWriter>();
        default:
            return std::make_unique<TurtleWriter>();
    }
}

// ============================================================================
// JSON-LD 简化实现
// ============================================================================

std::optional<Ontology> JsonLdParser::parse(const String& content) {
    auto graph = parseRdf(content);
    if (!graph) return std::nullopt;
    return OwlToOntologyConverter::convert(*graph);
}

std::optional<RdfGraph> JsonLdParser::parseRdf(const String& content) {
    RdfGraph graph;

    try {
        Json j = Json::parse(content);

        // 解析 @context
        if (j.contains("@context")) {
            auto& ctx = j["@context"];
            if (ctx.is_object()) {
                for (auto& [key, value] : ctx.items()) {
                    if (key == "@base") {
                        graph.baseIRI = value.get<String>();
                    } else if (value.is_string()) {
                        graph.prefixes[key] = value.get<String>();
                    }
                }
            }
        }

        // 获取数据节点
        Json nodes;
        if (j.contains("@graph")) {
            nodes = j["@graph"];
        } else if (j.contains("@id")) {
            // 单个节点
            nodes = Json::array({j});
        } else {
            nodes = Json::array();
        }

        // 解析每个节点
        for (const auto& node : nodes) {
            if (!node.contains("@id")) continue;

            String subject = node["@id"].get<String>();
            String base = graph.baseIRI.empty() ? "" : graph.baseIRI;

            // 如果 subject 不是完整 IRI，添加 base
            if (subject.find("://") == String::npos && !base.empty()) {
                subject = base + subject;
            }

            // 处理 @type
            if (node.contains("@type")) {
                auto& types = node["@type"];
                if (types.is_string()) {
                    String type = types.get<String>();
                    if (type.find("://") == String::npos && !base.empty()) {
                        type = base + type;
                    }
                    graph.addTriple({subject, OwlToOntologyConverter::RDF_NS + "type", type, false, "", ""});
                } else if (types.is_array()) {
                    for (const auto& t : types) {
                        String type = t.get<String>();
                        if (type.find("://") == String::npos && !base.empty()) {
                            type = base + type;
                        }
                        graph.addTriple({subject, OwlToOntologyConverter::RDF_NS + "type", type, false, "", ""});
                    }
                }
            }

            // 处理其他属性
            for (auto& [key, value] : node.items()) {
                if (key == "@id" || key == "@type" || key == "@context") continue;

                // 解析属性名
                String predicate = key;
                auto colonPos = key.find(':');
                if (colonPos != String::npos) {
                    String prefix = key.substr(0, colonPos);
                    String local = key.substr(colonPos + 1);
                    auto it = graph.prefixes.find(prefix);
                    if (it != graph.prefixes.end()) {
                        predicate = it->second + local;
                    }
                } else if (!base.empty()) {
                    predicate = base + key;
                }

                // 解析值
                if (value.is_string()) {
                    String obj = value.get<String>();
                    bool isLiteral = obj.find("://") == String::npos;
                    graph.addTriple({subject, predicate, obj, isLiteral, "", ""});
                } else if (value.is_number()) {
                    String obj = std::to_string(value.get<double>());
                    graph.addTriple({subject, predicate, obj, true, OwlToOntologyConverter::XSD_NS + "double", ""});
                } else if (value.is_boolean()) {
                    String obj = value.get<bool>() ? "true" : "false";
                    graph.addTriple({subject, predicate, obj, true, OwlToOntologyConverter::XSD_NS + "boolean", ""});
                } else if (value.is_object()) {
                    // 嵌套对象 - 检查 @id
                    if (value.contains("@id")) {
                        String obj = value["@id"].get<String>();
                        if (obj.find("://") == String::npos && !base.empty()) {
                            obj = base + obj;
                        }
                        graph.addTriple({subject, predicate, obj, false, "", ""});
                    } else if (value.contains("@value")) {
                        // 类型化字面量
                        String obj = value["@value"].get<String>();
                        String datatype, lang;
                        if (value.contains("@type")) {
                            datatype = value["@type"].get<String>();
                        }
                        if (value.contains("@language")) {
                            lang = value["@language"].get<String>();
                        }
                        graph.addTriple({subject, predicate, obj, true, datatype, lang});
                    }
                } else if (value.is_array()) {
                    for (const auto& item : value) {
                        if (item.is_string()) {
                            String obj = item.get<String>();
                            bool isLiteral = obj.find("://") == String::npos;
                            graph.addTriple({subject, predicate, obj, isLiteral, "", ""});
                        } else if (item.is_object() && item.contains("@id")) {
                            String obj = item["@id"].get<String>();
                            if (obj.find("://") == String::npos && !base.empty()) {
                                obj = base + obj;
                            }
                            graph.addTriple({subject, predicate, obj, false, "", ""});
                        }
                    }
                }
            }
        }

        return graph;
    } catch (const std::exception& e) {
        return std::nullopt;
    }
}

String JsonLdWriter::write(const Ontology& ontology) {
    // 转换为 JSON-LD 格式
    Json j;

    j["@context"] = Json{
        {"owl", OwlToOntologyConverter::OWL_NS},
        {"rdf", OwlToOntologyConverter::RDF_NS},
        {"rdfs", OwlToOntologyConverter::RDFS_NS},
        {"xsd", OwlToOntologyConverter::XSD_NS},
        {"@base", ontology.baseIRI.empty() ? "http://example.org/ontology#" : ontology.baseIRI}
    };

    j["@graph"] = Json::array();

    // 类
    for (const auto& [id, cls] : ontology.classes) {
        Json node;
        node["@id"] = id;
        node["@type"] = "owl:Class";
        node["rdfs:label"] = cls.name;
        if (!cls.description.empty()) {
            node["rdfs:comment"] = cls.description;
        }
        j["@graph"].push_back(node);
    }

    // 个体
    for (const auto& [id, ind] : ontology.individuals) {
        Json node;
        node["@id"] = id;
        node["@type"] = ind.classId;
        node["rdfs:label"] = ind.name;
        j["@graph"].push_back(node);
    }

    return j.dump(2);
}

String JsonLdWriter::resolvePrefixInIri(const String& value, const PrefixMap& prefixes) {
    // Already a full IRI (angle-bracketed or absolute URI)
    if (value.size() > 1 && value.front() == '<' && value.back() == '>') {
        return value.substr(1, value.size() - 2);
    }
    if (value.find("://") != String::npos) {
        return value;
    }
    // Blank node
    if (value.substr(0, 2) == "_:") {
        return value;
    }
    // Prefixed name: prefix:local
    auto colonPos = value.find(':');
    if (colonPos != String::npos) {
        String prefix = value.substr(0, colonPos);
        String local = value.substr(colonPos + 1);
        auto it = prefixes.find(prefix);
        if (it != prefixes.end()) {
            return it->second + local;
        }
    }
    return value;
}

String JsonLdWriter::writeRdf(const RdfGraph& graph) {
    Json j;

    // 1. Build @context from prefixes
    Json context = Json::object();
    for (const auto& [prefix, iri] : graph.prefixes) {
        context[prefix] = iri;
    }
    if (!graph.baseIRI.empty()) {
        context["@base"] = graph.baseIRI;
    }
    j["@context"] = context;

    // 2. Group triples by subject
    std::vector<String> subjectOrder;
    std::unordered_map<String, std::vector<const RdfTriple*>> bySubject;
    for (const auto& triple : graph.triples) {
        const String& subj = triple.subject;
        if (bySubject.find(subj) == bySubject.end()) {
            subjectOrder.push_back(subj);
        }
        bySubject[subj].push_back(&triple);
    }

    // 3. Build @graph
    const String RDF_TYPE = OwlToOntologyConverter::RDF_NS + "type";
    const String XSD_NS = OwlToOntologyConverter::XSD_NS;

    j["@graph"] = Json::array();

    for (const auto& subj : subjectOrder) {
        Json node;
        // Resolve subject to full IRI for @id
        String resolvedSubj = resolvePrefixInIri(subj, graph.prefixes);
        node["@id"] = resolvedSubj;

        // Collect types and properties
        std::vector<String> types;
        std::unordered_map<String, std::vector<Json>> properties;

        for (const auto* triple : bySubject[subj]) {
            String predIri = resolvePrefixInIri(triple->predicate, graph.prefixes);

            if (predIri == RDF_TYPE) {
                // rdf:type -> @type
                String objIri = resolvePrefixInIri(triple->object, graph.prefixes);
                types.push_back(objIri);
            } else {
                // Other predicates -> properties
                Json value;
                if (triple->isLiteral) {
                    if (!triple->language.empty()) {
                        // Language-tagged literal
                        value = Json::object();
                        value["@value"] = triple->object;
                        value["@language"] = triple->language;
                    } else if (!triple->datatype.empty() && triple->datatype != "xsd:string") {
                        String dtIri = resolvePrefixInIri(triple->datatype, graph.prefixes);
                        if (dtIri == XSD_NS + "integer") {
                            try { value = std::stoi(triple->object); }
                            catch (...) { value = triple->object; }
                        } else if (dtIri == XSD_NS + "float" || dtIri == XSD_NS + "double" || dtIri == XSD_NS + "decimal") {
                            try { value = std::stod(triple->object); }
                            catch (...) { value = triple->object; }
                        } else if (dtIri == XSD_NS + "boolean") {
                            value = (triple->object == "true" || triple->object == "1");
                        } else {
                            value = Json::object();
                            value["@value"] = triple->object;
                            value["@type"] = dtIri;
                        }
                    } else {
                        // xsd:string or no datatype -> plain string
                        value = triple->object;
                    }
                } else {
                    // IRI object
                    String objIri = resolvePrefixInIri(triple->object, graph.prefixes);
                    value = Json::object();
                    value["@id"] = objIri;
                }

                // Use the prefixed name as the JSON-LD property key
                // (so @context prefix expansion works)
                String predKey = triple->predicate;
                // Strip angle brackets if present
                if (predKey.size() > 1 && predKey.front() == '<' && predKey.back() == '>') {
                    predKey = predKey.substr(1, predKey.size() - 2);
                }
                properties[predKey].push_back(value);
            }
        }

        // @type: single string or array
        if (!types.empty()) {
            if (types.size() == 1) {
                node["@type"] = types[0];
            } else {
                node["@type"] = types;
            }
        }

        // Properties: single value or array
        for (const auto& [predKey, values] : properties) {
            if (values.size() == 1) {
                node[predKey] = values[0];
            } else {
                node[predKey] = values;
            }
        }

        j["@graph"].push_back(node);
    }

    return j.dump(2);
}

// ============================================================================
// OWL/XML 解析器/写入器 (简化)
// ============================================================================

OwlXmlParser::OwlXmlParser() {}

std::optional<Ontology> OwlXmlParser::parse(const String& content) {
    auto graph = parseRdf(content);
    if (!graph) return std::nullopt;
    return OwlToOntologyConverter::convert(*graph);
}

std::optional<RdfGraph> OwlXmlParser::parseRdf(const String& content) {
    RdfGraph graph;

    // OWL/XML 使用不同的结构，但我们可以转换为 RDF
    // 首先使用 RDF/XML 解析器的基础设施
    auto root = parseOwlXml(content);
    if (!root) return std::nullopt;

    // 默认命名空间
    graph.prefixes["owl"] = OwlToOntologyConverter::OWL_NS;
    graph.prefixes["rdf"] = OwlToOntologyConverter::RDF_NS;
    graph.prefixes["rdfs"] = OwlToOntologyConverter::RDFS_NS;
    graph.prefixes["xsd"] = OwlToOntologyConverter::XSD_NS;

    parseOwlElement(*root, graph, "");
    return graph;
}

std::shared_ptr<OwlXmlParser::XmlNode> OwlXmlParser::parseOwlXml(const String& content) {
    auto root = std::make_shared<XmlNode>();
    root->name = "root";

    size_t i = 0;

    // 跳过 XML 声明
    if (content.find("<?xml") == 0) {
        i = content.find("?>");
        if (i != String::npos) i += 2;
    }

    // 递归解析元素
    std::function<std::shared_ptr<XmlNode>(size_t&, size_t)> parseElement;
    parseElement = [&](size_t& pos, size_t end) -> std::shared_ptr<XmlNode> {
        auto node = std::make_shared<XmlNode>();

        while (pos < end && std::isspace(content[pos])) pos++;
        if (pos >= end || content[pos] != '<') return nullptr;

        pos++;

        if (content[pos] == '/') return nullptr;

        // 检查注释
        if (pos + 2 < content.size() && content[pos] == '!' && content[pos+1] == '-' && content[pos+2] == '-') {
            pos += 3;
            while (pos + 2 < content.size() && !(content[pos] == '-' && content[pos+1] == '-' && content[pos+2] == '>')) pos++;
            pos += 3;
            return nullptr;
        }

        // 读取元素名
        while (pos < end && !std::isspace(content[pos]) && content[pos] != '>' && content[pos] != '/') {
            node->name += content[pos++];
        }

        // 读取属性
        while (pos < end && content[pos] != '>' && content[pos] != '/') {
            while (pos < end && std::isspace(content[pos])) pos++;
            if (content[pos] == '>' || content[pos] == '/') break;

            String attrName, attrValue;
            while (pos < end && !std::isspace(content[pos]) && content[pos] != '=') {
                attrName += content[pos++];
            }

            if (content[pos] == '=') pos++;
            while (pos < end && std::isspace(content[pos])) pos++;

            if (content[pos] == '"') {
                pos++;
                while (pos < end && content[pos] != '"') {
                    attrValue += content[pos++];
                }
                if (pos < end) pos++;
            }

            if (!attrName.empty()) {
                node->attributes.push_back({attrName, attrValue});
            }
        }

        // 自闭合标签
        if (content[pos] == '/') {
            pos += 2;
            return node;
        }

        if (content[pos] == '>') pos++;

        // 读取内容和子元素
        while (pos < end) {
            while (pos < end && content[pos] != '<') {
                node->text += content[pos++];
            }

            while (!node->text.empty() && std::isspace(node->text.front())) node->text.erase(0, 1);
            while (!node->text.empty() && std::isspace(node->text.back())) node->text.pop_back();

            if (pos >= end) break;

            if (pos + 1 < content.size() && content[pos] == '<' && content[pos+1] == '/') {
                pos += 2;
                while (pos < end && content[pos] != '>') pos++;
                if (pos < end) pos++;
                break;
            }

            auto child = parseElement(pos, end);
            if (child && !child->name.empty()) {
                node->children.push_back(child);
            } else {
                pos++;
            }
        }

        return node;
    };

    size_t pos = i;
    while (pos < content.size()) {
        auto child = parseElement(pos, content.size());
        if (child && !child->name.empty()) {
            root->children.push_back(child);
        } else {
            pos++;
        }
    }

    return root;
}

void OwlXmlParser::parseOwlElement(const XmlNode& node, RdfGraph& graph, const String& currentSubject) {
    String base = graph.baseIRI.empty() ? "http://example.org/ontology#" : graph.baseIRI;

    // 处理 OWL 本体
    if (node.name == "Ontology") {
        String ontologyIRI;
        for (const auto& [name, value] : node.attributes) {
            if (name == "ontologyIRI" || name == "IRI") {
                ontologyIRI = value;
                graph.baseIRI = value;
            }
        }
        for (const auto& child : node.children) {
            parseOwlElement(*child, graph, ontologyIRI);
        }
        return;
    }

    // 处理类声明
    if (node.name == "Declaration") {
        for (const auto& child : node.children) {
            parseOwlElement(*child, graph, "");
        }
        return;
    }

    // 处理 Class
    if (node.name == "Class") {
        String classIRI = getClassIRI(node);
        if (!classIRI.empty()) {
            graph.addTriple({classIRI, OwlToOntologyConverter::RDF_NS + "type", OwlToOntologyConverter::OWL_NS + "Class", false, "", ""});
        }
        return;
    }

    // 处理 ObjectProperty
    if (node.name == "ObjectProperty") {
        String propIRI = getPropertyIRI(node);
        if (!propIRI.empty()) {
            graph.addTriple({propIRI, OwlToOntologyConverter::RDF_NS + "type", OwlToOntologyConverter::OWL_NS + "ObjectProperty", false, "", ""});
        }
        return;
    }

    // 处理 DataProperty
    if (node.name == "DataProperty") {
        String propIRI = getPropertyIRI(node);
        if (!propIRI.empty()) {
            graph.addTriple({propIRI, OwlToOntologyConverter::RDF_NS + "type", OwlToOntologyConverter::OWL_NS + "DatatypeProperty", false, "", ""});
        }
        return;
    }

    // 处理 NamedIndividual
    if (node.name == "NamedIndividual") {
        String indIRI = getIndividualIRI(node);
        if (!indIRI.empty()) {
            graph.addTriple({indIRI, OwlToOntologyConverter::RDF_NS + "type", OwlToOntologyConverter::OWL_NS + "NamedIndividual", false, "", ""});
        }
        return;
    }

    // 处理 SubClassOf
    if (node.name == "SubClassOf") {
        if (node.children.size() >= 2) {
            String subClass = getClassIRI(*node.children[0]);
            String superClass = getClassIRI(*node.children[1]);
            if (!subClass.empty() && !superClass.empty()) {
                graph.addTriple({subClass, OwlToOntologyConverter::RDFS_NS + "subClassOf", superClass, false, "", ""});
            }
        }
        return;
    }

    // 处理 EquivalentClasses
    if (node.name == "EquivalentClasses") {
        if (node.children.size() >= 2) {
            String cls1 = getClassIRI(*node.children[0]);
            String cls2 = getClassIRI(*node.children[1]);
            if (!cls1.empty() && !cls2.empty()) {
                graph.addTriple({cls1, OwlToOntologyConverter::OWL_NS + "equivalentClass", cls2, false, "", ""});
                graph.addTriple({cls2, OwlToOntologyConverter::OWL_NS + "equivalentClass", cls1, false, "", ""});
            }
        }
        return;
    }

    // 处理 DisjointClasses
    if (node.name == "DisjointClasses") {
        if (node.children.size() >= 2) {
            String cls1 = getClassIRI(*node.children[0]);
            String cls2 = getClassIRI(*node.children[1]);
            if (!cls1.empty() && !cls2.empty()) {
                graph.addTriple({cls1, OwlToOntologyConverter::OWL_NS + "disjointWith", cls2, false, "", ""});
            }
        }
        return;
    }

    // 处理 SubObjectPropertyOf
    if (node.name == "SubObjectPropertyOf") {
        if (node.children.size() >= 2) {
            String subProp = getPropertyIRI(*node.children[0]);
            String superProp = getPropertyIRI(*node.children[1]);
            if (!subProp.empty() && !superProp.empty()) {
                graph.addTriple({subProp, OwlToOntologyConverter::RDFS_NS + "subPropertyOf", superProp, false, "", ""});
            }
        }
        return;
    }

    // 处理 ObjectPropertyDomain
    if (node.name == "ObjectPropertyDomain") {
        if (node.children.size() >= 2) {
            String prop = getPropertyIRI(*node.children[0]);
            String domain = getClassIRI(*node.children[1]);
            if (!prop.empty() && !domain.empty()) {
                graph.addTriple({prop, OwlToOntologyConverter::RDFS_NS + "domain", domain, false, "", ""});
            }
        }
        return;
    }

    // 处理 ObjectPropertyRange
    if (node.name == "ObjectPropertyRange") {
        if (node.children.size() >= 2) {
            String prop = getPropertyIRI(*node.children[0]);
            String range = getClassIRI(*node.children[1]);
            if (!prop.empty() && !range.empty()) {
                graph.addTriple({prop, OwlToOntologyConverter::RDFS_NS + "range", range, false, "", ""});
            }
        }
        return;
    }

    // 处理 ClassAssertion
    if (node.name == "ClassAssertion") {
        if (node.children.size() >= 2) {
            String cls = getClassIRI(*node.children[0]);
            String ind = getIndividualIRI(*node.children[1]);
            if (!cls.empty() && !ind.empty()) {
                graph.addTriple({ind, OwlToOntologyConverter::RDF_NS + "type", cls, false, "", ""});
            }
        }
        return;
    }

    // 处理 ObjectPropertyAssertion
    if (node.name == "ObjectPropertyAssertion") {
        if (node.children.size() >= 3) {
            String prop = getPropertyIRI(*node.children[0]);
            String subj = getIndividualIRI(*node.children[1]);
            String obj = getIndividualIRI(*node.children[2]);
            if (!prop.empty() && !subj.empty() && !obj.empty()) {
                graph.addTriple({subj, prop, obj, false, "", ""});
            }
        }
        return;
    }

    // 处理 DataPropertyAssertion
    if (node.name == "DataPropertyAssertion") {
        if (node.children.size() >= 3) {
            String prop = getPropertyIRI(*node.children[0]);
            String subj = getIndividualIRI(*node.children[1]);
            String value = getLiteralValue(*node.children[2]);
            if (!prop.empty() && !subj.empty()) {
                graph.addTriple({subj, prop, value, true, "", ""});
            }
        }
        return;
    }

    // 处理 TransitiveObjectProperty
    if (node.name == "TransitiveObjectProperty") {
        if (!node.children.empty()) {
            String prop = getPropertyIRI(*node.children[0]);
            if (!prop.empty()) {
                graph.addTriple({prop, OwlToOntologyConverter::RDF_NS + "type",
                    OwlToOntologyConverter::OWL_NS + "TransitiveProperty", false, "", ""});
            }
        }
        return;
    }

    // 处理 SymmetricObjectProperty
    if (node.name == "SymmetricObjectProperty") {
        if (!node.children.empty()) {
            String prop = getPropertyIRI(*node.children[0]);
            if (!prop.empty()) {
                graph.addTriple({prop, OwlToOntologyConverter::RDF_NS + "type",
                    OwlToOntologyConverter::OWL_NS + "SymmetricProperty", false, "", ""});
            }
        }
        return;
    }

    // 处理 AsymmetricObjectProperty
    if (node.name == "AsymmetricObjectProperty") {
        if (!node.children.empty()) {
            String prop = getPropertyIRI(*node.children[0]);
            if (!prop.empty()) {
                graph.addTriple({prop, OwlToOntologyConverter::RDF_NS + "type",
                    OwlToOntologyConverter::OWL_NS + "AsymmetricProperty", false, "", ""});
            }
        }
        return;
    }

    // 处理 ReflexiveObjectProperty
    if (node.name == "ReflexiveObjectProperty") {
        if (!node.children.empty()) {
            String prop = getPropertyIRI(*node.children[0]);
            if (!prop.empty()) {
                graph.addTriple({prop, OwlToOntologyConverter::RDF_NS + "type",
                    OwlToOntologyConverter::OWL_NS + "ReflexiveProperty", false, "", ""});
            }
        }
        return;
    }

    // 处理 IrreflexiveObjectProperty
    if (node.name == "IrreflexiveObjectProperty") {
        if (!node.children.empty()) {
            String prop = getPropertyIRI(*node.children[0]);
            if (!prop.empty()) {
                graph.addTriple({prop, OwlToOntologyConverter::RDF_NS + "type",
                    OwlToOntologyConverter::OWL_NS + "IrreflexiveProperty", false, "", ""});
            }
        }
        return;
    }

    // 处理 FunctionalObjectProperty
    if (node.name == "FunctionalObjectProperty") {
        if (!node.children.empty()) {
            String prop = getPropertyIRI(*node.children[0]);
            if (!prop.empty()) {
                graph.addTriple({prop, OwlToOntologyConverter::RDF_NS + "type",
                    OwlToOntologyConverter::OWL_NS + "FunctionalProperty", false, "", ""});
            }
        }
        return;
    }

    // 处理 InverseFunctionalObjectProperty
    if (node.name == "InverseFunctionalObjectProperty") {
        if (!node.children.empty()) {
            String prop = getPropertyIRI(*node.children[0]);
            if (!prop.empty()) {
                graph.addTriple({prop, OwlToOntologyConverter::RDF_NS + "type",
                    OwlToOntologyConverter::OWL_NS + "InverseFunctionalProperty", false, "", ""});
            }
        }
        return;
    }

    // 处理 InverseObjectProperties
    if (node.name == "InverseObjectProperties") {
        if (node.children.size() >= 2) {
            String prop1 = getPropertyIRI(*node.children[0]);
            String prop2 = getPropertyIRI(*node.children[1]);
            if (!prop1.empty() && !prop2.empty()) {
                graph.addTriple({prop1, OwlToOntologyConverter::OWL_NS + "inverseOf", prop2, false, "", ""});
                graph.addTriple({prop2, OwlToOntologyConverter::OWL_NS + "inverseOf", prop1, false, "", ""});
            }
        }
        return;
    }

    // 处理 SameIndividual
    if (node.name == "SameIndividual") {
        if (node.children.size() >= 2) {
            String ind1 = getIndividualIRI(*node.children[0]);
            String ind2 = getIndividualIRI(*node.children[1]);
            if (!ind1.empty() && !ind2.empty()) {
                graph.addTriple({ind1, OwlToOntologyConverter::OWL_NS + "sameAs", ind2, false, "", ""});
            }
        }
        return;
    }

    // 处理 DifferentIndividuals
    if (node.name == "DifferentIndividuals") {
        if (node.children.size() >= 2) {
            String ind1 = getIndividualIRI(*node.children[0]);
            String ind2 = getIndividualIRI(*node.children[1]);
            if (!ind1.empty() && !ind2.empty()) {
                graph.addTriple({ind1, OwlToOntologyConverter::OWL_NS + "differentFrom", ind2, false, "", ""});
            }
        }
        return;
    }

    // 处理 DisjointUnion
    if (node.name == "DisjointUnion") {
        if (node.children.size() >= 2) {
            String cls = getClassIRI(*node.children[0]);
            if (!cls.empty()) {
                // DisjointUnion: C ≡ D1 ⊔ ... ⊔ Dn
                // 生成等价类和不相交公理
                for (size_t i = 1; i < node.children.size(); ++i) {
                    String part = getClassIRI(*node.children[i]);
                    if (!part.empty()) {
                        // C 包含每个 Di
                        graph.addTriple({part, OwlToOntologyConverter::RDFS_NS + "subClassOf", cls, false, "", ""});
                    }
                }
            }
        }
        return;
    }

    // 处理 HasKey
    if (node.name == "HasKey") {
        if (node.children.size() >= 2) {
            String cls = getClassIRI(*node.children[0]);
            // HasKey 公理定义类的键属性
            // 简化处理：记录属性为关键属性
        }
        return;
    }

    // 处理 DatatypeDefinition
    if (node.name == "DatatypeDefinition") {
        if (node.children.size() >= 2) {
            String dt = node.children[0]->text;  // 数据类型 IRI
            // 数据类型定义使用数据范围表达式
        }
        return;
    }

    // 处理 AnnotationAssertion
    if (node.name == "AnnotationAssertion") {
        if (node.children.size() >= 3) {
            String prop = getPropertyIRI(*node.children[0]);
            String target;
            String value;

            // 目标可以是 IRI 或匿名个体
            if (node.children[1]->name == "IRI") {
                target = node.children[1]->text;
            }

            // 值可以是字面量
            if (node.children[2]->name == "Literal") {
                value = node.children[2]->text;
            }

            if (!prop.empty() && !target.empty()) {
                graph.addTriple({target, prop, value.empty() ? node.children[2]->text : value, true, "", ""});
            }
        }
        return;
    }

    // 递归处理子元素
    for (const auto& child : node.children) {
        parseOwlElement(*child, graph, currentSubject);
    }
}

String OwlXmlParser::getClassIRI(const XmlNode& node) {
    String base = "http://example.org/ontology#";

    if (node.name == "Class") {
        for (const auto& child : node.children) {
            if (child->name == "IRI") {
                String iri = child->text;
                if (iri.find("://") == String::npos) {
                    return base + iri;
                }
                return iri;
            }
        }
        // 检查 abbreviatedIRI 属性
        for (const auto& [name, value] : node.attributes) {
            if (name == "abbreviatedIRI") {
                return base + value;
            }
        }
    }

    if (node.name == "IRI") {
        String iri = node.text;
        if (iri.find("://") == String::npos) {
            return base + iri;
        }
        return iri;
    }

    return "";
}

String OwlXmlParser::getPropertyIRI(const XmlNode& node) {
    String base = "http://example.org/ontology#";

    if (node.name == "ObjectProperty" || node.name == "DataProperty" || node.name == "AnnotationProperty") {
        for (const auto& child : node.children) {
            if (child->name == "IRI") {
                String iri = child->text;
                if (iri.find("://") == String::npos) {
                    return base + iri;
                }
                return iri;
            }
        }
        for (const auto& [name, value] : node.attributes) {
            if (name == "abbreviatedIRI") {
                return base + value;
            }
        }
    }

    if (node.name == "IRI") {
        String iri = node.text;
        if (iri.find("://") == String::npos) {
            return base + iri;
        }
        return iri;
    }

    return "";
}

String OwlXmlParser::getIndividualIRI(const XmlNode& node) {
    String base = "http://example.org/ontology#";

    if (node.name == "NamedIndividual") {
        for (const auto& child : node.children) {
            if (child->name == "IRI") {
                String iri = child->text;
                if (iri.find("://") == String::npos) {
                    return base + iri;
                }
                return iri;
            }
        }
        for (const auto& [name, value] : node.attributes) {
            if (name == "abbreviatedIRI") {
                return base + value;
            }
        }
    }

    if (node.name == "IRI") {
        String iri = node.text;
        if (iri.find("://") == String::npos) {
            return base + iri;
        }
        return iri;
    }

    return "";
}

String OwlXmlParser::getLiteralValue(const XmlNode& node) {
    if (node.name == "Literal") {
        return node.text;
    }
    return "";
}

OwlXmlWriter::OwlXmlWriter() {}

namespace {

// Resolve a prefixed name (e.g. "ex:Dog") to a full IRI using the prefix map.
String resolvePrefixedName(const String& value, const PrefixMap& prefixes) {
    if (value.size() > 1 && value.front() == '<' && value.back() == '>') {
        return value.substr(1, value.size() - 2);
    }
    if (value.find("://") != String::npos) {
        return value;
    }
    if (value.substr(0, 2) == "_:") {
        return value;
    }
    auto colonPos = value.find(':');
    if (colonPos != String::npos) {
        String prefix = value.substr(0, colonPos);
        String local = value.substr(colonPos + 1);
        auto it = prefixes.find(prefix);
        if (it != prefixes.end()) {
            return it->second + local;
        }
    }
    return value;
}

String owlEscapeXml(const String& text) {
    String result;
    for (char c : text) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default: result += c;
        }
    }
    return result;
}

// Well-known IRI constants
static const String OWL_CLASS_IRI  = "http://www.w3.org/2002/07/owl#Class";
static const String OWL_OBJPROP_IRI = "http://www.w3.org/2002/07/owl#ObjectProperty";
static const String OWL_DATAPROP_IRI = "http://www.w3.org/2002/07/owl#DatatypeProperty";
static const String OWL_FUNC_PROP_IRI = "http://www.w3.org/2002/07/owl#FunctionalProperty";
static const String OWL_DISJOINT_IRI = "http://www.w3.org/2002/07/owl#disjointWith";
static const String RDF_TYPE_IRI   = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type";
static const String RDFS_SUBCLASS_IRI = "http://www.w3.org/2000/01/rdf-schema#subClassOf";
static const String RDFS_DOMAIN_IRI = "http://www.w3.org/2000/01/rdf-schema#domain";
static const String RDFS_RANGE_IRI  = "http://www.w3.org/2000/01/rdf-schema#range";

} // anonymous namespace

String OwlXmlWriter::write(const Ontology& ontology) {
    // Build an RdfGraph from the ontology, then serialize as OWL/XML
    String base = ontology.baseIRI.empty() ? "http://example.org/ontology#" : ontology.baseIRI;

    RdfGraph graph;
    graph.addPrefix("ex", base);
    graph.addPrefix("owl", "http://www.w3.org/2002/07/owl#");
    graph.addPrefix("rdf", "http://www.w3.org/1999/02/22-rdf-syntax-ns#");
    graph.addPrefix("rdfs", "http://www.w3.org/2000/01/rdf-schema#");

    // Emit classes
    for (const auto& [id, cls] : ontology.classes) {
        graph.addTriple({"ex:" + id, "rdf:type", "owl:Class", false, "", ""});
        for (const auto& super : cls.superClasses) {
            graph.addTriple({"ex:" + id, "rdfs:subClassOf", "ex:" + super, false, "", ""});
        }
        for (const auto& disj : cls.disjointClasses) {
            graph.addTriple({"ex:" + id, "owl:disjointWith", "ex:" + disj, false, "", ""});
        }
    }
    // Emit relations as object properties
    for (const auto& [id, rel] : ontology.relations) {
        graph.addTriple({"ex:" + id, "rdf:type", "owl:ObjectProperty", false, "", ""});
        if (!rel.domain.empty()) {
            graph.addTriple({"ex:" + id, "rdfs:domain", "ex:" + rel.domain, false, "", ""});
        }
        if (!rel.range.empty()) {
            graph.addTriple({"ex:" + id, "rdfs:range", "ex:" + rel.range, false, "", ""});
        }
    }
    // Emit individuals
    for (const auto& [id, ind] : ontology.individuals) {
        if (!ind.classId.empty()) {
            graph.addTriple({"ex:" + id, "rdf:type", "ex:" + ind.classId, false, "", ""});
        }
        for (const auto& [relId, targets] : ind.relations) {
            for (const auto& target : targets) {
                graph.addTriple({"ex:" + id, "ex:" + relId, "ex:" + target, false, "", ""});
            }
        }
    }

    return writeRdf(graph);
}

String OwlXmlWriter::writeRdf(const RdfGraph& graph) {
    std::ostringstream oss;

    // Categorise triples by resolved predicate
    std::vector<std::pair<String, String>> classDecls;        // class IRI
    std::vector<std::pair<String, String>> objPropDecls;      // object property IRI
    std::vector<std::pair<String, String>> dataPropDecls;     // datatype property IRI
    std::vector<std::pair<String, String>> funcPropDecls;     // functional property IRI
    std::vector<std::pair<String, String>> subClassOf;        // (sub, super)
    std::vector<std::pair<String, String>> disjointWith;      // (c1, c2)
    std::vector<std::pair<String, String>> propDomain;        // (prop, class)
    std::vector<std::pair<String, String>> propRange;         // (prop, class)
    std::vector<std::pair<String, String>> classAssertions;   // (individual, class)
    std::vector<std::tuple<String, String, String>> objPropAssertions; // (prop, subject, object)
    std::vector<const RdfTriple*> otherTriples;

    for (const auto& t : graph.triples) {
        String predIri = resolvePrefixedName(t.predicate, graph.prefixes);
        String subjIri = resolvePrefixedName(t.subject, graph.prefixes);
        String objIri  = resolvePrefixedName(t.object, graph.prefixes);

        if (predIri == RDF_TYPE_IRI) {
            if (objIri == OWL_CLASS_IRI) {
                classDecls.emplace_back(subjIri, objIri);
            } else if (objIri == OWL_OBJPROP_IRI) {
                objPropDecls.emplace_back(subjIri, objIri);
            } else if (objIri == OWL_DATAPROP_IRI) {
                dataPropDecls.emplace_back(subjIri, objIri);
            } else if (objIri == OWL_FUNC_PROP_IRI) {
                funcPropDecls.emplace_back(subjIri, objIri);
            } else {
                // Individual type assertion (not a built-in OWL metaclass)
                classAssertions.emplace_back(subjIri, objIri);
            }
        } else if (predIri == RDFS_SUBCLASS_IRI) {
            subClassOf.emplace_back(subjIri, objIri);
        } else if (predIri == OWL_DISJOINT_IRI) {
            disjointWith.emplace_back(subjIri, objIri);
        } else if (predIri == RDFS_DOMAIN_IRI) {
            propDomain.emplace_back(subjIri, objIri);
        } else if (predIri == RDFS_RANGE_IRI) {
            propRange.emplace_back(subjIri, objIri);
        } else {
            // Check if it's an object property assertion (subject and object are not literals)
            if (!t.isLiteral) {
                objPropAssertions.emplace_back(predIri, subjIri, objIri);
            } else {
                otherTriples.push_back(&t);
            }
        }
    }

    // Emit OWL/XML
    oss << "<?xml version=\"1.0\"?>\n";
    oss << "<Ontology xml:base=\"http://example.org/ontology\"\n";
    oss << "  xmlns=\"http://www.w3.org/2002/07/owl#\"\n";
    oss << "  xmlns:owl=\"http://www.w3.org/2002/07/owl#\"\n";
    oss << "  xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"\n";
    oss << "  xmlns:rdfs=\"http://www.w3.org/2000/01/rdf-schema#\"\n";
    oss << "  xmlns:xsd=\"http://www.w3.org/2001/XMLSchema#\">\n";

    // Declarations
    for (const auto& [iri, _] : classDecls) {
        oss << "  <Declaration><Class IRI=\"" << owlEscapeXml(iri) << "\"/></Declaration>\n";
    }
    for (const auto& [iri, _] : objPropDecls) {
        oss << "  <Declaration><ObjectProperty IRI=\"" << owlEscapeXml(iri) << "\"/></Declaration>\n";
    }
    for (const auto& [iri, _] : dataPropDecls) {
        oss << "  <Declaration><DataProperty IRI=\"" << owlEscapeXml(iri) << "\"/></Declaration>\n";
    }
    // Functional properties also need a Declaration + FunctionalObjectProperty axiom
    for (const auto& [iri, _] : funcPropDecls) {
        oss << "  <Declaration><ObjectProperty IRI=\"" << owlEscapeXml(iri) << "\"/></Declaration>\n";
        oss << "  <FunctionalObjectProperty><ObjectProperty IRI=\"" << owlEscapeXml(iri)
            << "\"/></FunctionalObjectProperty>\n";
    }

    // SubClassOf axioms
    for (const auto& [sub, sup] : subClassOf) {
        oss << "  <SubClassOf><Class IRI=\"" << owlEscapeXml(sub) << "\"/>"
            << "<Class IRI=\"" << owlEscapeXml(sup) << "\"/></SubClassOf>\n";
    }

    // DisjointClasses axioms
    for (const auto& [c1, c2] : disjointWith) {
        oss << "  <DisjointClasses><Class IRI=\"" << owlEscapeXml(c1) << "\"/>"
            << "<Class IRI=\"" << owlEscapeXml(c2) << "\"/></DisjointClasses>\n";
    }

    // ObjectPropertyDomain / ObjectPropertyRange
    for (const auto& [prop, cls] : propDomain) {
        oss << "  <ObjectPropertyDomain><ObjectProperty IRI=\"" << owlEscapeXml(prop) << "\"/>"
            << "<Class IRI=\"" << owlEscapeXml(cls) << "\"/></ObjectPropertyDomain>\n";
    }
    for (const auto& [prop, cls] : propRange) {
        oss << "  <ObjectPropertyRange><ObjectProperty IRI=\"" << owlEscapeXml(prop) << "\"/>"
            << "<Class IRI=\"" << owlEscapeXml(cls) << "\"/></ObjectPropertyRange>\n";
    }

    // ClassAssertion (individual rdf:type NonOwlClass)
    for (const auto& [individual, cls] : classAssertions) {
        oss << "  <ClassAssertion><Class IRI=\"" << owlEscapeXml(cls) << "\"/>"
            << "<NamedIndividual IRI=\"" << owlEscapeXml(individual) << "\"/></ClassAssertion>\n";
    }

    // ObjectPropertyAssertion
    for (const auto& [prop, subj, obj] : objPropAssertions) {
        oss << "  <ObjectPropertyAssertion>"
            << "<ObjectProperty IRI=\"" << owlEscapeXml(prop) << "\"/>"
            << "<NamedIndividual IRI=\"" << owlEscapeXml(subj) << "\"/>"
            << "<NamedIndividual IRI=\"" << owlEscapeXml(obj) << "\"/>"
            << "</ObjectPropertyAssertion>\n";
    }

    // Other triples as AnnotationAssertion (simplified)
    for (const auto* t : otherTriples) {
        String predIri = resolvePrefixedName(t->predicate, graph.prefixes);
        String subjIri = resolvePrefixedName(t->subject, graph.prefixes);
        if (t->isLiteral) {
            oss << "  <AnnotationAssertion>"
                << "<AnnotationProperty abbreviatedIRI=\"" << owlEscapeXml(t->predicate) << "\"/>"
                << "<IRI>" << owlEscapeXml(subjIri) << "</IRI>"
                << "<Literal>" << owlEscapeXml(t->object) << "</Literal>"
                << "</AnnotationAssertion>\n";
        }
    }

    oss << "</Ontology>\n";
    return oss.str();
}

} // namespace ontology
