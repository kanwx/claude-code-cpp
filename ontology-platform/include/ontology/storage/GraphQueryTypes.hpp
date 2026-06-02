#pragma once
#include <ontology/Core.hpp>
#include <vector>
#include <string>

namespace ontology {

struct LoadResult {
    bool success = false;
    int count = 0;
    String error;
};

struct BatchResult {
    bool success = false;
    int created = 0;
    int failed = 0;
    String error;
};

struct PathResult {
    bool success = false;
    std::vector<String> nodes;
    std::vector<String> edges;
    String error;
};

struct GraphQueryResult {
    bool success = false;
    std::vector<Json> rows;
    String error;
};

struct CommunityResult {
    bool success = false;
    std::vector<std::vector<String>> communities;
    String error;
};

struct HealthStatus {
    bool connected = false;
    String version;
    int nodeCount = 0;
    int edgeCount = 0;
    String error;
};

} // namespace ontology
