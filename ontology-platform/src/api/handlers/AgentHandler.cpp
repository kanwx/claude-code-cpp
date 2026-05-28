#include <ontology/ApiHandler.hpp>
#include <ontology/AgentCollaboration.hpp>
#include <spdlog/spdlog.h>

namespace ontology {

class AgentHandler : public ApiHandler {
public:
    String name() const override { return "AgentHandler"; }

    void registerRoutes(httplib::Server& server) override {
        auto& mgr = AgentCollaborationManager::instance();

        // ==================== Agent list ====================
        server.Get("/api/agents", [this](const httplib::Request&, httplib::Response& res) {
            auto& m = AgentCollaborationManager::instance();
            auto agents = m.getAgentStats();
            Json arr = Json::array();
            for (const auto& a : agents) {
                arr.push_back(a.toJson());
            }
            jsonResponse(res, arr);
        });

        // ==================== Agent summary ====================
        server.Get("/api/agents/summary", [this](const httplib::Request&, httplib::Response& res) {
            auto& m = AgentCollaborationManager::instance();
            jsonResponse(res, m.getSummary());
        });

        // ==================== Activity log ====================
        server.Get("/api/agents/activities", [this](const httplib::Request& req, httplib::Response& res) {
            auto& m = AgentCollaborationManager::instance();
            int limit = 100;
            String filter;
            if (req.has_param("limit")) {
                try { limit = std::stoi(req.get_param_value("limit")); } catch (const std::exception&) {}
            }
            if (req.has_param("agentId")) {
                filter = req.get_param_value("agentId");
            }
            auto activities = m.getActivityLog(limit, filter);
            Json arr = Json::array();
            for (const auto& a : activities) {
                arr.push_back(a.toJson());
            }
            jsonResponse(res, arr);
        });

        // ==================== Tool usage stats ====================
        server.Get("/api/agents/tools/stats", [this](const httplib::Request&, httplib::Response& res) {
            auto& m = AgentCollaborationManager::instance();
            auto tools = m.getToolUsageStats();
            Json arr = Json::array();
            for (const auto& t : tools) {
                arr.push_back(t.toJson());
            }
            jsonResponse(res, arr);
        });

        // ==================== Register agent ====================
        server.Post("/api/agents/register", [this](const httplib::Request& req, httplib::Response& res) {
            Json body = parseBody(req);
            if (body.is_null() || !body.contains("agentId")) {
                errorResponse(res, 400, "Missing agentId");
                return;
            }

            String agentId = body["agentId"];
            String name = body.value("name", agentId);
            String type = body.value("type", "custom");
            String version = body.value("version", "");
            String endpoint = body.value("endpoint", "");

            std::vector<String> tools;
            if (body.contains("tools") && body["tools"].is_array()) {
                for (const auto& t : body["tools"]) {
                    tools.push_back(t.get<String>());
                }
            }

            AgentCollaborationManager::instance().registerAgent(
                agentId, name, type, version, endpoint, tools);

            Json j;
            j["status"] = "ok";
            j["agentId"] = agentId;
            jsonResponse(res, j);
        });

        // ==================== Unregister agent ====================
        server.Post("/api/agents/unregister", [this](const httplib::Request& req, httplib::Response& res) {
            Json body = parseBody(req);
            if (body.is_null() || !body.contains("agentId")) {
                errorResponse(res, 400, "Missing agentId");
                return;
            }

            AgentCollaborationManager::instance().unregisterAgent(body["agentId"]);

            Json j;
            j["status"] = "ok";
            jsonResponse(res, j);
        });

        // ==================== Clear history ====================
        server.Post("/api/agents/clear", [this](const httplib::Request&, httplib::Response& res) {
            AgentCollaborationManager::instance().clearHistory();
            Json j;
            j["status"] = "ok";
            jsonResponse(res, j);
        });
    }
};

std::shared_ptr<ApiHandler> createAgentHandler() {
    return std::make_shared<AgentHandler>();
}

} // namespace ontology
