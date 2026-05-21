#pragma once

#include "ServiceContext.hpp"
#include "Core.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace ontology {

/// Base class for all route handler modules.
/// Each handler extracts a subset of routes from HttpServer and owns
/// its dependency references via the shared ServiceContext.
class ApiHandler {
public:
    virtual ~ApiHandler() = default;

    /// Register all routes handled by this module onto the given server.
    virtual void registerRoutes(httplib::Server& server) = 0;

    /// Human-readable name for logging and diagnostics.
    virtual String name() const = 0;

    /// Inject the shared service context.
    void setContext(ServiceContextPtr ctx) { ctx_ = std::move(ctx); }

protected:
    // --- HTTP response helpers ---

    void jsonResponse(httplib::Response& res, const Json& j) {
        res.set_content(j.dump(), "application/json");
        res.status = 200;
    }

    void errorResponse(httplib::Response& res, int code, const String& message) {
        Json j;
        j["error"] = message;
        j["code"] = code;
        res.set_content(j.dump(), "application/json");
        res.status = code;
    }

    Json parseBody(const httplib::Request& req) {
        try {
            return Json::parse(req.body);
        } catch (const Json::parse_error&) {
            return Json();
        }
    }

    // --- JSON serialization helpers (virtual so handlers can override) ---

    virtual Json classToJson(const Class& cls);
    virtual Json relationToJson(const Relation& rel);
    virtual Json individualToJson(const Individual& ind);

protected:
    ServiceContextPtr ctx_;
};

} // namespace ontology
