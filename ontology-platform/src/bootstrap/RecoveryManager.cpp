#include <ontology/bootstrap/RecoveryManager.hpp>
#include <spdlog/spdlog.h>

namespace ontology {

RecoveryManager::RecoveryManager(StoragePtr storage,
                                 GraphDatabasePtr graphDB,
                                 std::shared_ptr<WalManager> walManager,
                                 std::shared_ptr<SnapshotManager> snapshotManager,
                                 const RecoveryConfig& config)
    : storage_(std::move(storage))
    , graphDB_(std::move(graphDB))
    , walManager_(std::move(walManager))
    , snapshotManager_(std::move(snapshotManager))
    , config_(config) {}

RecoveryManager::RecoveryResult RecoveryManager::recover() {
    // Priority 1: GraphDB
    if (graphDB_ && graphDB_->isConnected()) {
        spdlog::info("Recovery: attempting load from graphDB");
        auto result = recoverFromGraphDB();
        if (result.success) return result;
        spdlog::warn("Recovery: graphDB load failed: {}", result.details);
    }

    // Priority 2: Snapshot
    if (snapshotManager_) {
        spdlog::info("Recovery: attempting restore from snapshot");
        auto result = recoverFromSnapshot();
        if (result.success) {
            isReadOnly_ = true;
            result.details += " (read-only mode)";
            return result;
        }
    }

    // Priority 3: WAL
    if (walManager_) {
        spdlog::info("Recovery: attempting WAL replay");
        auto result = recoverFromWal();
        if (result.success) {
            isReadOnly_ = true;
            result.details += " (read-only mode)";
            return result;
        }
    }

    // No recovery possible
    isReadOnly_ = true;
    if (graphDB_) {
        storage_->startReconnectionLoop();
    }
    return {false, RecoverySource::None, "No recovery source available (read-only mode)"};
}

RecoveryManager::RecoveryResult RecoveryManager::recoverFromGraphDB() {
    if (!storage_->loadFromGraphDB()) {
        return {false, RecoverySource::GraphDB, "loadFromGraphDB() returned false"};
    }
    return {true, RecoverySource::GraphDB, "Loaded from graphDB successfully"};
}

RecoveryManager::RecoveryResult RecoveryManager::recoverFromSnapshot() {
    auto latestId = snapshotManager_->latestSnapshotId();
    if (latestId.empty()) {
        return {false, RecoverySource::Snapshot, "No snapshots available"};
    }
    if (!snapshotManager_->restoreSnapshot(latestId, storage_, nullptr)) {
        return {false, RecoverySource::Snapshot, "Snapshot restore failed for " + latestId};
    }
    return {true, RecoverySource::Snapshot, "Restored from snapshot " + latestId};
}

RecoveryManager::RecoveryResult RecoveryManager::recoverFromWal() {
    if (!walManager_) {
        return {false, RecoverySource::Wal, "No WAL manager"};
    }
    int loaded = 0;
    walManager_->replay([&](const WalEntry& entry) {
        switch (entry.type) {
            case WalEntryType::AddTriple: {
                Triple t;
                t.subject = entry.data.value("subject", "");
                t.predicate = entry.data.value("predicate", "");
                t.object = entry.data.value("object", "");
                t.confidence = entry.data.value("confidence", 1.0f);
                storage_->addTriple(t);
                loaded++;
                break;
            }
            case WalEntryType::AddClass: {
                Class cls;
                cls.id = entry.data.value("id", "");
                cls.name = entry.data.value("name", "");
                storage_->addClass(cls);
                loaded++;
                break;
            }
            case WalEntryType::AddIndividual: {
                Individual ind;
                ind.id = entry.data.value("id", "");
                ind.name = entry.data.value("name", "");
                ind.classId = entry.data.value("classId", "");
                storage_->addIndividual(ind);
                loaded++;
                break;
            }
            case WalEntryType::AddRelation: {
                Relation rel;
                rel.id = entry.data.value("id", "");
                rel.name = entry.data.value("name", "");
                storage_->addRelation(rel);
                loaded++;
                break;
            }
            default: break;
        }
    });
    if (loaded > 0) {
        return {true, RecoverySource::Wal,
                "Replayed " + std::to_string(loaded) + " WAL entries"};
    }
    return {false, RecoverySource::Wal, "No replayable WAL entries"};
}

bool RecoveryManager::isReadOnly() const {
    return isReadOnly_;
}

} // namespace ontology
