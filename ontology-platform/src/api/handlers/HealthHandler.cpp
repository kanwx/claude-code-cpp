#include <ontology/ApiHandler.hpp>
#include <ontology/Storage.hpp>

namespace ontology {

class HealthHandler : public ApiHandler {
public:
    String name() const override { return "HealthHandler"; }

    void registerRoutes(httplib::Server& server) override {
        // ==================== Health check ====================
        server.Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
            Json j;
            j["status"] = "healthy";
            if (ctx_->storage) {
                j["stats"]["classes"] = ctx_->storage->classCount();
                j["stats"]["individuals"] = ctx_->storage->individualCount();
                j["stats"]["triples"] = ctx_->storage->tripleCount();
            }
            jsonResponse(res, j);
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
