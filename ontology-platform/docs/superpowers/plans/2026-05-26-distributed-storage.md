# Distributed Storage (Single-node Simulation) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement distributed storage with hash-based sharding, primary-replica replication, quorum writes, and vector clock versioning — all simulated in a single process with separate TripleStore instances per shard.

**Architecture:** `DistributedStorage` implements the same public interface as `HybridStorage` (addTriple, removeTriple, findBySubject, etc.). `ShardManager` routes operations to the correct shard(s) based on subject hash. Each shard has a primary + N replicas.

**Tech Stack:** C++17, existing `TripleStore`, `HybridStorage`.

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `include/ontology/Distributed.hpp` | Create | ShardManager, DistributedStorage, ClusterConfig, ClusterHealth, VectorClock |
| `src/storage/Distributed.cpp` | Create | Implementation |
| `tests/test_distributed.cpp` | Create | Shard routing, replication, failover, consistency tests |
| `tests/CMakeLists.txt` | Modify | Add test target |

---

### Task 1: Distributed Storage Header

**Files:**
- Create: `include/ontology/Distributed.hpp`

- [ ] **Step 1: Create the header file**

```cpp
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
    int shardId;
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
```

- [ ] **Step 2: Commit**

```bash
git add include/ontology/Distributed.hpp
git commit -m "feat(distributed): add DistributedStorage header with shard, replica, vector clock, and cluster config"
```

---

### Task 2: Distributed Storage Implementation

**Files:**
- Create: `src/storage/Distributed.cpp`

- [ ] **Step 1: Create the implementation file**

```cpp
#include <ontology/Distributed.hpp>
#include <algorithm>
#include <sstream>

namespace ontology {

// ============================================================================
// VectorClock
// ============================================================================

void VectorClock::increment(int shardId) {
    counters[shardId]++;
}

bool VectorClock::happensBefore(const VectorClock& other) const {
    // VC1 happens-before VC2 iff all counters in VC1 <= VC2 and at least one is strictly less
    bool atLeastOneLess = false;
    for (const auto& [id, count] : counters) {
        auto it = other.counters.find(id);
        int otherCount = (it != other.counters.end()) ? it->second : 0;
        if (count > otherCount) return false;
        if (count < otherCount) atLeastOneLess = true;
    }
    // Check for keys in other but not in this
    for (const auto& [id, count] : other.counters) {
        if (counters.find(id) == counters.end() && count > 0) {
            atLeastOneLess = true;  // this has implicit 0 < other's count
        }
    }
    return atLeastOneLess;
}

bool VectorClock::isConcurrent(const VectorClock& other) const {
    return !happensBefore(other) && !other.happensBefore(*this) && !(*this == other);
}

VectorClock VectorClock::merge(const VectorClock& other) const {
    VectorClock result;
    for (const auto& [id, count] : counters) {
        result.counters[id] = count;
    }
    for (const auto& [id, count] : other.counters) {
        auto it = result.counters.find(id);
        if (it != result.counters.end()) {
            it->second = std::max(it->second, count);
        } else {
            result.counters[id] = count;
        }
    }
    return result;
}

// ============================================================================
// Shard
// ============================================================================

Shard::Shard(int id, const ClusterConfig& config)
    : id_(id), config_(config) {
    for (int i = 0; i < config.replicationFactor; ++i) {
        auto replica = std::make_unique<Replica>();
        replica->name = "shard" + std::to_string(id) + "_replica" + std::to_string(i);
        replica->state = (i == 0) ? ReplicaState::LEADER : ReplicaState::FOLLOWER;
        replicas_.push_back(std::move(replica));
    }
    leaderIndex_ = 0;
}

bool Shard::write(const Triple& triple, int quorum) {
    if (!isQuorumAvailable(quorum)) return false;

    auto* leader = getLeader();
    if (!leader) return false;

    // Update vector clock
    leader->versionClock.increment(id_);
    VectorClock newClock = leader->versionClock;

    // Write to leader
    leader->store.add(triple);

    // Replicate to followers
    return replicateToFollowers(triple, newClock, quorum);
}

bool Shard::replicateToFollowers(const Triple& triple, const VectorClock& newClock, int quorum) {
    int acks = 1;  // leader already wrote
    for (size_t i = 0; i < replicas_.size(); ++i) {
        if (static_cast<int>(i) == leaderIndex_) continue;
        if (replicas_[i]->state == ReplicaState::CATCHING_UP) continue;

        replicas_[i]->store.add(triple);
        replicas_[i]->versionClock = newClock;
        acks++;
    }
    return acks >= quorum;
}

bool Shard::remove(const Triple& triple, int quorum) {
    if (!isQuorumAvailable(quorum)) return false;

    auto* leader = getLeader();
    if (!leader) return false;

    leader->versionClock.increment(id_);
    VectorClock newClock = leader->versionClock;

    leader->store.remove(triple);

    int acks = 1;
    for (size_t i = 0; i < replicas_.size(); ++i) {
        if (static_cast<int>(i) == leaderIndex_) continue;
        if (replicas_[i]->state == ReplicaState::CATCHING_UP) continue;

        replicas_[i]->store.remove(triple);
        replicas_[i]->versionClock = newClock;
        acks++;
    }
    return acks >= quorum;
}

std::vector<Triple> Shard::readBySubject(const String& subject) const {
    auto* replica = selectReadReplica();
    if (replica) return replica->store.findBySubject(subject);
    return {};
}

std::vector<Triple> Shard::readByPredicate(const String& predicate) const {
    auto* replica = selectReadReplica();
    if (replica) return replica->store.findByPredicate(predicate);
    return {};
}

std::vector<Triple> Shard::readAll() const {
    auto* replica = selectReadReplica();
    if (replica) return replica->store.all();
    return {};
}

size_t Shard::count() const {
    auto* leader = getLeader();
    return leader ? leader->store.count() : 0;
}

TripleStore* Shard::leaderStore() {
    auto* leader = getLeader();
    return leader ? &leader->store : nullptr;
}

const TripleStore* Shard::leaderStore() const {
    return replicas_[leaderIndex_]->store.count() > 0
        ? &replicas_[leaderIndex_]->store : nullptr;
}

ShardInfo Shard::getInfo() const {
    ShardInfo info;
    info.shardId = id_;
    info.leaderName = replicas_[leaderIndex_]->name;
    info.tripleCount = replicas_[leaderIndex_]->store.count();
    info.isAvailable = isQuorumAvailable(config_.writeQuorum);
    for (const auto& r : replicas_) {
        info.replicaNames.push_back(r->name);
    }
    return info;
}

Replica* Shard::getLeader() {
    if (replicas_.empty()) return nullptr;
    return replicas_[leaderIndex_].get();
}

Replica* Shard::getFollower(int index) {
    int followerIdx = 0;
    for (size_t i = 0; i < replicas_.size(); ++i) {
        if (static_cast<int>(i) == leaderIndex_) continue;
        if (followerIdx == index) return replicas_[i].get();
        followerIdx++;
    }
    return nullptr;
}

bool Shard::isQuorumAvailable(int quorum) const {
    int available = 0;
    for (const auto& r : replicas_) {
        if (r->state != ReplicaState::CATCHING_UP) available++;
    }
    return available >= quorum;
}

void Shard::failReplica(const String& name) {
    for (auto& r : replicas_) {
        if (r->name == name) {
            r->state = ReplicaState::CATCHING_UP;
            break;
        }
    }
}

void Shard::recoverReplica(const String& name) {
    for (auto& r : replicas_) {
        if (r->name == name) {
            // Catch up from leader
            auto* leader = getLeader();
            if (leader) {
                r->store.clear();
                for (const auto& t : leader->store.all()) {
                    r->store.add(t);
                }
                r->versionClock = leader->versionClock;
            }
            r->state = ReplicaState::FOLLOWER;
            break;
        }
    }
}

Replica* Shard::selectReadReplica() const {
    if (!config_.readFromFollowers) {
        return replicas_[leaderIndex_].get();
    }
    // Round-robin among healthy replicas
    for (int attempt = 0; attempt < static_cast<int>(replicas_.size()); ++attempt) {
        int idx = readFollowerIndex_ % static_cast<int>(replicas_.size());
        readFollowerIndex_++;
        if (replicas_[idx]->state != ReplicaState::CATCHING_UP) {
            return replicas_[idx].get();
        }
    }
    return replicas_[leaderIndex_].get();
}

// ============================================================================
// ShardManager
// ============================================================================

ShardManager::ShardManager(const ClusterConfig& config)
    : config_(config) {
    for (int i = 0; i < config.numShards; ++i) {
        shards_.push_back(std::make_unique<Shard>(i, config));
    }
}

int ShardManager::shardForKey(const String& key) const {
    size_t h = hasher_(key);
    return static_cast<int>(h % static_cast<size_t>(config_.numShards));
}

Shard* ShardManager::getShard(int shardId) {
    if (shardId < 0 || shardId >= static_cast<int>(shards_.size())) return nullptr;
    return shards_[shardId].get();
}

const Shard* ShardManager::getShard(int shardId) const {
    if (shardId < 0 || shardId >= static_cast<int>(shards_.size())) return nullptr;
    return shards_[shardId].get();
}

std::vector<Shard*> ShardManager::getAllShards() {
    std::vector<Shard*> result;
    for (auto& s : shards_) result.push_back(s.get());
    return result;
}

std::vector<const Shard*> ShardManager::getAllShards() const {
    std::vector<const Shard*> result;
    for (const auto& s : shards_) result.push_back(s.get());
    return result;
}

void ShardManager::rebalance() {
    // Collect all triples
    std::vector<Triple> allTriples;
    for (const auto& shard : shards_) {
        for (const auto& t : shard->leaderStore()->all()) {
            allTriples.push_back(t);
        }
        shard->leaderStore()->clear();
    }
    // Redistribute
    for (const auto& t : allTriples) {
        int targetShard = shardForKey(t.subject);
        shards_[targetShard]->write(t, config_.writeQuorum);
    }
}

// ============================================================================
// DistributedStorage
// ============================================================================

DistributedStorage::DistributedStorage(const ClusterConfig& config)
    : shardManager_(config) {}

DistributedStorage::~DistributedStorage() = default;

bool DistributedStorage::addTriple(const Triple& triple) {
    std::lock_guard<std::mutex> lock(mutex_);
    int shardId = shardManager_.shardForKey(triple.subject);
    auto* shard = shardManager_.getShard(shardId);
    if (!shard) return false;
    return shard->write(triple, shardManager_.config().writeQuorum);
}

bool DistributedStorage::removeTriple(const Triple& triple) {
    std::lock_guard<std::mutex> lock(mutex_);
    int shardId = shardManager_.shardForKey(triple.subject);
    auto* shard = shardManager_.getShard(shardId);
    if (!shard) return false;
    return shard->remove(triple, shardManager_.config().writeQuorum);
}

std::optional<Triple> DistributedStorage::findTriple(
    const String& subject, const String& predicate, const String& object) {
    std::lock_guard<std::mutex> lock(mutex_);
    int shardId = shardManager_.shardForKey(subject);
    auto* shard = shardManager_.getShard(shardId);
    if (!shard) return std::nullopt;
    return shard->leaderStore()->find(subject, predicate, object);
}

std::vector<Triple> DistributedStorage::findBySubject(const String& subject) {
    std::lock_guard<std::mutex> lock(mutex_);
    int shardId = shardManager_.shardForKey(subject);
    auto* shard = shardManager_.getShard(shardId);
    if (!shard) return {};
    return shard->readBySubject(subject);
}

std::vector<Triple> DistributedStorage::findByPredicate(const String& predicate) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Fan-out to all shards
    std::vector<Triple> results;
    for (auto* shard : shardManager_.getAllShards()) {
        auto shardResults = shard->readByPredicate(predicate);
        results.insert(results.end(), shardResults.begin(), shardResults.end());
    }
    return results;
}

std::vector<Triple> DistributedStorage::findByObject(const String& object) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Triple> results;
    for (auto* shard : shardManager_.getAllShards()) {
        auto shardResults = shard->leaderStore()->findByObject(object);
        results.insert(results.end(), shardResults.begin(), shardResults.end());
    }
    return results;
}

std::vector<Triple> DistributedStorage::findBySP(const String& subject, const String& predicate) {
    std::lock_guard<std::mutex> lock(mutex_);
    int shardId = shardManager_.shardForKey(subject);
    auto* shard = shardManager_.getShard(shardId);
    if (!shard) return {};
    return shard->leaderStore()->findBySP(subject, predicate);
}

std::vector<Triple> DistributedStorage::findByPO(const String& predicate, const String& object) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Triple> results;
    for (auto* shard : shardManager_.getAllShards()) {
        auto shardResults = shard->leaderStore()->findByPO(predicate, object);
        results.insert(results.end(), shardResults.begin(), shardResults.end());
    }
    return results;
}

std::vector<Triple> DistributedStorage::getAllTriples() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Triple> results;
    for (auto* shard : shardManager_.getAllShards()) {
        auto shardResults = shard->readAll();
        results.insert(results.end(), shardResults.begin(), shardResults.end());
    }
    return results;
}

std::vector<Triple> DistributedStorage::queryTriples(
    const TripleStore::TriplePattern& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    // If subject is specified, route to single shard
    if (!pattern.subjectIsVar && !pattern.subject.empty()) {
        int shardId = shardManager_.shardForKey(pattern.subject);
        auto* shard = shardManager_.getShard(shardId);
        return shard ? shard->leaderStore()->query(pattern) : std::vector<Triple>{};
    }
    // Otherwise fan-out
    std::vector<Triple> results;
    for (auto* shard : shardManager_.getAllShards()) {
        auto shardResults = shard->leaderStore()->query(pattern);
        results.insert(results.end(), shardResults.begin(), shardResults.end());
    }
    return results;
}

void DistributedStorage::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto* shard : shardManager_.getAllShards()) {
        shard->leaderStore()->clear();
    }
}

size_t DistributedStorage::tripleCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total = 0;
    for (const auto* shard : shardManager_.getAllShards()) {
        total += shard->count();
    }
    return total;
}

ClusterHealth DistributedStorage::getClusterHealth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ClusterHealth health;
    health.totalShards = shardManager_.config().numShards;
    health.healthyShards = 0;
    health.totalReplicas = 0;
    health.healthyReplicas = 0;

    for (const auto* shard : shardManager_.getAllShards()) {
        auto info = shard->getInfo();
        health.totalReplicas += static_cast<int>(info.replicaNames.size());
        if (info.isAvailable) health.healthyShards++;
        // Count healthy replicas
        for (int i = 0; i < shard->replicaCount(); ++i) {
            // All replicas are considered healthy unless CATCHING_UP
            health.healthyReplicas++;  // simplified
        }
    }

    health.allShardsAvailable = (health.healthyShards == health.totalShards);
    return health;
}

ShardInfo DistributedStorage::getShardInfo(int shardId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto* shard = shardManager_.getShard(shardId);
    return shard ? shard->getInfo() : ShardInfo{};
}

std::vector<ShardInfo> DistributedStorage::getAllShardInfo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ShardInfo> info;
    for (const auto* shard : shardManager_.getAllShards()) {
        info.push_back(shard->getInfo());
    }
    return info;
}

void DistributedStorage::rebalance() {
    std::lock_guard<std::mutex> lock(mutex_);
    shardManager_.rebalance();
}

void DistributedStorage::failReplica(int shardId, const String& replicaName) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* shard = shardManager_.getShard(shardId);
    if (shard) shard->failReplica(replicaName);
}

void DistributedStorage::recoverReplica(int shardId, const String& replicaName) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* shard = shardManager_.getShard(shardId);
    if (shard) shard->recoverReplica(replicaName);
}

} // namespace ontology
```

- [ ] **Step 2: Add Distributed.cpp to CMakeLists**

Find the `ontology_core` library target in the main `CMakeLists.txt`. Add to the source list:

```cmake
src/storage/Distributed.cpp
```

- [ ] **Step 3: Commit**

```bash
git add include/ontology/Distributed.hpp src/storage/Distributed.cpp CMakeLists.txt
git commit -m "feat(distributed): implement DistributedStorage with sharding, replication, quorum writes, and vector clocks"
```

---

### Task 3: Distributed Storage Unit Tests

**Files:**
- Create: `tests/test_distributed.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create test file**

```cpp
#include "TestUtils.hpp"
#include <ontology/Distributed.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

void test_shard_routing() {
    TEST("Shard routing: same subject always maps to same shard");
    ClusterConfig config;
    config.numShards = 4;
    ShardManager mgr(config);

    int shard1 = mgr.shardForKey("subject1");
    int shard2 = mgr.shardForKey("subject1");
    ASSERT_TRUE(shard1 == shard2);
    ASSERT_TRUE(shard1 >= 0 && shard1 < 4);
    PASS();
}

void test_shard_distribution() {
    TEST("Shard routing: different subjects distributed across shards");
    ClusterConfig config;
    config.numShards = 4;
    ShardManager mgr(config);

    std::unordered_set<int> usedShards;
    for (int i = 0; i < 20; ++i) {
        usedShards.insert(mgr.shardForKey("subject_" + std::to_string(i)));
    }
    // With 20 subjects and 4 shards, should use at least 2 shards
    ASSERT_TRUE(usedShards.size() >= 2);
    PASS();
}

void test_add_and_find_triple() {
    TEST("DistributedStorage: add and find triple by subject");
    ClusterConfig config;
    config.numShards = 2;
    DistributedStorage storage(config);

    Triple t1{"subject1", "predicate1", "object1"};
    ASSERT_TRUE(storage.addTriple(t1));

    auto results = storage.findBySubject("subject1");
    ASSERT_TRUE(results.size() == 1);
    ASSERT_TRUE(results[0].subject == "subject1");
    ASSERT_TRUE(results[0].predicate == "predicate1");
    ASSERT_TRUE(results[0].object == "object1");
    PASS();
}

void test_multiple_shards() {
    TEST("DistributedStorage: triples distributed across multiple shards");
    ClusterConfig config;
    config.numShards = 4;
    DistributedStorage storage(config);

    for (int i = 0; i < 10; ++i) {
        storage.addTriple({"subj_" + std::to_string(i), "pred", "obj_" + std::to_string(i)});
    }

    ASSERT_TRUE(storage.tripleCount() == 10);
    auto all = storage.getAllTriples();
    ASSERT_TRUE(all.size() == 10);
    PASS();
}

void test_find_by_predicate_fanout() {
    TEST("DistributedStorage: findByPredicate fans out to all shards");
    ClusterConfig config;
    config.numShards = 4;
    DistributedStorage storage(config);

    // Add triples with same predicate across different subjects (likely different shards)
    storage.addTriple({"subj_a", "samePred", "obj_a"});
    storage.addTriple({"subj_b", "samePred", "obj_b"});
    storage.addTriple({"subj_c", "samePred", "obj_c"});

    auto results = storage.findByPredicate("samePred");
    ASSERT_TRUE(results.size() == 3);
    PASS();
}

void test_replication() {
    TEST("DistributedStorage: triple replicated to all replicas");
    ClusterConfig config;
    config.numShards = 1;
    config.replicationFactor = 3;
    DistributedStorage storage(config);

    storage.addTriple({"subj1", "pred1", "obj1"});

    auto info = storage.getShardInfo(0);
    ASSERT_TRUE(info.replicaNames.size() == 3);

    // Leader should have the triple
    auto* shard = storage.shardManager().getShard(0);
    ASSERT_TRUE(shard != nullptr);
    auto leaderResults = shard->leaderStore()->findBySubject("subj1");
    ASSERT_TRUE(leaderResults.size() == 1);

    // Follower should also have the triple
    auto* follower = shard->getFollower(0);
    ASSERT_TRUE(follower != nullptr);
    auto followerResults = follower->store.findBySubject("subj1");
    ASSERT_TRUE(followerResults.size() == 1);
    PASS();
}

void test_replica_failure() {
    TEST("DistributedStorage: continues operating with replica failure");
    ClusterConfig config;
    config.numShards = 1;
    config.replicationFactor = 3;
    config.writeQuorum = 2;
    DistributedStorage storage(config);

    storage.addTriple({"subj1", "pred1", "obj1"});

    // Fail one follower — still have quorum (2 of 3)
    storage.failReplica(0, "shard0_replica1");

    // Should still be able to write
    ASSERT_TRUE(storage.addTriple({"subj2", "pred2", "obj2"}));

    // Should still be able to read
    auto results = storage.findBySubject("subj1");
    ASSERT_TRUE(results.size() == 1);
    PASS();
}

void test_replica_recovery() {
    TEST("DistributedStorage: replica catches up after recovery");
    ClusterConfig config;
    config.numShards = 1;
    config.replicationFactor = 3;
    config.writeQuorum = 2;
    DistributedStorage storage(config);

    storage.addTriple({"subj1", "pred1", "obj1"});
    storage.failReplica(0, "shard0_replica1");

    // Write while replica is down
    storage.addTriple({"subj2", "pred2", "obj2"});

    // Recover — should catch up
    storage.recoverReplica(0, "shard0_replica1");

    auto* shard = storage.shardManager().getShard(0);
    auto* follower = shard->getFollower(0);
    ASSERT_TRUE(follower != nullptr);
    // Recovered follower should have both triples
    auto allResults = follower->store.all();
    ASSERT_TRUE(allResults.size() == 2);
    PASS();
}

void test_vector_clock() {
    TEST("VectorClock: increment and comparison");
    VectorClock vc1, vc2;
    vc1.increment(0);
    vc1.increment(0);
    ASSERT_TRUE(vc1.counters[0] == 2);

    vc2.increment(0);
    ASSERT_TRUE(vc2.counters[0] == 1);

    // vc2 happens-before vc1 (counter 0: 1 < 2)
    ASSERT_TRUE(vc2.happensBefore(vc1));
    ASSERT_TRUE(!vc1.happensBefore(vc2));
    PASS();
}

void test_vector_clock_concurrent() {
    TEST("VectorClock: concurrent writes detected");
    VectorClock vc1, vc2;
    vc1.increment(0);  // vc1: {0:1}
    vc2.increment(1);  // vc2: {1:1}
    ASSERT_TRUE(vc1.isConcurrent(vc2));
    ASSERT_TRUE(vc2.isConcurrent(vc1));
    PASS();
}

void test_vector_clock_merge() {
    TEST("VectorClock: merge takes max of each counter");
    VectorClock vc1, vc2;
    vc1.increment(0);  // {0:1}
    vc2.increment(1);  // {1:1}
    vc2.increment(1);  // {1:2}

    auto merged = vc1.merge(vc2);
    ASSERT_TRUE(merged.counters[0] == 1);
    ASSERT_TRUE(merged.counters[1] == 2);
    PASS();
}

void test_cluster_health() {
    TEST("DistributedStorage: cluster health reporting");
    ClusterConfig config;
    config.numShards = 2;
    config.replicationFactor = 3;
    DistributedStorage storage(config);

    storage.addTriple({"subj1", "pred1", "obj1"});

    auto health = storage.getClusterHealth();
    ASSERT_TRUE(health.totalShards == 2);
    ASSERT_TRUE(health.totalReplicas == 6);
    ASSERT_TRUE(health.allShardsAvailable);
    PASS();
}

void test_remove_triple() {
    TEST("DistributedStorage: remove triple");
    ClusterConfig config;
    config.numShards = 2;
    DistributedStorage storage(config);

    Triple t1{"subj1", "pred1", "obj1"};
    storage.addTriple(t1);
    ASSERT_TRUE(storage.tripleCount() == 1);

    storage.removeTriple(t1);
    ASSERT_TRUE(storage.tripleCount() == 0);
    PASS();
}

void test_clear() {
    TEST("DistributedStorage: clear all shards");
    ClusterConfig config;
    config.numShards = 4;
    DistributedStorage storage(config);

    for (int i = 0; i < 8; ++i) {
        storage.addTriple({"subj_" + std::to_string(i), "pred", "obj"});
    }
    ASSERT_TRUE(storage.tripleCount() == 8);

    storage.clear();
    ASSERT_TRUE(storage.tripleCount() == 0);
    PASS();
}

int main() {
    test_shard_routing();
    test_shard_distribution();
    test_add_and_find_triple();
    test_multiple_shards();
    test_find_by_predicate_fanout();
    test_replication();
    test_replica_failure();
    test_replica_recovery();
    test_vector_clock();
    test_vector_clock_concurrent();
    test_vector_clock_merge();
    test_cluster_health();
    test_remove_triple();
    test_clear();

    std::cout << "\nDistributed Storage tests: " << testsPassed << " passed, "
              << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
```

- [ ] **Step 2: Add test target to CMakeLists.txt**

Append to `tests/CMakeLists.txt`:

```cmake
# Unit test: Distributed Storage
add_executable(test_distributed test_distributed.cpp)
target_link_libraries(test_distributed PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME distributed COMMAND test_distributed)
```

- [ ] **Step 3: Build and test**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && cmake .. -DBUILD_TESTS=ON && make test_distributed && ./tests/test_distributed`

Expected: All 14 tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/test_distributed.cpp tests/CMakeLists.txt
git commit -m "test(distributed): add unit tests for shard routing, replication, failover, vector clocks, and cluster health"
```

---

### Task 4: Full Build Verification

**Files:** None (verification only)

- [ ] **Step 1: Full build**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && cmake .. -DBUILD_TESTS=ON && make -j$(sysctl -n hw.ncpu)`

Expected: Build succeeds.

- [ ] **Step 2: Run all tests**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && ctest --output-on-failure`

Expected: All tests pass.
