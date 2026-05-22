#pragma once

#include <ontology/ServiceContext.hpp>
#include <ontology/Config.hpp>
#include <memory>

namespace ontology {

/// Initializes all platform services from a typed OntologyConfig.
/// Returns a fully-populated ServiceContext ready for injection into HttpServer.
class Bootstrap {
public:
    static ServiceContextPtr initialize(const OntologyConfig& config);

private:
    static void initStorage(ServiceContext& ctx, const OntologyConfig& config);
    static void initEmbedding(ServiceContext& ctx, const OntologyConfig& config);
    static void initInference(ServiceContext& ctx, const OntologyConfig& config);
    static void initRag(ServiceContext& ctx, const OntologyConfig& config);
    static void initPersistence(ServiceContext& ctx, const OntologyConfig& config);
};

} // namespace ontology
