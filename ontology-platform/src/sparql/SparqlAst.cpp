#include <ontology/sparql/SparqlAst.hpp>
#include <unordered_set>
#include <algorithm>

// Ensure Json and String are available in ontology::sparql scope
using ontology::Json;
using ontology::String;

namespace ontology::sparql {

// ============================================================================
// GroupPattern
// ============================================================================

std::vector<String> GroupPattern::getVariables() const {
    std::unordered_set<String> seen;
    std::vector<String> result;

    auto addUnique = [&](const String& var) {
        if (seen.insert(var).second) {
            result.push_back(var);
        }
    };

    // Collect variables from triples
    for (const auto& triple : triples) {
        auto vars = triple.getVariables();
        for (const auto& v : vars) {
            addUnique(v);
        }
    }

    // Collect variables from optional groups
    for (const auto& opt : optionalGroups) {
        auto vars = opt.getVariables();
        for (const auto& v : vars) {
            addUnique(v);
        }
    }

    // Collect variables from union groups
    for (const auto& uni : unionGroups) {
        auto vars = uni.getVariables();
        for (const auto& v : vars) {
            addUnique(v);
        }
    }

    return result;
}

// ============================================================================
// QueryResult
// ============================================================================

Json QueryResult::toJson() const {
    Json j;

    if (!errorMessage.empty()) {
        j["error"] = errorMessage;
        return j;
    }

    switch (queryType) {
        case SparqlQueryType::SELECT:
            j["type"] = "select";
            j["variables"] = variables;
            j["results"] = Json::array();
            for (const auto& row : rows) {
                Json r;
                for (const auto& [var, val] : row) {
                    r[var] = val;
                }
                j["results"].push_back(r);
            }
            break;

        case SparqlQueryType::ASK:
            j["type"] = "ask";
            j["result"] = askResult;
            break;

        case SparqlQueryType::CONSTRUCT:
            j["type"] = "construct";
            j["triples"] = Json::array();
            for (const auto& tp : constructTriples) {
                j["triples"].push_back({
                    {"subject", tp.subject.toString()},
                    {"predicate", tp.predicate.toString()},
                    {"object", tp.object.toString()}
                });
            }
            break;

        case SparqlQueryType::DESCRIBE:
            j["type"] = "describe";
            j["triples"] = Json::array();
            for (const auto& tp : constructTriples) {
                j["triples"].push_back({
                    {"subject", tp.subject.toString()},
                    {"predicate", tp.predicate.toString()},
                    {"object", tp.object.toString()}
                });
            }
            break;
    }

    j["count"] = resultCount;
    j["executionTime"] = executionTime;

    return j;
}

// ============================================================================
// Variant helpers
// ============================================================================

SparqlQueryType queryType(const Query& q) {
    return std::visit([](const auto& query) -> SparqlQueryType {
        using T = std::decay_t<decltype(query)>;
        if constexpr (std::is_same_v<T, SelectQuery>) {
            return SparqlQueryType::SELECT;
        } else if constexpr (std::is_same_v<T, AskQuery>) {
            return SparqlQueryType::ASK;
        } else if constexpr (std::is_same_v<T, ConstructQuery>) {
            return SparqlQueryType::CONSTRUCT;
        } else if constexpr (std::is_same_v<T, DescribeQuery>) {
            return SparqlQueryType::DESCRIBE;
        }
    }, q);
}

std::vector<String> allVariables(const Query& q) {
    std::unordered_set<String> seen;
    std::vector<String> result;

    auto addUnique = [&](const String& var) {
        if (seen.insert(var).second) {
            result.push_back(var);
        }
    };

    std::visit([&](const auto& query) {
        using T = std::decay_t<decltype(query)>;

        // Collect from the WHERE group pattern
        auto whereVars = query.where.getVariables();
        for (const auto& v : whereVars) {
            addUnique(v);
        }

        // Collect from explicitly listed variables
        if constexpr (std::is_same_v<T, SelectQuery>) {
            for (const auto& var : query.variables) {
                addUnique(var.name);
            }
        } else if constexpr (std::is_same_v<T, DescribeQuery>) {
            for (const auto& var : query.variables) {
                addUnique(var.name);
            }
        }
    }, q);

    return result;
}

} // namespace ontology::sparql
