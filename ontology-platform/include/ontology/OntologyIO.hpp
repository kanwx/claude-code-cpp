#pragma once

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "Core.hpp"

namespace ontology {

// ============================================================================
// RDF 相关类型定义
// ============================================================================

/// RDF 三元组
struct RdfTriple {
    String subject;
    String predicate;
    String object;
    bool isLiteral = false;
    String datatype;            // xsd:string, xsd:integer, etc.
    String language;            // 语言标签 @en, @zh
};

/// RDF 前缀映射
using PrefixMap = std::unordered_map<String, String>;

/// RDF 图
struct RdfGraph {
    std::vector<RdfTriple> triples;
    PrefixMap prefixes;
    String baseIRI;

    void addPrefix(const String& prefix, const String& iri) {
        prefixes[prefix] = iri;
    }

    void addTriple(const RdfTriple& triple) {
        triples.push_back(triple);
    }

    size_t size() const { return triples.size(); }
};

// ============================================================================
// 格式解析器接口
// ============================================================================

class OntologyParser {
public:
    virtual ~OntologyParser() = default;

    /// 解析本体文件
    virtual std::optional<Ontology> parse(const String& content) = 0;

    /// 解析 RDF 图
    virtual std::optional<RdfGraph> parseRdf(const String& content) = 0;

    /// 从文件解析
    std::optional<Ontology> parseFile(const String& path);
};

class OntologyWriter {
public:
    virtual ~OntologyWriter() = default;

    /// 序列化本体
    virtual String write(const Ontology& ontology) = 0;

    /// 序列化 RDF 图
    virtual String writeRdf(const RdfGraph& graph) = 0;

    /// 写入文件
    bool writeFile(const String& path, const Ontology& ontology);
};

// ============================================================================
// Turtle 格式解析器
// ============================================================================

class TurtleParser : public OntologyParser {
public:
    TurtleParser();

    std::optional<Ontology> parse(const String& content) override;
    std::optional<RdfGraph> parseRdf(const String& content) override;

private:
    // 词法分析
    struct Token {
        enum Type {
            IRI,            // <http://...>
            PREFIXED_NAME,  // prefix:local
            BLANK_NODE,     // _:id
            LITERAL,        // "value"
            PREFIX,         // @prefix
            BASE,           // @base
            DOT,            // .
            COMMA,          // ,
            SEMICOLON,      // ;
            LBRACKET,       // [
            RBRACKET,       // ]
            LPAREN,         // (
            RPAREN,         // )
            AT,             // @
            A,              // a (rdf:type shorthand)
            EOF_
        };

        Type type;
        String value;
        String datatype;
        String language;
    };

    std::vector<Token> tokenize(const String& content);
    String resolveIri(const String& prefixedName);
    String parseIri(const String& iriRef);

    PrefixMap prefixes_;
    String baseIri_;
    int blankNodeCounter_ = 0;

    String generateBlankNode() {
        return "_:b" + std::to_string(blankNodeCounter_++);
    }
};

class TurtleWriter : public OntologyWriter {
public:
    TurtleWriter();

    String write(const Ontology& ontology) override;
    String writeRdf(const RdfGraph& graph) override;

    void setIndent(int spaces) { indent_ = spaces; }
    void setUsePrefixes(bool use) { usePrefixes_ = use; }

private:
    String prefixForIri(const String& iri);
    String localNameForIri(const String& iri);
    String escapeLiteral(const String& literal);

    int indent_ = 2;
    bool usePrefixes_ = true;
    PrefixMap prefixes_;
};

// ============================================================================
// RDF/XML 格式解析器
// ============================================================================

class RdfXmlParser : public OntologyParser {
public:
    RdfXmlParser();

    std::optional<Ontology> parse(const String& content) override;
    std::optional<RdfGraph> parseRdf(const String& content) override;

private:
    // 简单的 XML 解析 (不依赖外部库)
    struct XmlNode {
        String name;                    // 带命名空间的元素名
        String text;                    // 文本内容
        std::vector<std::pair<String, String>> attributes;
        std::vector<std::shared_ptr<XmlNode>> children;

        String attr(const String& name) const {
            for (const auto& [n, v] : attributes) {
                if (n == name) return v;
            }
            return "";
        }
    };

    std::shared_ptr<XmlNode> parseXml(const String& content);
    void parseRdfElement(const XmlNode& node, RdfGraph& graph);
    String parseResource(const XmlNode& node);
    String parseLiteral(const XmlNode& node);

    PrefixMap namespaces_;
    String baseIri_;
};

class RdfXmlWriter : public OntologyWriter {
public:
    RdfXmlWriter();

    String write(const Ontology& ontology) override;
    String writeRdf(const RdfGraph& graph) override;

    void setIndent(int spaces) { indent_ = spaces; }

private:
    String escapeXml(const String& text);
    String indent(int level);

    int indent_ = 2;
};

// ============================================================================
// OWL/XML 格式解析器
// ============================================================================

class OwlXmlParser : public OntologyParser {
public:
    OwlXmlParser();

    std::optional<Ontology> parse(const String& content) override;
    std::optional<RdfGraph> parseRdf(const String& content) override;

private:
    // XML 节点结构
    struct XmlNode {
        String name;
        String text;
        std::vector<std::pair<String, String>> attributes;
        std::vector<std::shared_ptr<XmlNode>> children;

        String attr(const String& name) const {
            for (const auto& [n, v] : attributes) {
                if (n == name) return v;
            }
            return "";
        }
    };

    std::shared_ptr<XmlNode> parseOwlXml(const String& content);
    void parseOwlElement(const XmlNode& node, RdfGraph& graph, const String& currentSubject);
    String getClassIRI(const XmlNode& node);
    String getPropertyIRI(const XmlNode& node);
    String getIndividualIRI(const XmlNode& node);
    String getLiteralValue(const XmlNode& node);
};

class OwlXmlWriter : public OntologyWriter {
public:
    OwlXmlWriter();

    String write(const Ontology& ontology) override;
    String writeRdf(const RdfGraph& graph) override;

private:
    void writeClass(const Class& cls, std::ostringstream& oss, int level);
    void writeProperty(const Relation& rel, std::ostringstream& oss, int level);
    void writeIndividual(const Individual& ind, std::ostringstream& oss, int level);
};

// ============================================================================
// N-Triples 格式解析器
// ============================================================================

class NTriplesParser : public OntologyParser {
public:
    std::optional<Ontology> parse(const String& content) override;
    std::optional<RdfGraph> parseRdf(const String& content) override;
};

class NTriplesWriter : public OntologyWriter {
public:
    String write(const Ontology& ontology) override;
    String writeRdf(const RdfGraph& graph) override;
};

// ============================================================================
// JSON-LD 格式解析器
// ============================================================================

class JsonLdParser : public OntologyParser {
public:
    std::optional<Ontology> parse(const String& content) override;
    std::optional<RdfGraph> parseRdf(const String& content) override;
};

class JsonLdWriter : public OntologyWriter {
public:
    String write(const Ontology& ontology) override;
    String writeRdf(const RdfGraph& graph) override;

private:
    static String resolvePrefixInIri(const String& value, const PrefixMap& prefixes);
};

// ============================================================================
// 统一 I/O 接口
// ============================================================================

class OntologyIO {
public:
    /// 支持的格式
    enum class Format {
        Auto,       // 自动检测
        Turtle,     // .ttl
        RdfXml,     // .rdf, .owl
        OwlXml,     // .owx
        NTriples,   // .nt
        JsonLd,     // .jsonld
        Json        // .json (自定义格式)
    };

    /// 从格式检测
    static Format detectFormat(const String& filename);

    /// 从字符串解析
    static std::optional<Ontology> parse(const String& content, Format format = Format::Auto);

    /// 从文件解析
    static std::optional<Ontology> parseFile(const String& path);

    /// 序列化到字符串
    static String write(const Ontology& ontology, Format format);

    /// 序列化到文件
    static bool writeFile(const String& path, const Ontology& ontology, Format format = Format::Auto);

    /// 获取解析器
    static std::unique_ptr<OntologyParser> getParser(Format format);

    /// 获取写入器
    static std::unique_ptr<OntologyWriter> getWriter(Format format);
};

// ============================================================================
// OWL 到本体转换
// ============================================================================

class OwlToOntologyConverter {
public:
    /// 从 RDF 图构建本体
    static Ontology convert(const RdfGraph& graph);

    /// 设置 OWL 命名空间
    static const String OWL_NS;
    static const String RDF_NS;
    static const String RDFS_NS;
    static const String XSD_NS;

private:
    static bool isOwlClass(const RdfTriple& t);
    static bool isOwlProperty(const RdfTriple& t);
    static bool isOwlIndividual(const RdfTriple& t);
};

} // namespace ontology
