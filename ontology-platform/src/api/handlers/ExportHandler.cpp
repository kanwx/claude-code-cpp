#include <ontology/ApiHandler.hpp>
#include <ontology/OntologyIO.hpp>
#include <spdlog/spdlog.h>

namespace ontology {

class ExportHandler : public ApiHandler {
public:
    String name() const override { return "ExportHandler"; }

    void registerRoutes(httplib::Server& server) override {
        // ==================== JSON export ====================
        server.Get("/api/export", [this](const httplib::Request&, httplib::Response& res) {
            Json j;
            j["classes"] = Json::array();
            j["relations"] = Json::array();
            j["individuals"] = Json::array();

            if (ctx_->storage) {
                auto classes = ctx_->storage->getAllClasses();
                for (const auto& cls : classes) {
                    j["classes"].push_back(classToJson(cls));
                }

                auto individuals = ctx_->storage->getAllIndividuals();
                for (const auto& ind : individuals) {
                    j["individuals"].push_back(individualToJson(ind));
                }
            }

            jsonResponse(res, j);
        });

        // ==================== JSON import ====================
        server.Post("/api/import", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ctx_->storage) {
                errorResponse(res, 500, "Storage not initialized");
                return;
            }

            try {
                Json body = Json::parse(req.body);
                int imported = 0;

                if (body.contains("classes")) {
                    for (const auto& c : body["classes"]) {
                        Class cls;
                        cls.id = c.value("id", "");
                        cls.name = c.value("name", "");
                        cls.description = c.value("description", "");
                        cls.superClasses = c.value("superClasses", std::vector<String>{});
                        if (ctx_->storage->addClass(cls)) imported++;
                    }
                }

                if (body.contains("individuals")) {
                    for (const auto& i : body["individuals"]) {
                        Individual ind;
                        ind.id = i.value("id", "");
                        ind.name = i.value("name", "");
                        ind.classId = i.value("classId", "");
                        ind.properties = i.value("properties", Json{});
                        if (ctx_->storage->addIndividual(ind)) imported++;
                    }
                }

                Json j;
                j["imported"] = imported;
                j["status"] = "success";
                jsonResponse(res, j);
            } catch (const nlohmann::json::parse_error& e) {
                errorResponse(res, 400, std::string("Invalid JSON: ") + e.what());
            } catch (const nlohmann::json::type_error& e) {
                errorResponse(res, 400, std::string("Type mismatch: ") + e.what());
            } catch (const std::exception& e) {
                spdlog::error("Handler error: {}", e.what());
                errorResponse(res, 500, "Internal error");
            }
        });

        // ==================== Turtle format ====================
        server.Post("/api/import/turtle", [this](const httplib::Request& req, httplib::Response& res) {
            TurtleParser parser;
            auto onto = parser.parse(req.body);
            if (!onto) {
                errorResponse(res, 400, "Failed to parse Turtle");
                return;
            }

            int imported = 0;
            for (const auto& [id, cls] : onto->classes) {
                if (ctx_->storage->addClass(cls)) imported++;
            }
            for (const auto& [id, ind] : onto->individuals) {
                if (ctx_->storage->addIndividual(ind)) imported++;
            }

            Json j;
            j["imported"] = imported;
            j["format"] = "turtle";
            jsonResponse(res, j);
        });

        server.Get("/api/export/turtle", [this](const httplib::Request&, httplib::Response& res) {
            Ontology onto;
            onto.classes.clear();
            onto.individuals.clear();

            auto classes = ctx_->storage->getAllClasses();
            for (const auto& cls : classes) {
                onto.classes[cls.id] = cls;
            }

            auto individuals = ctx_->storage->getAllIndividuals();
            for (const auto& ind : individuals) {
                onto.individuals[ind.id] = ind;
            }

            TurtleWriter writer;
            res.set_content(writer.write(onto), "text/turtle");
        });

        // ==================== RDF/XML format ====================
        server.Post("/api/import/rdfxml", [this](const httplib::Request& req, httplib::Response& res) {
            RdfXmlParser parser;
            auto onto = parser.parse(req.body);
            if (!onto) {
                errorResponse(res, 400, "Failed to parse RDF/XML");
                return;
            }

            int imported = 0;
            for (const auto& [id, cls] : onto->classes) {
                if (ctx_->storage->addClass(cls)) imported++;
            }
            for (const auto& [id, ind] : onto->individuals) {
                if (ctx_->storage->addIndividual(ind)) imported++;
            }

            Json j;
            j["imported"] = imported;
            j["format"] = "rdfxml";
            jsonResponse(res, j);
        });

        server.Get("/api/export/rdfxml", [this](const httplib::Request&, httplib::Response& res) {
            Ontology onto;
            auto classes = ctx_->storage->getAllClasses();
            for (const auto& cls : classes) {
                onto.classes[cls.id] = cls;
            }
            auto individuals = ctx_->storage->getAllIndividuals();
            for (const auto& ind : individuals) {
                onto.individuals[ind.id] = ind;
            }

            RdfXmlWriter writer;
            res.set_content(writer.write(onto), "application/rdf+xml");
        });

        // ==================== N-Triples format ====================
        server.Get("/api/export/ntriples", [this](const httplib::Request&, httplib::Response& res) {
            Ontology onto;
            auto classes = ctx_->storage->getAllClasses();
            for (const auto& cls : classes) {
                onto.classes[cls.id] = cls;
            }
            auto individuals = ctx_->storage->getAllIndividuals();
            for (const auto& ind : individuals) {
                onto.individuals[ind.id] = ind;
            }

            NTriplesWriter writer;
            res.set_content(writer.write(onto), "application/n-triples");
        });

        // ==================== JSON-LD format ====================
        server.Get("/api/export/jsonld", [this](const httplib::Request&, httplib::Response& res) {
            Ontology onto;
            auto classes = ctx_->storage->getAllClasses();
            for (const auto& cls : classes) {
                onto.classes[cls.id] = cls;
            }
            auto individuals = ctx_->storage->getAllIndividuals();
            for (const auto& ind : individuals) {
                onto.individuals[ind.id] = ind;
            }

            JsonLdWriter writer;
            res.set_content(writer.write(onto), "application/ld+json");
        });
    }
};

std::shared_ptr<ApiHandler> createExportHandler() {
    return std::make_shared<ExportHandler>();
}

} // namespace ontology
