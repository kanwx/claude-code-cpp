#include <ontology/ApiHandler.hpp>
#include <ontology/Storage.hpp>
#include <ontology/storage/HybridStorage.hpp>
#include <ontology/storage/GraphDatabase.hpp>
#include <ontology/storage/VectorDatabase.hpp>

namespace ontology {

class HealthHandler : public ApiHandler {
public:
    String name() const override { return "HealthHandler"; }

    void registerRoutes(httplib::Server& server) override {
        // ==================== Health check ====================
        server.Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
            Json health;
            bool readOnly = ctx_->storage && ctx_->storage->isReadOnly();
            health["status"] = readOnly ? "degraded" : "healthy";
            health["mode"] = readOnly ? "read_only" : "normal";

            Json graphDBStatus;
            if (ctx_->graphDB) {
                auto hs = ctx_->graphDB->healthCheck();
                graphDBStatus["connected"] = hs.connected;
                if (!hs.connected) graphDBStatus["lastError"] = hs.error;
                if (!hs.version.empty()) graphDBStatus["version"] = hs.version;
            } else {
                graphDBStatus["connected"] = false;
                graphDBStatus["lastError"] = "Not configured";
            }
            health["graphDB"] = graphDBStatus;

            Json vectorDBStatus;
            if (ctx_->vectorDB) {
                vectorDBStatus["connected"] = ctx_->vectorDB->isConnected();
            } else {
                vectorDBStatus["connected"] = false;
            }
            health["vectorDB"] = vectorDBStatus;

            if (ctx_->storage) {
                Json mem;
                mem["classes"] = ctx_->storage->classCount();
                mem["individuals"] = ctx_->storage->individualCount();
                mem["triples"] = ctx_->storage->tripleCount();
                health["memory"] = mem;
            }

            res.set_content(health.dump(2), "application/json");
        });

        // ==================== Stats ====================
        server.Get("/api/stats", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            Json j;
            j["classes"] = ctx_->storage->classCount();
            j["individuals"] = ctx_->storage->individualCount();
            j["triples"] = ctx_->storage->tripleCount();
            j["relations"] = ctx_->storage->relationCount();
            j["symbolicRules"] = ctx_->symbolicReasoner ? 1 : 0;
            j["neuralEnabled"] = ctx_->neuralReasoner ? 1 : 0;
            jsonResponse(res, j);
        });
    }
};

std::shared_ptr<ApiHandler> createHealthHandler() {
    return std::make_shared<HealthHandler>();
}

} // namespace ontology
