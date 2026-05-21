#include <ontology/ApiHandler.hpp>
#include <ontology/Persistence.hpp>

namespace ontology {

class PersistenceHandler : public ApiHandler {
public:
    String name() const override { return "PersistenceHandler"; }

    void registerRoutes(httplib::Server& server) override {
        // ==================== Snapshot management ====================
        server.Post("/persistence/snapshot", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->snapshotManager) {
                errorResponse(res, 500, "Snapshot manager not initialized");
                return;
            }

            try {
                String snapshotId = ctx_->snapshotManager->createSnapshot(
                    ctx_->storage, ctx_->ragStorage);

                Json j;
                j["snapshotId"] = snapshotId;
                j["status"] = "created";
                jsonResponse(res, j);
            } catch (const std::exception& e) {
                errorResponse(res, 500, String("Failed to create snapshot: ") + e.what());
            }
        });

        server.Post("/persistence/restore", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->snapshotManager) {
                errorResponse(res, 500, "Snapshot manager not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                String snapshotId = body.value("snapshotId", "");

                if (snapshotId.empty()) {
                    // Restore latest snapshot
                    snapshotId = ctx_->snapshotManager->latestSnapshotId();
                    if (snapshotId.empty()) {
                        errorResponse(res, 404, "No snapshots available");
                        return;
                    }
                }

                bool success = ctx_->snapshotManager->restoreSnapshot(
                    snapshotId, ctx_->storage, ctx_->ragStorage);

                if (success) {
                    Json j;
                    j["snapshotId"] = snapshotId;
                    j["status"] = "restored";
                    jsonResponse(res, j);
                } else {
                    errorResponse(res, 404, "Snapshot not found or restore failed");
                }
            } catch (const std::exception& e) {
                errorResponse(res, 400, String("Invalid JSON: ") + e.what());
            }
        });

        server.Get("/persistence/snapshots", [this](const httplib::Request&, httplib::Response& res) {
            if (!ctx_->snapshotManager) {
                errorResponse(res, 500, "Snapshot manager not initialized");
                return;
            }

            auto snapshots = ctx_->snapshotManager->listSnapshots();
            Json j = Json::array();
            for (const auto& id : snapshots) {
                j.push_back(id);
            }
            jsonResponse(res, j);
        });

        server.Get("/persistence/stats", [this](const httplib::Request&, httplib::Response& res) {
            Json j;

            if (ctx_->walManager) {
                j["wal"] = ctx_->walManager->getStats();
            } else {
                j["wal"] = {{"available", false}};
            }

            if (ctx_->snapshotManager) {
                j["snapshot"] = ctx_->snapshotManager->getStats();
            } else {
                j["snapshot"] = {{"available", false}};
            }

            jsonResponse(res, j);
        });
    }
};

std::shared_ptr<ApiHandler> createPersistenceHandler() {
    return std::make_shared<PersistenceHandler>();
}

} // namespace ontology
