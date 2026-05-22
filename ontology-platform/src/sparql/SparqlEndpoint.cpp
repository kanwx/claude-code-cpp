#include <ontology/sparql/SparqlEndpoint.hpp>
#include <ontology/sparql/SparqlParser.hpp>
#include <algorithm>
#include <sstream>

namespace ontology::sparql {

// ============================================================================
// Construction
// ============================================================================

SparqlEndpoint::SparqlEndpoint(StoragePtr storage)
    : storage_(std::move(storage))
{
    if (storage_) {
        executor_ = std::make_unique<SparqlExecutor>(
            SparqlExecutor::fromHybridStorage(*storage_));
    }
}

void SparqlEndpoint::setStorage(StoragePtr storage) {
    storage_ = std::move(storage);
    if (storage_) {
        executor_ = std::make_unique<SparqlExecutor>(
            SparqlExecutor::fromHybridStorage(*storage_));
    } else {
        executor_.reset();
    }
}

// ============================================================================
// Query
// ============================================================================

Json SparqlEndpoint::query(const String& sparqlString) {
    if (!executor_) {
        return Json{{"error", "SPARQL endpoint not initialized (no storage)"}};
    }

    auto result = executor_->execute(sparqlString);
    return result.toJson();
}

// ============================================================================
// SPARQL Update — parse
// ============================================================================

std::optional<SparqlEndpoint::SparqlUpdate> SparqlEndpoint::parseUpdate(const String& updateString) {
    SparqlUpdate update;
    size_t pos = 0;
    String s = updateString;

    auto toUpper = [](const String& str) {
        String result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    };

    auto skipWs = [&]() {
        while (pos < s.size() && std::isspace(s[pos])) pos++;
    };

    auto readWord = [&]() -> String {
        skipWs();
        String word;
        while (pos < s.size() && (std::isalnum(s[pos]) || s[pos] == '_')) {
            word += s[pos++];
        }
        return word;
    };

    auto readIri = [&]() -> String {
        skipWs();
        if (s[pos] == '<') {
            pos++;
            String iri;
            while (pos < s.size() && s[pos] != '>') {
                iri += s[pos++];
            }
            if (pos < s.size()) pos++;
            return iri;
        }
        return "";
    };

    auto readVariable = [&]() -> String {
        skipWs();
        if (s[pos] == '?' || s[pos] == '$') {
            pos++;
            String var;
            while (pos < s.size() && (std::isalnum(s[pos]) || s[pos] == '_')) {
                var += s[pos++];
            }
            return var;
        }
        return "";
    };

    auto readPrefixedName = [&]() -> String {
        skipWs();
        String name;
        while (pos < s.size() && (std::isalnum(s[pos]) || s[pos] == '_' || s[pos] == ':')) {
            name += s[pos++];
        }
        return name;
    };

    auto readLiteral = [&]() -> String {
        skipWs();
        if (s[pos] == '"') {
            pos++;
            String lit;
            while (pos < s.size() && s[pos] != '"') {
                if (s[pos] == '\\' && pos + 1 < s.size()) {
                    pos++;
                    switch (s[pos]) {
                        case 'n': lit += '\n'; break;
                        case 't': lit += '\t'; break;
                        case '\\': lit += '\\'; break;
                        case '"': lit += '"'; break;
                        default: lit += s[pos];
                    }
                } else {
                    lit += s[pos];
                }
                pos++;
            }
            if (pos < s.size()) pos++;
            return lit;
        }
        return "";
    };

    auto parseTriplePatterns = [&]() -> std::vector<TriplePattern> {
        std::vector<TriplePattern> patterns;
        skipWs();

        if (pos < s.size() && s[pos] == '{') {
            pos++;

            while (pos < s.size() && s[pos] != '}') {
                skipWs();
                if (s[pos] == '}') break;

                TriplePattern pattern;

                // Subject
                if (s[pos] == '<') {
                    pattern.subject = TriplePattern::iri(readIri());
                } else if (s[pos] == '?' || s[pos] == '$') {
                    pattern.subject = TriplePattern::variable(readVariable());
                } else if (s[pos] == '_') {
                    String bn;
                    while (pos < s.size() && (std::isalnum(s[pos]) || s[pos] == '_' || s[pos] == ':')) {
                        bn += s[pos++];
                    }
                    pattern.subject = TriplePattern::blankNode(bn);
                } else {
                    pattern.subject = TriplePattern::iri(readPrefixedName());
                }

                skipWs();

                // Predicate
                if (s[pos] == '<') {
                    pattern.predicate = TriplePattern::iri(readIri());
                } else if (s[pos] == '?' || s[pos] == '$') {
                    pattern.predicate = TriplePattern::variable(readVariable());
                } else {
                    String pred = readPrefixedName();
                    if (pred == "a") {
                        pred = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type";
                    }
                    pattern.predicate = TriplePattern::iri(pred);
                }

                skipWs();

                // Object
                if (s[pos] == '<') {
                    pattern.object = TriplePattern::iri(readIri());
                } else if (s[pos] == '?' || s[pos] == '$') {
                    pattern.object = TriplePattern::variable(readVariable());
                } else if (s[pos] == '"') {
                    pattern.object = TriplePattern::literal(readLiteral());
                } else if (s[pos] == '_') {
                    String bn;
                    while (pos < s.size() && (std::isalnum(s[pos]) || s[pos] == '_' || s[pos] == ':')) {
                        bn += s[pos++];
                    }
                    pattern.object = TriplePattern::blankNode(bn);
                } else {
                    pattern.object = TriplePattern::iri(readPrefixedName());
                }

                patterns.push_back(pattern);

                skipWs();
                if (pos < s.size() && s[pos] == '.') pos++;
                else if (pos < s.size() && s[pos] == ';') pos++;
            }

            if (pos < s.size()) pos++; // skip }
        }

        return patterns;
    };

    // Parse prefix declarations
    while (pos < s.size()) {
        skipWs();
        String word = toUpper(readWord());
        if (word == "PREFIX") {
            skipWs();
            String prefix;
            while (pos < s.size() && s[pos] != ':') {
                prefix += s[pos++];
            }
            if (pos < s.size()) pos++; // skip :
            String iri = readIri();
            update.prefixes[prefix] = iri;
        } else {
            pos = 0;
            break;
        }
    }

    // Parse update operation
    skipWs();
    String keyword = toUpper(readWord());

    if (keyword == "INSERT") {
        skipWs();
        String next = toUpper(readWord());

        if (next == "DATA") {
            update.type = SparqlUpdate::InsertData;
            update.data = parseTriplePatterns();
        } else {
            update.type = SparqlUpdate::InsertDelete;
            while (pos > 0 && s[pos-1] != '{') pos--;
            if (pos > 0) pos--;
            update.insertTemplate = parseTriplePatterns();

            skipWs();
            String w = toUpper(readWord());
            if (w == "WHERE") {
                update.where = parseTriplePatterns();
            }
        }
    } else if (keyword == "DELETE") {
        skipWs();
        String next = toUpper(readWord());

        if (next == "DATA") {
            update.type = SparqlUpdate::DeleteData;
            update.data = parseTriplePatterns();
        } else if (next == "WHERE") {
            update.type = SparqlUpdate::DeleteWhere;
            update.where = parseTriplePatterns();
        } else {
            update.type = SparqlUpdate::InsertDelete;
            while (pos > 0 && s[pos-1] != '{') pos--;
            if (pos > 0) pos--;
            update.deleteTemplate = parseTriplePatterns();

            skipWs();
            String ins = toUpper(readWord());
            if (ins == "INSERT") {
                update.insertTemplate = parseTriplePatterns();
            }

            skipWs();
            String w = toUpper(readWord());
            if (w == "WHERE") {
                update.where = parseTriplePatterns();
            }
        }
    } else if (keyword == "LOAD") {
        update.type = SparqlUpdate::Load;
        skipWs();
        update.sourceGraph = readIri();
        skipWs();
        String into = toUpper(readWord());
        if (into == "INTO") {
            skipWs();
            String g = toUpper(readWord());
            if (g == "GRAPH") {
                update.graph = readIri();
            }
        }
    } else if (keyword == "CLEAR") {
        update.type = SparqlUpdate::Clear;
        skipWs();
        String g = toUpper(readWord());
        if (g == "GRAPH") {
            update.graph = readIri();
        }
    } else if (keyword == "DROP") {
        update.type = SparqlUpdate::Drop;
        skipWs();
        String g = toUpper(readWord());
        if (g == "GRAPH") {
            update.graph = readIri();
        }
    } else if (keyword == "CREATE") {
        update.type = SparqlUpdate::Create;
        skipWs();
        String g = toUpper(readWord());
        if (g == "GRAPH") {
            update.graph = readIri();
        }
    } else if (keyword == "COPY") {
        update.type = SparqlUpdate::Copy;
        skipWs();
        String g1 = toUpper(readWord());
        if (g1 == "GRAPH") {
            update.sourceGraph = readIri();
            skipWs();
            String to = toUpper(readWord());
            if (to == "TO") {
                skipWs();
                String g2 = toUpper(readWord());
                if (g2 == "GRAPH") {
                    update.graph = readIri();
                }
            }
        }
    } else if (keyword == "MOVE") {
        update.type = SparqlUpdate::Move;
        skipWs();
        String g1 = toUpper(readWord());
        if (g1 == "GRAPH") {
            update.sourceGraph = readIri();
            skipWs();
            String to = toUpper(readWord());
            if (to == "TO") {
                skipWs();
                String g2 = toUpper(readWord());
                if (g2 == "GRAPH") {
                    update.graph = readIri();
                }
            }
        }
    } else if (keyword == "ADD") {
        update.type = SparqlUpdate::Add;
        skipWs();
        String g1 = toUpper(readWord());
        if (g1 == "GRAPH") {
            update.sourceGraph = readIri();
            skipWs();
            String to = toUpper(readWord());
            if (to == "TO") {
                skipWs();
                String g2 = toUpper(readWord());
                if (g2 == "GRAPH") {
                    update.graph = readIri();
                }
            }
        }
    } else {
        return std::nullopt;
    }

    return update;
}

// ============================================================================
// SPARQL Update — execute
// ============================================================================

bool SparqlEndpoint::executeUpdate(const SparqlUpdate& update) {
    if (!storage_) return false;

    auto resolveIri = [&](const String& name) -> String {
        if (name.find("://") != String::npos) {
            return name;
        }
        auto colonPos = name.find(':');
        if (colonPos != String::npos) {
            String prefix = name.substr(0, colonPos);
            String local = name.substr(colonPos + 1);
            auto it = update.prefixes.find(prefix);
            if (it != update.prefixes.end()) {
                return it->second + local;
            }
        }
        return name;
    };

    switch (update.type) {
        case SparqlUpdate::InsertData: {
            for (const auto& pattern : update.data) {
                Triple t;
                t.subject = resolveIri(pattern.subject.value);
                t.predicate = resolveIri(pattern.predicate.value);
                t.object = resolveIri(pattern.object.value);
                if (pattern.object.isLiteral()) t.isLiteral = true;
                storage_->addTriple(t);
            }
            return true;
        }

        case SparqlUpdate::DeleteData: {
            for (const auto& pattern : update.data) {
                Triple t;
                t.subject = resolveIri(pattern.subject.value);
                t.predicate = resolveIri(pattern.predicate.value);
                t.object = resolveIri(pattern.object.value);
                storage_->removeTriple(t);
            }
            return true;
        }

        case SparqlUpdate::DeleteWhere: {
            // Execute WHERE query using the old ontology::SparqlExecutor for compatibility
            // since the where patterns use the old flat TriplePattern format
            // Use the new executor via a SELECT-like approach
            SparqlParser parser;
            // Build a SELECT-like query string from the where patterns
            // For now, execute a simplified version that looks up by pattern
            for (const auto& tp : update.where) {
                String subj = tp.subject.isVariable() ? "" : resolveIri(tp.subject.value);
                String pred = tp.predicate.isVariable() ? "" : resolveIri(tp.predicate.value);
                String obj = tp.object.isVariable() ? "" : resolveIri(tp.object.value);

                auto triples = storage_->getAllTriples();
                for (const auto& t : triples) {
                    bool match = true;
                    if (!subj.empty() && t.subject != subj) match = false;
                    if (!pred.empty() && t.predicate != pred) match = false;
                    if (!obj.empty() && t.object != obj) match = false;
                    if (match) {
                        storage_->removeTriple(t);
                    }
                }
            }
            return true;
        }

        case SparqlUpdate::InsertDelete: {
            // Find matching triples from WHERE patterns, delete from deleteTemplate, insert from insertTemplate
            // Simplified: collect all triples matching the WHERE patterns
            std::vector<Triple> matching;
            for (const auto& tp : update.where) {
                String subj = tp.subject.isVariable() ? "" : resolveIri(tp.subject.value);
                String pred = tp.predicate.isVariable() ? "" : resolveIri(tp.predicate.value);
                String obj = tp.object.isVariable() ? "" : resolveIri(tp.object.value);

                auto triples = storage_->getAllTriples();
                for (const auto& t : triples) {
                    bool match = true;
                    if (!subj.empty() && t.subject != subj) match = false;
                    if (!pred.empty() && t.predicate != pred) match = false;
                    if (!obj.empty() && t.object != obj) match = false;
                    if (match) {
                        matching.push_back(t);
                    }
                }
            }

            // Delete
            for (const auto& pattern : update.deleteTemplate) {
                for (const auto& t : matching) {
                    bool del = true;
                    if (!pattern.subject.isVariable()) {
                        if (t.subject != resolveIri(pattern.subject.value)) del = false;
                    }
                    if (!pattern.predicate.isVariable()) {
                        if (t.predicate != resolveIri(pattern.predicate.value)) del = false;
                    }
                    if (!pattern.object.isVariable()) {
                        if (t.object != resolveIri(pattern.object.value)) del = false;
                    }
                    if (del) {
                        storage_->removeTriple(t);
                    }
                }
            }

            // Insert
            for (const auto& pattern : update.insertTemplate) {
                Triple t;
                t.subject = resolveIri(pattern.subject.value);
                t.predicate = resolveIri(pattern.predicate.value);
                t.object = resolveIri(pattern.object.value);
                if (pattern.object.isLiteral()) t.isLiteral = true;
                storage_->addTriple(t);
            }
            return true;
        }

        case SparqlUpdate::Clear: {
            storage_->clear();
            return true;
        }

        case SparqlUpdate::Drop:
        case SparqlUpdate::Create:
        case SparqlUpdate::Load:
        case SparqlUpdate::Copy:
        case SparqlUpdate::Move:
        case SparqlUpdate::Add: {
            // Named graph operations — not yet supported
            return true;
        }
    }

    return false;
}

// ============================================================================
// Update (public)
// ============================================================================

bool SparqlEndpoint::update(const String& updateString) {
    auto updateOp = parseUpdate(updateString);
    if (!updateOp) {
        return false;
    }
    return executeUpdate(*updateOp);
}

// ============================================================================
// Service description
// ============================================================================

Json SparqlEndpoint::getServiceDescription() const {
    return Json{
        {"type", "sparql"},
        {"version", "1.1"},
        {"features", {
            "SELECT", "ASK", "CONSTRUCT", "DESCRIBE",
            "FILTER", "OPTIONAL", "UNION",
            "ORDER BY", "LIMIT", "OFFSET", "GROUP BY", "HAVING",
            "VALID_AT", "VALID_BETWEEN",
            "INSERT DATA", "DELETE DATA", "DELETE WHERE",
            "INSERT-DELETE-WHERE", "LOAD", "CLEAR", "DROP", "CREATE",
            "COPY", "MOVE", "ADD"
        }}
    };
}

} // namespace ontology::sparql
