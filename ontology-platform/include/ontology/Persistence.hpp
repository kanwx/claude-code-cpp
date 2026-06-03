#pragma once

#include "Core.hpp"
#include "Storage.hpp"
#include "RagStorage.hpp"
#include <vector>
#include <memory>
#include <mutex>
#include <fstream>
#include <functional>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <filesystem>
#include <optional>

namespace ontology {

// ============================================================================
// WAL (Write-Ahead Log) 持久化
// 对标: SQLite WAL, LevelDB log, PostgreSQL WAL
// ============================================================================

enum class WalEntryType {
    AddTriple,
    RemoveTriple,
    AddClass,
    RemoveClass,
    AddRelation,
    RemoveRelation,
    AddIndividual,
    RemoveIndividual,
    AddAxiom,
    RemoveAxiom,
    AddChunk,
    RemoveChunk,
    AddDocument,
    RemoveDocument,
    Checkpoint,     // 快照标记
    BeginTxn,
    CommitTxn,
    RollbackTxn
};

struct WalEntry {
    WalEntryType type;
    int64_t timestamp = 0;
    int64_t txnId = 0;
    int64_t lsn = 0;            // Log Sequence Number
    Json data;                   // 操作数据

    String serialize() const {
        Json j;
        j["type"] = static_cast<int>(type);
        j["ts"] = timestamp;
        j["txn"] = txnId;
        j["lsn"] = lsn;
        j["data"] = data;
        return j.dump();
    }

    static WalEntry deserialize(const String& line) {
        WalEntry entry;
        try {
            Json j = Json::parse(line);
            entry.type = static_cast<WalEntryType>(j.value("type", 0));
            entry.timestamp = j.value("ts", int64_t(0));
            entry.txnId = j.value("txn", int64_t(0));
            entry.lsn = j.value("lsn", int64_t(0));
            entry.data = j.value("data", Json());
        } catch (const std::exception&) {}
        return entry;
    }
};

class WalManager {
public:
    struct Config {
        String walDirectory = "./wal";
        int64_t maxWalSize = 64 * 1024 * 1024;  // 64MB per WAL file
        int maxWalFiles = 10;
        int syncIntervalMs = 1000;    // fsync 间隔
        bool enableSync = true;       // 是否 fsync (false = 更快但可能丢数据)
        int checkpointInterval = 1000; // 每 N 条 entry 自动 checkpoint
    };

    explicit WalManager(const Config& config);
    ~WalManager();

    /// 写入 WAL 条目
    int64_t append(WalEntryType type, const Json& data, int64_t txnId = 0);

    /// 批量写入
    std::vector<int64_t> appendBatch(const std::vector<std::pair<WalEntryType, Json>>& entries, int64_t txnId = 0);

    /// 读取 WAL (从指定 LSN)
    std::vector<WalEntry> readFromLsn(int64_t startLsn);

    /// 读取所有未 checkpoint 的 WAL
    std::vector<WalEntry> readUncheckpointed();

    /// 标记 checkpoint (截断 WAL 到此 LSN)
    void checkpoint(int64_t lsn);

    /// 获取当前 LSN
    int64_t currentLsn() const;

    /// 获取 WAL 统计
    Json getStats() const;

    /// 恢复: 重放 WAL
    void replay(std::function<void(const WalEntry&)> callback);

    // Authority source: confirmation tracking
    void markConfirmed(int64_t lsn);
    int64_t lastConfirmedLsn() const;
    void truncateConfirmed();

    // Authority source: replay from specific LSN
    size_t replayFrom(int64_t fromLsn, std::function<void(const WalEntry&)> callback);

private:
    Config config_;
    int64_t currentLsn_ = 0;
    int64_t checkpointLsn_ = 0;
    int64_t confirmedLsn_ = 0;
    int64_t currentFileOffset_ = 0;
    int currentFileIndex_ = 0;

    std::ofstream walStream_;
    mutable std::mutex mutex_;
    int entriesSinceCheckpoint_ = 0;

    void openCurrentFile();
    void rotateIfNeeded();
    void sync();
    String walFilePath(int index) const;
    String checkpointFilePath() const;
};

// ============================================================================
// 快照管理
// 对标: Redis RDB, LevelDB SST, Neo4j snapshot
// ============================================================================

struct SnapshotInfo {
    String id;
    std::chrono::system_clock::time_point timestamp;
    size_t fileSize = 0;
};

class SnapshotManager {
public:
    struct Config {
        String snapshotDirectory = "./snapshots";
        int maxSnapshots = 5;            // 保留的最大快照数
        bool compressSnapshots = false;   // 是否压缩快照
        int64_t snapshotIntervalMs = 60000;  // 自动快照间隔
    };

    explicit SnapshotManager(const Config& config);
    ~SnapshotManager();

    /// 创建快照
    String createSnapshot(
        StoragePtr storage,
        std::shared_ptr<RagStorage> ragStorage = nullptr
    );

    /// 从快照恢复
    bool restoreSnapshot(
        const String& snapshotId,
        StoragePtr storage,
        std::shared_ptr<RagStorage> ragStorage = nullptr
    );

    /// 列出所有快照
    std::vector<String> listSnapshots() const;

    /// 删除快照
    bool deleteSnapshot(const String& snapshotId);

    /// 获取最新快照 ID
    String latestSnapshotId();

    /// 自动快照线程
    void startAutoSnapshot(
        StoragePtr storage,
        std::shared_ptr<RagStorage> ragStorage = nullptr
    );
    void stopAutoSnapshot();

    /// 统计
    Json getStats() const;

    std::chrono::system_clock::time_point getLatestSnapshotTime() const;
    std::optional<SnapshotInfo> getLatestSnapshot() const;

private:
    Config config_;
    mutable std::mutex mutex_;
    bool autoSnapshotRunning_ = false;
    std::thread autoSnapshotThread_;
    std::condition_variable cv_;

    String snapshotFilePath(const String& id) const;
    void cleanupOldSnapshots();
};

// ============================================================================
// 流式响应
// 对标: OpenAI streaming, Server-Sent Events (SSE)
// ============================================================================

struct StreamChunk {
    enum class Type {
        Text,           // 文本内容
        Citation,       // 引用
        Entity,         // 实体信息
        GraphFact,      // 图谱事实
        DebugInfo,      // 调试信息
        Progress,       // 进度
        Done            // 结束标记
    };

    Type type = Type::Text;
    String content;
    float progress = 0.0f;
    Json metadata;

    String toSSE() const {
        Json j;
        j["type"] = type == Type::Text ? "text" :
                    type == Type::Citation ? "citation" :
                    type == Type::Entity ? "entity" :
                    type == Type::GraphFact ? "graph_fact" :
                    type == Type::DebugInfo ? "debug" :
                    type == Type::Progress ? "progress" : "done";
        j["content"] = content;
        if (progress > 0) j["progress"] = progress;
        if (!metadata.is_null()) j["metadata"] = metadata;
        return "data: " + j.dump() + "\n\n";
    }
};

class StreamGenerator {
public:
    using Callback = std::function<void(const StreamChunk&)>;

    /// 流式生成 RAG 回答
    /// results: JSON-serialized retrieval results (each with id, text, documentId, fusedScore, matchedEntities, matchedTerms)
    /// communities: JSON-serialized community summaries
    void generateStreamingAnswer(
        const std::vector<Json>& resultJsons,
        const std::vector<Json>& communityJsons,
        const std::vector<Triple>& graphFacts,
        Callback callback,
        int chunkDelayMs = 50
    );

    /// 流式生成检索过程 (实时反馈)
    void generateStreamingRetrieval(
        const String& query,
        Callback callback,
        const std::vector<Json>& resultJsons,
        const std::vector<Json>& communityJsons,
        const std::vector<Triple>& graphFacts
    );

private:
    String formatEntityBlock(
        const std::vector<Individual>& entities,
        const std::vector<Triple>& graphFacts
    ) const;

    String formatFactBlock(
        const std::vector<Triple>& facts,
        int maxFacts = 20
    ) const;
};

} // namespace ontology
