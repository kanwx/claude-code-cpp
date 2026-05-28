#include <ontology/ApiHandler.hpp>
#include <ontology/Sparql.hpp>
#include <spdlog/spdlog.h>

namespace ontology {

class SparqlHandler : public ApiHandler {
public:
    String name() const override { return "SparqlHandler"; }

    void registerRoutes(httplib::Server& server) override {
        // ==================== SPARQL query (POST) ====================
        server.Post("/api/sparql", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->sparqlEndpoint) {
                errorResponse(res, 500, "SPARQL endpoint not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String query = body.value("query", "");
                if (query.empty()) {
                    errorResponse(res, 400, "Query is required");
                    return;
                }

                Json options = body.value("options", Json::object());
                SparqlResult result = ctx_->sparqlEndpoint->query(query, options);

                // Content negotiation based on Accept header
                String accept = req.get_header_value("Accept");
                if (accept.find("application/sparql-results+xml") != String::npos) {
                    res.set_content(result.toSparqlResultsXml(), "application/sparql-results+xml");
                } else if (accept.find("text/csv") != String::npos) {
                    res.set_content(result.toCsv(), "text/csv");
                } else if (accept.find("text/tab-separated-values") != String::npos) {
                    res.set_content(result.toTsv(), "text/tab-separated-values");
                } else if (accept.find("application/sparql-results+json") != String::npos) {
                    jsonResponse(res, result.toSparqlResultsJson());
                } else {
                    jsonResponse(res, result.toJson());
                }
            } catch (const nlohmann::json::parse_error& e) {
                errorResponse(res, 400, std::string("Invalid JSON: ") + e.what());
            } catch (const nlohmann::json::type_error& e) {
                errorResponse(res, 400, std::string("Type mismatch: ") + e.what());
            } catch (const std::exception& e) {
                spdlog::error("Handler error: {}", e.what());
                errorResponse(res, 500, "Internal error");
            }
        });

        // ==================== SPARQL query (GET) ====================
        server.Get("/api/sparql", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->sparqlEndpoint) {
                errorResponse(res, 500, "SPARQL endpoint not initialized");
                return;
            }

            // Query via URL parameter
            String query = req.get_param_value("query");
            if (query.empty()) {
                // Return service description
                jsonResponse(res, ctx_->sparqlEndpoint->getServiceDescription());
                return;
            }

            SparqlResult result = ctx_->sparqlEndpoint->query(query);

            // Content negotiation
            String accept = req.get_header_value("Accept");
            if (accept.find("application/sparql-results+xml") != String::npos) {
                res.set_content(result.toSparqlResultsXml(), "application/sparql-results+xml");
            } else if (accept.find("text/csv") != String::npos) {
                res.set_content(result.toCsv(), "text/csv");
            } else if (accept.find("text/tab-separated-values") != String::npos) {
                res.set_content(result.toTsv(), "text/tab-separated-values");
            } else if (accept.find("application/sparql-results+json") != String::npos) {
                jsonResponse(res, result.toSparqlResultsJson());
            } else {
                jsonResponse(res, result.toJson());
            }
        });
    }
};

std::shared_ptr<ApiHandler> createSparqlHandler() {
    return std::make_shared<SparqlHandler>();
}

} // namespace ontology
