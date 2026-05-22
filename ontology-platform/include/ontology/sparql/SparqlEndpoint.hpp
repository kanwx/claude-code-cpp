#pragma once

#include "SparqlExecutor.hpp"
#include <ontology/Storage.hpp>

namespace ontology::sparql {

// ============================================================================
// SparqlEndpoint — HTTP-facing SPARQL endpoint wiring executor to storage
// ============================================================================

class SparqlEndpoint {
public:
    SparqlEndpoint() = default;
    explicit SparqlEndpoint(StoragePtr storage);

    /// Execute a SPARQL query string, returning JSON result
    Json query(const String& sparqlString);

    /// Execute a SPARQL Update string
    bool update(const String& sparqlString);

    /// Set or replace the backing storage
    void setStorage(StoragePtr storage);

    /// Get SPARQL service description
    Json getServiceDescription() const;

private:
    StoragePtr storage_;
    std::unique_ptr<SparqlExecutor> executor_;

    // SPARQL Update parsing and execution
    struct SparqlUpdate {
        enum Type {
            InsertData, DeleteData, DeleteWhere, InsertDelete,
            Load, Clear, Create, Drop, Copy, Move, Add
        };
        Type type;
        std::vector<TriplePattern> data;
        std::vector<TriplePattern> deleteTemplate;
        std::vector<TriplePattern> insertTemplate;
        std::vector<TriplePattern> where;
        String graph;
        String sourceGraph;
        bool silent = false;
        std::unordered_map<String, String> prefixes;
    };

    std::optional<SparqlUpdate> parseUpdate(const String& updateString);
    bool executeUpdate(const SparqlUpdate& update);
};

} // namespace ontology::sparql
