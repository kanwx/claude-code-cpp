#include <ontology/CommunityDetection.hpp>
#include <ontology/mcp/CognitiveMcpServer.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <random>
#include <unordered_set>

namespace ontology {

CommunityDetector::CommunityDetector(
    StoragePtr hybridStorage,
    std::shared_ptr<TextEmbedder> embedder,
    LlmCollaboration* llm
)
    : hybridStorage_(hybridStorage)
    , embedder_(embedder)
    , llm_(llm)
{
}

// ============================================================================
// Main Detection
// ============================================================================

std::vector<CommunityDetector::Community>
CommunityDetector::detectCommunities() {
    communities_.clear();
    entityIdToCommunity_.clear();
    graph_.clear();
    entityIndex_.clear();

    if (!hybridStorage_) return communities_;

    // Step 1: Build graph from storage
    buildGraph();

    if (graph_.empty()) return communities_;

    // Step 2: Run Louvain community detection
    std::vector<String> nodeIds;
    nodeIds.reserve(graph_.size());
    for (const auto& [id, _] : graph_) {
        nodeIds.push_back(id);
    }

    auto partition = louvain(nodeIds);

    // Step 3: Group nodes by community
    std::unordered_map<int, std::vector<String>> communityNodes;
    for (size_t i = 0; i < partition.size() && i < nodeIds.size(); i++) {
        communityNodes[partition[i]].push_back(nodeIds[i]);
    }

    // Step 4: Create Community objects
    int commIdx = 0;
    for (const auto& [commLabel, members] : communityNodes) {
        if (static_cast<int>(members.size()) < config_.minCommunitySize) continue;

        Community comm;
        comm.id = generateCommunityId(0, commIdx++);
        comm.level = 0;
        comm.entityIds = members;

        // Collect internal triples
        for (const auto& eid : members) {
            auto triples = hybridStorage_->findBySubject(eid);
            for (const auto& t : triples) {
                // Only include triples where both endpoints are in the community
                if (std::find(members.begin(), members.end(), t.object) != members.end()) {
                    comm.internalTriples.push_back(t);
                    if (std::find(comm.relationIds.begin(), comm.relationIds.end(),
                                  t.predicate) == comm.relationIds.end()) {
                        comm.relationIds.push_back(t.predicate);
                    }
                }
            }
        }

        // Limit triples
        if (comm.internalTriples.size() > 100) {
            comm.internalTriples.resize(100);
        }

        // Map entity to community
        for (const auto& eid : members) {
            entityIdToCommunity_[eid] = static_cast<int>(communities_.size());
        }

        communities_.push_back(std::move(comm));
    }

    // Step 5: Compute centrality
    computeCentrality();

    // Step 6: Generate summaries
    generateSummaries();

    // Step 7: Build hierarchy
    if (config_.maxLevels > 1) {
        buildHierarchy();
    }

    // Step 8: Build entity-centric index
    if (config_.buildEntityIndex) {
        buildEntityIndex();
    }

    return communities_;
}

// ============================================================================
// Query Relevant Communities
// ============================================================================

std::vector<CommunityDetector::Community>
CommunityDetector::getRelevantCommunities(
    const String& query,
    int topK
) {
    if (!embedder_) return {};

    auto queryEmb = embedder_->embed(query);
    return getRelevantCommunities(queryEmb, topK);
}

std::vector<CommunityDetector::Community>
CommunityDetector::getRelevantCommunities(
    const std::vector<float>& queryEmbedding,
    int topK
) {
    std::vector<std::pair<int, float>> scored;

    for (size_t i = 0; i < communities_.size(); i++) {
        float score = 0.0f;

        // Score by summary embedding similarity
        if (!communities_[i].summaryEmbedding.empty() && !queryEmbedding.empty()) {
            score = cosineSimilarity(queryEmbedding, communities_[i].summaryEmbedding);
        }

        // Score by entity name overlap with query entities
        if (hybridStorage_) {
            for (const auto& eid : communities_[i].entityIds) {
                auto ind = hybridStorage_->getIndividual(eid);
                if (ind) {
                    if (!ind->name.empty() && !communities_[i].summary.empty()) {
                        // Boost if entity name appears in summary
                        if (communities_[i].summary.find(ind->name) != String::npos) {
                            score += 0.1f;
                        }
                    }
                }
            }
        }

        communities_[i].queryRelevance = score;
        scored.push_back({static_cast<int>(i), score});
    }

    std::sort(scored.begin(), scored.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<Community> results;
    for (int i = 0; i < topK && i < static_cast<int>(scored.size()); i++) {
        if (scored[i].second > 0.0f) {
            results.push_back(communities_[scored[i].first]);
        }
    }

    return results;
}

std::vector<Triple>
CommunityDetector::findRelatedTriples(
    const String& entityId,
    int maxHops
) const {
    std::vector<Triple> results;
    std::unordered_set<String> visited;
    std::queue<std::pair<String, int>> toVisit;

    toVisit.push({entityId, 0});
    visited.insert(entityId);

    while (!toVisit.empty()) {
        auto [current, depth] = toVisit.front();
        toVisit.pop();

        if (depth > maxHops) continue;

        if (hybridStorage_) {
            auto triples = hybridStorage_->findBySubject(current);
            for (const auto& t : triples) {
                results.push_back(t);
                if (visited.find(t.object) == visited.end()) {
                    visited.insert(t.object);
                    toVisit.push({t.object, depth + 1});
                }
            }
        }
    }

    // Deduplicate
    std::sort(results.begin(), results.end(), [](const Triple& a, const Triple& b) {
        if (a.subject != b.subject) return a.subject < b.subject;
        if (a.predicate != b.predicate) return a.predicate < b.predicate;
        return a.object < b.object;
    });
    results.erase(std::unique(results.begin(), results.end(), [](const Triple& a, const Triple& b) {
        return a.subject == b.subject && a.predicate == b.predicate && a.object == b.object;
    }), results.end());

    return results;
}

std::vector<String>
CommunityDetector::getEntityCommunities(const String& entityId) const {
    std::vector<String> result;

    auto it = entityIdToCommunity_.find(entityId);
    if (it != entityIdToCommunity_.end() && it->second < static_cast<int>(communities_.size())) {
        const auto& comm = communities_[it->second];
        result.push_back(comm.id);

        // Also add parent communities
        if (!comm.parentCommunityId.empty()) {
            result.push_back(comm.parentCommunityId);
        }
    }

    return result;
}

String CommunityDetector::getCommunitySummary(const String& communityId) const {
    for (const auto& comm : communities_) {
        if (comm.id == communityId) return comm.summary;
    }
    return "";
}

Json CommunityDetector::getStats() const {
    Json j;
    j["communityCount"] = static_cast<int>(communities_.size());
    j["entityIndexSize"] = static_cast<int>(entityIndex_.size());
    j["graphNodes"] = static_cast<int>(graph_.size());

    Json commArr = Json::array();
    for (const auto& comm : communities_) {
        if (commArr.size() >= 20) break;
        commArr.push_back(comm.toJson());
    }
    j["communities"] = commArr;

    // Size distribution
    int minSize = std::numeric_limits<int>::max();
    int maxSize = 0;
    float avgSize = 0.0f;
    for (const auto& comm : communities_) {
        int sz = static_cast<int>(comm.entityIds.size());
        minSize = std::min(minSize, sz);
        maxSize = std::max(maxSize, sz);
        avgSize += sz;
    }
    if (!communities_.empty()) avgSize /= communities_.size();
    j["sizeDistribution"] = {{"min", minSize}, {"max", maxSize}, {"avg", avgSize}};

    return j;
}

// ============================================================================
// Graph Building
// ============================================================================

void CommunityDetector::buildGraph() {
    if (!hybridStorage_) return;

    auto allTriples = hybridStorage_->getAllTriples();

    for (const auto& t : allTriples) {
        // Add nodes
        if (graph_.find(t.subject) == graph_.end()) {
            graph_[t.subject] = GraphNode{t.subject, 1.0f, {}};
        }
        if (graph_.find(t.object) == graph_.end()) {
            graph_[t.object] = GraphNode{t.object, 1.0f, {}};
        }

        // Add edges (weighted by confidence)
        float weight = t.confidence;

        // Check if edge already exists
        bool found = false;
        for (auto& [neighbor, w] : graph_[t.subject].neighbors) {
            if (neighbor == t.object) {
                w += weight;
                found = true;
                break;
            }
        }
        if (!found) {
            graph_[t.subject].neighbors.push_back({t.object, weight});
        }

        // Also add reverse edge for undirected community detection
        found = false;
        for (auto& [neighbor, w] : graph_[t.object].neighbors) {
            if (neighbor == t.subject) {
                w += weight;
                found = true;
                break;
            }
        }
        if (!found) {
            graph_[t.object].neighbors.push_back({t.subject, weight});
        }

        // Update node weights
        graph_[t.subject].weight += weight;
        graph_[t.object].weight += weight;
    }
}

// ============================================================================
// Louvain Algorithm
// ============================================================================

std::vector<int>
CommunityDetector::louvain(const std::vector<String>& nodeIds) {
    int n = static_cast<int>(nodeIds.size());
    if (n == 0) return {};

    // Initialize: each node in its own community
    std::vector<int> partition(n);
    for (int i = 0; i < n; i++) {
        partition[i] = i;
    }

    // Build adjacency for index-based access
    std::unordered_map<String, int> nodeToIdx;
    for (int i = 0; i < n; i++) {
        nodeToIdx[nodeIds[i]] = i;
    }

    // Compute total edge weight (2m in modularity formula)
    float m2 = 0.0f;
    for (int i = 0; i < n; i++) {
        auto it = graph_.find(nodeIds[i]);
        if (it != graph_.end()) {
            for (const auto& [neighbor, weight] : it->second.neighbors) {
                auto nIt = nodeToIdx.find(neighbor);
                if (nIt != nodeToIdx.end()) {
                    m2 += weight;
                }
            }
        }
    }

    if (m2 < 1e-10f) return partition;

    // Phase 1: Local moving
    std::random_device rd;
    std::mt19937 gen(rd());

    for (int iter = 0; iter < config_.maxIterations; iter++) {
        bool improved = false;

        // Shuffle node order
        std::vector<int> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::shuffle(order.begin(), order.end(), gen);

        for (int idx : order) {
            const String& nodeId = nodeIds[idx];
            int currentComm = partition[idx];

            // Compute sigma_in and sigma_tot for current community
            float ki = 0.0f;  // weighted degree of node i
            std::unordered_map<int, float> commWeights;  // community -> edge weight from node i

            auto it = graph_.find(nodeId);
            if (it != graph_.end()) {
                for (const auto& [neighbor, weight] : it->second.neighbors) {
                    auto nIt = nodeToIdx.find(neighbor);
                    if (nIt != nodeToIdx.end()) {
                        ki += weight;
                        commWeights[partition[nIt->second]] += weight;
                    }
                }
            }

            // Compute community weights
            std::unordered_map<int, float> sigmaTot;  // community -> total weight
            for (int j = 0; j < n; j++) {
                auto jIt = graph_.find(nodeIds[j]);
                if (jIt != graph_.end()) {
                    for (const auto& [neighbor, weight] : jIt->second.neighbors) {
                        auto nIt = nodeToIdx.find(neighbor);
                        if (nIt != nodeToIdx.end()) {
                            sigmaTot[partition[j]] += weight;
                        }
                    }
                }
            }

            // Try moving node to each neighboring community
            float bestGain = 0.0f;
            int bestComm = currentComm;

            float ki_in_current = commWeights[currentComm];
            float sigma_tot_current = sigmaTot[currentComm];

            for (const auto& [comm, ki_in] : commWeights) {
                if (comm == currentComm) continue;

                float sigma_tot_c = sigmaTot[comm];

                // Modularity gain: ΔQ = [ki_in_c / m - sigma_tot_c * ki / (2m)] - [ki_in_current / m - sigma_tot_current * ki / (2m)]
                // Simplified version
                float deltaQ = (ki_in - ki_in_current) / m2 +
                               ki * (sigma_tot_current - sigma_tot_c) / (m2 * m2 / 2);

                // Additional penalty for oversized communities
                int commSize = 0;
                for (int j = 0; j < n; j++) {
                    if (partition[j] == comm) commSize++;
                }
                if (commSize >= config_.maxCommunitySize) continue;

                if (deltaQ > bestGain && deltaQ > config_.minModularityGain) {
                    bestGain = deltaQ;
                    bestComm = comm;
                }
            }

            if (bestComm != currentComm) {
                partition[idx] = bestComm;
                improved = true;
            }
        }

        if (!improved) break;
    }

    // Renumber communities starting from 0
    std::unordered_map<int, int> renumber;
    int nextComm = 0;
    for (int i = 0; i < n; i++) {
        if (renumber.find(partition[i]) == renumber.end()) {
            renumber[partition[i]] = nextComm++;
        }
        partition[i] = renumber[partition[i]];
    }

    return partition;
}

// ============================================================================
// Build Entity Index (LightRAG style)
// ============================================================================

void CommunityDetector::buildEntityIndex() {
    if (!hybridStorage_) return;

    auto individuals = hybridStorage_->getAllIndividuals();

    for (const auto& ind : individuals) {
        EntityIndex idx;
        idx.entityId = ind.id;
        idx.entityName = ind.name;
        idx.entityType = ind.classId;
        idx.embedding = ind.embedding;

        // Collect outgoing edges
        auto triples = hybridStorage_->findBySubject(ind.id);
        for (const auto& t : triples) {
            idx.outgoing.push_back({t.predicate, t.object});
            idx.degree += 1.0f;
        }

        // Collect incoming edges
        auto allTriples = hybridStorage_->getAllTriples();
        for (const auto& t : allTriples) {
            if (t.object == ind.id) {
                idx.incoming.push_back({t.subject, t.predicate});
                idx.degree += 1.0f;
            }
        }

        // Assign communities
        auto it = entityIdToCommunity_.find(ind.id);
        if (it != entityIdToCommunity_.end()) {
            idx.communityIds.push_back(communities_[it->second].id);
        }

        // Limit related entities
        if (idx.outgoing.size() > static_cast<size_t>(config_.maxRelatedEntities)) {
            idx.outgoing.resize(config_.maxRelatedEntities);
        }

        entityIndex_[ind.id] = std::move(idx);
    }
}

// ============================================================================
// Summaries
// ============================================================================

void CommunityDetector::generateSummaries() {
    for (auto& comm : communities_) {
        comm.summary = generateSummary(comm);

        // Embed the summary
        if (embedder_ && !comm.summary.empty()) {
            comm.summaryEmbedding = embedder_->embed(comm.summary);
        }
    }
}

String CommunityDetector::generateSummary(const Community& community) const {
    if (community.internalTriples.empty()) return "";

    // If LLM available, use it
    if (llm_ && config_.useLlmSummary) {
        try {
            std::ostringstream prompt;
            prompt << "请总结以下知识图谱片段的主要内容：\n\n";
            int count = 0;
            for (const auto& t : community.internalTriples) {
                if (count++ >= 20) break;
                prompt << "- " << t.subject << " " << t.predicate << " " << t.object << "\n";
            }

            auto response = llm_->callLlm("你是一个知识图谱摘要助手。", prompt.str());
            if (!response.empty()) return response;
        } catch (const std::exception& e) {
            spdlog::error("RAG error: {}", e.what());
        }
    }

    // Template-based summary
    std::ostringstream oss;

    // Collect unique entities and relations
    std::unordered_set<String> entitySet;
    std::unordered_set<String> relationSet;
    for (const auto& t : community.internalTriples) {
        entitySet.insert(t.subject);
        entitySet.insert(t.object);
        relationSet.insert(t.predicate);
    }

    oss << "该社区包含" << entitySet.size() << "个实体和"
        << relationSet.size() << "种关系。";

    // List top entities
    oss << "核心实体包括：";
    int entCount = 0;
    for (const auto& e : entitySet) {
        if (entCount++ >= 5) {
            oss << "等";
            break;
        }
        if (entCount > 1) oss << "、";
        oss << e;
    }

    // List relation types
    oss << "。关系类型包括：";
    int relCount = 0;
    for (const auto& r : relationSet) {
        if (relCount++ >= 5) {
            oss << "等";
            break;
        }
        if (relCount > 1) oss << "、";
        oss << r;
    }

    oss << "。";

    // Add key facts
    oss << "关键事实：";
    int factCount = 0;
    for (const auto& t : community.internalTriples) {
        if (factCount++ >= 3) break;
        oss << t.subject << t.predicate << t.object;
        if (factCount < 3 && factCount < static_cast<int>(community.internalTriples.size())) oss << "；";
    }

    return oss.str();
}

// ============================================================================
// Centrality
// ============================================================================

void CommunityDetector::computeCentrality() {
    for (auto& comm : communities_) {
        // Find entity with highest degree centrality within the community
        std::unordered_map<String, int> degrees;

        for (const auto& t : comm.internalTriples) {
            degrees[t.subject]++;
            degrees[t.object]++;
        }

        int maxDeg = 0;
        String centralEntity;
        for (const auto& [eid, deg] : degrees) {
            if (deg > maxDeg) {
                maxDeg = deg;
                centralEntity = eid;
            }
        }

        comm.centralEntity = centralEntity;
        comm.centrality = comm.internalTriples.empty() ? 0.0f :
            static_cast<float>(maxDeg) / comm.internalTriples.size();
    }
}

// ============================================================================
// Hierarchy
// ============================================================================

void CommunityDetector::buildHierarchy() {
    // Level 0 communities already exist
    // Build level 1+ by merging similar communities

    std::vector<Community> currentLevel = communities_;
    int level = 1;

    while (level < config_.maxLevels && currentLevel.size() > config_.minCommunitySize) {
        // Compute inter-community similarity
        std::vector<std::tuple<float, size_t, size_t>> similarities;

        for (size_t i = 0; i < currentLevel.size(); i++) {
            for (size_t j = i + 1; j < currentLevel.size(); j++) {
                // Similarity based on shared entities and relations
                std::unordered_set<String> entities_i(
                    currentLevel[i].entityIds.begin(), currentLevel[i].entityIds.end());
                std::unordered_set<String> entities_j(
                    currentLevel[j].entityIds.begin(), currentLevel[j].entityIds.end());

                int shared = 0;
                for (const auto& e : entities_i) {
                    if (entities_j.count(e)) shared++;
                }

                // Jaccard similarity
                float sim = static_cast<float>(shared) /
                    (entities_i.size() + entities_j.size() - shared);

                // Also consider embedding similarity
                if (!currentLevel[i].summaryEmbedding.empty() &&
                    !currentLevel[j].summaryEmbedding.empty()) {
                    float embSim = cosineSimilarity(
                        currentLevel[i].summaryEmbedding,
                        currentLevel[j].summaryEmbedding);
                    sim = sim * 0.5f + embSim * 0.5f;
                }

                if (sim > 0) {
                    similarities.push_back({sim, i, j});
                }
            }
        }

        // Sort by similarity descending
        std::sort(similarities.begin(), similarities.end(),
            [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });

        // Merge most similar communities
        std::unordered_set<int> merged;
        std::vector<Community> nextLevel;
        int nextIdx = 0;

        for (const auto& [sim, i, j] : similarities) {
            if (sim < config_.mergeThreshold) break;
            if (merged.count(static_cast<int>(i)) || merged.count(static_cast<int>(j))) continue;

            Community parent;
            parent.id = generateCommunityId(level, nextIdx++);
            parent.level = level;

            // Merge entities
            parent.entityIds = currentLevel[i].entityIds;
            parent.entityIds.insert(parent.entityIds.end(),
                currentLevel[j].entityIds.begin(), currentLevel[j].entityIds.end());

            // Merge triples
            parent.internalTriples = currentLevel[i].internalTriples;
            parent.internalTriples.insert(parent.internalTriples.end(),
                currentLevel[j].internalTriples.begin(), currentLevel[j].internalTriples.end());

            // Set sub-communities
            parent.subCommunityIds = {currentLevel[i].id, currentLevel[j].id};

            // Update parent references
            for (auto& comm : communities_) {
                if (comm.id == currentLevel[i].id || comm.id == currentLevel[j].id) {
                    comm.parentCommunityId = parent.id;
                }
            }

            // Update entity mapping
            for (const auto& eid : parent.entityIds) {
                entityIdToCommunity_[eid] = static_cast<int>(communities_.size());
            }

            nextLevel.push_back(parent);
            merged.insert(static_cast<int>(i));
            merged.insert(static_cast<int>(j));
        }

        // Add unmerged communities as-is
        for (size_t i = 0; i < currentLevel.size(); i++) {
            if (!merged.count(static_cast<int>(i))) {
                nextLevel.push_back(currentLevel[i]);
            }
        }

        // Generate summaries for new level
        for (auto& comm : nextLevel) {
            if (comm.summary.empty()) {
                comm.summary = generateSummary(comm);
                if (embedder_ && !comm.summary.empty()) {
                    comm.summaryEmbedding = embedder_->embed(comm.summary);
                }
            }
        }

        // Add to main communities list
        for (auto& comm : nextLevel) {
            if (comm.level == level) {
                communities_.push_back(std::move(comm));
            }
        }

        currentLevel = nextLevel;
        level++;

        // Safety: stop if no merging happened
        if (merged.empty()) break;
    }
}

// ============================================================================
// Helpers
// ============================================================================

String CommunityDetector::generateCommunityId(int level, int index) const {
    return "community_L" + std::to_string(level) + "_" + std::to_string(index);
}

float CommunityDetector::computeModularity(
    const std::vector<int>& partition,
    const std::vector<String>& nodeIds
) const {
    int n = static_cast<int>(nodeIds.size());
    if (n == 0) return 0.0f;

    // Build node index map
    std::unordered_map<String, int> nodeToIdx;
    for (int i = 0; i < n; i++) nodeToIdx[nodeIds[i]] = i;

    // Compute m (total edge weight)
    float m = 0.0f;
    for (int i = 0; i < n; i++) {
        auto it = graph_.find(nodeIds[i]);
        if (it != graph_.end()) {
            for (const auto& [neighbor, weight] : it->second.neighbors) {
                auto nIt = nodeToIdx.find(neighbor);
                if (nIt != nodeToIdx.end()) {
                    m += weight;
                }
            }
        }
    }
    m /= 2.0f;

    if (m < 1e-10f) return 0.0f;

    // Compute modularity Q
    float Q = 0.0f;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (partition[i] != partition[j]) continue;

            float Aij = 0.0f;
            auto it = graph_.find(nodeIds[i]);
            if (it != graph_.end()) {
                for (const auto& [neighbor, weight] : it->second.neighbors) {
                    if (neighbor == nodeIds[j]) {
                        Aij = weight;
                        break;
                    }
                }
            }

            float ki = 0.0f, kj = 0.0f;
            auto iIt = graph_.find(nodeIds[i]);
            if (iIt != graph_.end()) {
                for (const auto& [_, w] : iIt->second.neighbors) ki += w;
            }
            auto jIt = graph_.find(nodeIds[j]);
            if (jIt != graph_.end()) {
                for (const auto& [_, w] : jIt->second.neighbors) kj += w;
            }

            Q += Aij - ki * kj / (2.0f * m);
        }
    }

    return Q / (2.0f * m);
}

float CommunityDetector::cosineSimilarity(
    const std::vector<float>& a, const std::vector<float>& b
) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (size_t i = 0; i < a.size(); i++) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    float denom = std::sqrt(na) * std::sqrt(nb);
    return denom < 1e-10f ? 0.0f : dot / denom;
}

void CommunityDetector::persistCommunitiesAsNodes(StoragePtr storage) {
    if (!storage) return;

    for (const auto& community : communities_) {
        Individual ind;
        ind.id = community.id;
        ind.name = "Community_" + community.id;
        ind.classId = "Community";
        ind.importance = community.centrality;

        // Store community properties
        ind.properties["summary"] = community.summary;
        ind.properties["entityCount"] = static_cast<int>(community.entityIds.size());
        ind.properties["relationCount"] = static_cast<int>(community.relationIds.size());
        ind.properties["level"] = community.level;
        ind.properties["centralEntity"] = community.centralEntity;
        ind.properties["centrality"] = community.centrality;
        if (!community.parentCommunityId.empty()) {
            ind.properties["parentCommunityId"] = community.parentCommunityId;
        }
        if (!community.subCommunityIds.empty()) {
            ind.properties["subCommunityIds"] = community.subCommunityIds;
        }
        ind.properties["entityIds"] = community.entityIds;

        // Store embedding
        ind.embedding = community.summaryEmbedding;

        // Store temporal validity (communities are always valid by default)
        storage->addIndividual(ind);

        // Create triples linking community to its entities
        for (const auto& entityId : community.entityIds) {
            Triple t;
            t.subject = community.id;
            t.predicate = "containsEntity";
            t.object = entityId;
            t.source = "community_detection";
            t.confidence = 1.0f;
            storage->addTriple(t);
        }

        // Create triple linking to parent community
        if (!community.parentCommunityId.empty()) {
            Triple t;
            t.subject = community.id;
            t.predicate = "subCommunityOf";
            t.object = community.parentCommunityId;
            t.source = "community_detection";
            t.confidence = 1.0f;
            storage->addTriple(t);
        }
    }
}

} // namespace ontology
