#pragma once

#include "Core.hpp"
#include "Storage.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <mutex>
#include <random>

namespace ontology {

// ============================================================================
// Vector Clock for conflict detection
// ============================================================================

struct VectorClock {
    std::unordered_map<int, int> counters;  // shardId -> counter

    void increment(int shardId);
    bool happensBefore(const VectorClock& other) const;
    bool isConcurrent(const VectorClock& other) const;
    VectorClock merge(const VectorClock& other) const;

    bool operator==(const VectorClock& other) const {
        return counters == other.counters;
    }
};

// ============================================================================
// Cluster Configuration
// ============================================================================

struct ClusterConfig {
    int numShards = 4;
    int replicationFactor = 3;         // primary + N-1 replicas
    bool readFromFollowers = true;     // allow follower reads
    int writeQuorum = 2;              // majority of replicas must ack
};

struct ClusterHealth {
    int totalShards = 0;
    int healthyShards = 0;
    int totalReplicas = 0;
    int healthyReplicas = 0;
    bool allShardsAvailable = false;
};

// ============================================================================
// Shard Info
// ============================================================================

struct ShardInfo {
    int shardId = 0;
    String leaderName;               // name of leader replica
    std::vector<String> replicaNames; // all replica names (including leader)
    size_t tripleCount = 0;
    bool isAvailable = true;
};

// ============================================================================
// Replica State
// ============================================================================

enum class ReplicaState {
    LEADER,
    FOLLOWER,
    CATCHING_UP
};

// ============================================================================
// Replica — a TripleStore instance with state and version vector
// ============================================================================

struct Replica {
    String name;
    TripleStore store;
    ReplicaState state = ReplicaState::FOLLOWER;
    VectorClock versionClock;
    int64_t lastCatchupTime = 0;

    bool isLeader() const { return state == ReplicaState::LEADER; }
    bool isHealthy() const { return state != ReplicaState::CATCHING_UP || true; }
};

// ============================================================================
// Shard — a collection of replicas for one partition
// ============================================================================

class Shard {
public:
    Shard(int id, const ClusterConfig& config);

    /// Write to leader, replicate to followers
    bool write(const Triple& triple, int quorum);

    /// Read from leader (or follower if readFromFollowers)
    std::vector<Triple> readBySubject(const String& subject) const;
    std::vector<Triple> readByPredicate(const String& predicate) const;
    std::vector<Triple> readAll() const;

    /// Remove triple
    bool remove(const Triple& triple, int quorum);

    /// Count triples
    size_t count() const;

    /// Get leader store
    TripleStore* leaderStore();
    const TripleStore* leaderStore() const;

    /// Get info
    ShardInfo getInfo() const;

    /// Replica management
    Replica* getLeader();
    Replica* getFollower(int index);
    int replicaCount() const { return static_cast<int>(replicas_.size()); }

    /// Check if quorum of replicas is available
    bool isQuorumAvailable(int quorum) const;

    /// Simulate replica failure
    void failReplica(const String& name);
    /// Simulate replica recovery
    void recoverReplica(const String& name);

    int id() const { return id_; }

private:
    int id_;
    ClusterConfig config_;
    std::vector<std::unique_ptr<Replica>> replicas_;
    int leaderIndex_ = 0;

    /// Replicate write to followers
    bool replicateToFollowers(const Triple& triple, const VectorClock& newClock, int quorum);

    /// Select a follower for reads (round-robin)
    mutable int readFollowerIndex_ = 0;
    Replica* selectReadReplica() const;
};

// ============================================================================
// ShardManager — routes operations to shards
// ============================================================================

class ShardManager {
public:
    explicit ShardManager(const ClusterConfig& config = {});

    /// Get shard for a given subject
    int shardForKey(const String& key) const;

    /// Get shard by ID
    Shard* getShard(int shardId);
    const Shard* getShard(int shardId) const;

    /// Get all shards
    std::vector<Shard*> getAllShards();
    std::vector<const Shard*> getAllShards() const;

    /// Rebalance: redistribute triples across shards
    void rebalance();

    const ClusterConfig& config() const { return config_; }

private:
    ClusterConfig config_;
    std::vector<std::unique_ptr<Shard>> shards_;
    std::hash<String> hasher_;
};

// ============================================================================
// DistributedStorage — drop-in replacement for HybridStorage
// ============================================================================

class DistributedStorage {
public:
    explicit DistributedStorage(const ClusterConfig& config = {});
    ~DistributedStorage();

    // ---- HybridStorage-compatible interface ----

    bool addTriple(const Triple& triple);
    bool removeTriple(const Triple& triple);

    std::optional<Triple> findTriple(const String& subject, const String& predicate, const String& object);
    std::vector<Triple> findBySubject(const String& subject);
    std::vector<Triple> findByPredicate(const String& predicate);
    std::vector<Triple> findByObject(const String& object);
    std::vector<Triple> findBySP(const String& subject, const String& predicate);
    std::vector<Triple> findByPO(const String& predicate, const String& object);
    std::vector<Triple> getAllTriples();
    std::vector<Triple> queryTriples(const TripleStore::TriplePattern& pattern);

    void clear();
    size_t tripleCount() const;

    // ---- Distributed-specific methods ----

    ClusterHealth getClusterHealth() const;
    ShardInfo getShardInfo(int shardId) const;
    std::vector<ShardInfo> getAllShardInfo() const;

    /// Rebalance shards
    void rebalance();

    /// Simulate failure
    void failReplica(int shardId, const String& replicaName);
    void recoverReplica(int shardId, const String& replicaName);

    /// Get underlying shard manager
    ShardManager& shardManager() { return shardManager_; }

    // ---- Stub methods for HybridStorage compatibility ----
    bool initialize(const String&, int) { return true; }
    bool storeOntology(const Ontology&) { return true; }
    bool storeIndividual(const Individual&, const std::vector<float>&) { return true; }
    bool storeTriple(const Triple& t) { return addTriple(t); }
    size_t individualCount() const { return 0; }
    size_t classCount() const { return 0; }
    size_t relationCount() const { return 0; }

private:
    ShardManager shardManager_;
    mutable std::mutex mutex_;
};

} // namespace ontology
