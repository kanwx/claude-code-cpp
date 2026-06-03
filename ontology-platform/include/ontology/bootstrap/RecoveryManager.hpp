#pragma once
#include <ontology/Core.hpp>
#include <ontology/Persistence.hpp>
#include <ontology/storage/HybridStorage.hpp>
#include <memory>
#include <string>
#include <functional>

namespace ontology {

struct RecoveryConfig {
    int reconnectIntervalSeconds = 30;
    int maxConsecutiveWriteFailures = 3;
    int loadTimeoutMs = 60000;
    bool autoRecoveryEnabled = true;
};

class RecoveryManager {
public:
    RecoveryManager(StoragePtr storage,
                    GraphDatabasePtr graphDB,
                    std::shared_ptr<WalManager> walManager,
                    std::shared_ptr<SnapshotManager> snapshotManager,
                    const RecoveryConfig& config = {});

    enum class RecoverySource { None, GraphDB, Snapshot, Wal };
    struct RecoveryResult {
        bool success = false;
        RecoverySource source = RecoverySource::None;
        String details;
    };

    RecoveryResult recover();
    bool isReadOnly() const;

private:
    RecoveryResult recoverFromGraphDB();
    RecoveryResult recoverFromSnapshot();
    RecoveryResult recoverFromWal();

    StoragePtr storage_;
    GraphDatabasePtr graphDB_;
    std::shared_ptr<WalManager> walManager_;
    std::shared_ptr<SnapshotManager> snapshotManager_;
    RecoveryConfig config_;
    bool isReadOnly_ = false;
};

} // namespace ontology
