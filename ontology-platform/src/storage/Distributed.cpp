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
    bool atLeastOneLess = false;
    for (const auto& [id, count] : counters) {
        auto it = other.counters.find(id);
        int otherCount = (it != other.counters.end()) ? it->second : 0;
        if (count > otherCount) return false;
        if (count < otherCount) atLeastOneLess = true;
    }
    for (const auto& [id, count] : other.counters) {
        if (counters.find(id) == counters.end() && count > 0) {
            atLeastOneLess = true;
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

    leader->versionClock.increment(id_);
    VectorClock newClock = leader->versionClock;

    leader->store.add(triple);

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
    if (replica) {
        const auto& allRef = replica->store.all();
        return std::vector<Triple>(allRef.begin(), allRef.end());
    }
    return {};
}

size_t Shard::count() const {
    if (replicas_.empty()) return 0;
    return replicas_[leaderIndex_]->store.count();
}

TripleStore* Shard::leaderStore() {
    auto* leader = getLeader();
    return leader ? &leader->store : nullptr;
}

const TripleStore* Shard::leaderStore() const {
    if (replicas_.empty()) return nullptr;
    return &replicas_[leaderIndex_]->store;
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
    std::vector<Triple> allTriples;
    for (const auto& shard : shards_) {
        for (const auto& t : shard->leaderStore()->all()) {
            allTriples.push_back(t);
        }
        shard->leaderStore()->clear();
    }
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
    if (!pattern.subjectIsVar && !pattern.subject.empty()) {
        int shardId = shardManager_.shardForKey(pattern.subject);
        auto* shard = shardManager_.getShard(shardId);
        return shard ? shard->leaderStore()->query(pattern) : std::vector<Triple>{};
    }
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
        for (int i = 0; i < shard->replicaCount(); ++i) {
            health.healthyReplicas++;
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
