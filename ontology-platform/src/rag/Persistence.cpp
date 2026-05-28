#include <ontology/Persistence.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <fstream>

namespace ontology {

namespace fs = std::filesystem;

// ============================================================================
// WalManager
// ============================================================================

WalManager::WalManager(const Config& config) : config_(config) {
    fs::create_directories(config_.walDirectory);
    openCurrentFile();
}

WalManager::~WalManager() {
    if (walStream_.is_open()) {
        walStream_.flush();
        walStream_.close();
    }
}

int64_t WalManager::append(WalEntryType type, const Json& data, int64_t txnId) {
    std::lock_guard<std::mutex> lock(mutex_);

    WalEntry entry;
    entry.type = type;
    entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    entry.txnId = txnId;
    entry.lsn = ++currentLsn_;
    entry.data = data;

    String line = entry.serialize() + "\n";
    walStream_ << line;
    currentFileOffset_ += static_cast<int64_t>(line.size());

    entriesSinceCheckpoint_++;

    if (config_.enableSync && entriesSinceCheckpoint_ % 100 == 0) {
        sync();
    }

    // Auto checkpoint
    if (entriesSinceCheckpoint_ >= config_.checkpointInterval) {
        // Don't auto-checkpoint here, just note it
    }

    rotateIfNeeded();

    return entry.lsn;
}

std::vector<int64_t> WalManager::appendBatch(
    const std::vector<std::pair<WalEntryType, Json>>& entries, int64_t txnId
) {
    std::vector<int64_t> lsns;
    lsns.reserve(entries.size());
    for (const auto& [type, data] : entries) {
        lsns.push_back(append(type, data, txnId));
    }
    if (config_.enableSync) sync();
    return lsns;
}

std::vector<WalEntry> WalManager::readFromLsn(int64_t startLsn) {
    std::vector<WalEntry> entries;

    // Read all WAL files and filter by LSN
    for (int i = 0; i <= currentFileIndex_; i++) {
        String path = walFilePath(i);
        std::ifstream file(path);
        if (!file.is_open()) continue;

        String line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            auto entry = WalEntry::deserialize(line);
            if (entry.lsn >= startLsn) {
                entries.push_back(entry);
            }
        }
    }

    return entries;
}

std::vector<WalEntry> WalManager::readUncheckpointed() {
    return readFromLsn(checkpointLsn_ + 1);
}

void WalManager::checkpoint(int64_t lsn) {
    std::lock_guard<std::mutex> lock(mutex_);

    checkpointLsn_ = lsn;

    // Write checkpoint marker
    WalEntry entry;
    entry.type = WalEntryType::Checkpoint;
    entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    entry.lsn = ++currentLsn_;
    entry.data = {{"checkpointLsn", lsn}};

    walStream_ << entry.serialize() << "\n";
    sync();

    // Write checkpoint file
    std::ofstream cpFile(checkpointFilePath());
    if (cpFile.is_open()) {
        cpFile << lsn << "\n";
        cpFile.close();
    }

    entriesSinceCheckpoint_ = 0;
}

int64_t WalManager::currentLsn() const {
    return currentLsn_;
}

Json WalManager::getStats() const {
    Json j;
    j["currentLsn"] = currentLsn_;
    j["checkpointLsn"] = checkpointLsn_;
    j["currentFileIndex"] = currentFileIndex_;
    j["entriesSinceCheckpoint"] = entriesSinceCheckpoint_;
    return j;
}

void WalManager::replay(std::function<void(const WalEntry&)> callback) {
    // Read checkpoint file to find starting point
    int64_t startLsn = 0;
    std::ifstream cpFile(checkpointFilePath());
    if (cpFile.is_open()) {
        cpFile >> startLsn;
        cpFile.close();
    }

    auto entries = readFromLsn(startLsn);

    for (const auto& entry : entries) {
        if (entry.type == WalEntryType::Checkpoint) continue;
        callback(entry);
    }

    // Update current LSN
    if (!entries.empty()) {
        currentLsn_ = entries.back().lsn;
    }
}

void WalManager::openCurrentFile() {
    // Find the latest WAL file
    currentFileIndex_ = 0;
    currentFileOffset_ = 0;

    for (int i = 0; i < config_.maxWalFiles; i++) {
        String path = walFilePath(i);
        if (fs::exists(path)) {
            currentFileIndex_ = i;
            currentFileOffset_ = static_cast<int64_t>(fs::file_size(path));
        } else {
            break;
        }
    }

    // Open in append mode
    walStream_.open(walFilePath(currentFileIndex_), std::ios::app);
}

void WalManager::rotateIfNeeded() {
    if (currentFileOffset_ < config_.maxWalSize) return;

    walStream_.flush();
    walStream_.close();

    currentFileIndex_++;
    currentFileOffset_ = 0;

    // Remove old WAL files if exceeding max
    while (currentFileIndex_ >= config_.maxWalFiles) {
        String oldPath = walFilePath(currentFileIndex_ - config_.maxWalFiles);
        if (fs::exists(oldPath)) {
            fs::remove(oldPath);
        }
        currentFileIndex_--;
    }

    walStream_.open(walFilePath(currentFileIndex_), std::ios::app);
}

void WalManager::sync() {
    if (walStream_.is_open()) {
        walStream_.flush();
    }
}

String WalManager::walFilePath(int index) const {
    return config_.walDirectory + "/wal_" + std::to_string(index) + ".log";
}

String WalManager::checkpointFilePath() const {
    return config_.walDirectory + "/checkpoint.txt";
}

// ============================================================================
// SnapshotManager
// ============================================================================

SnapshotManager::SnapshotManager(const Config& config) : config_(config) {
    fs::create_directories(config_.snapshotDirectory);
}

SnapshotManager::~SnapshotManager() {
    stopAutoSnapshot();
}

String SnapshotManager::createSnapshot(
    StoragePtr storage,
    std::shared_ptr<RagStorage> ragStorage
) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Generate snapshot ID (timestamp-based)
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    String snapshotId = "snap_" + std::to_string(ms);

    Json snapshot;

    // Store ontology data
    if (storage) {
        // Classes
        Json classesArr = Json::array();
        for (const auto& cls : storage->getAllClasses()) {
            classesArr.push_back(cls.toJson());
        }
        snapshot["classes"] = classesArr;

        // Relations
        Json relsArr = Json::array();
        for (const auto& rel : storage->getAllRelations()) {
            relsArr.push_back(rel.toJson());
        }
        snapshot["relations"] = relsArr;

        // Individuals
        Json indsArr = Json::array();
        for (const auto& ind : storage->getAllIndividuals()) {
            Json indJ;
            indJ["id"] = ind.id;
            indJ["name"] = ind.name;
            indJ["classId"] = ind.classId;
            indJ["properties"] = ind.properties;
            indJ["relations"] = ind.relations;
            indJ["importance"] = ind.importance;
            if (!ind.embedding.empty()) indJ["embedding"] = ind.embedding;
            if (!ind.validFrom.empty()) indJ["validFrom"] = ind.validFrom;
            if (!ind.validTo.empty()) indJ["validTo"] = ind.validTo;
            indsArr.push_back(indJ);
        }
        snapshot["individuals"] = indsArr;

        // Triples
        Json triplesArr = Json::array();
        for (const auto& t : storage->getAllTriples()) {
            triplesArr.push_back(t.toJson());
        }
        snapshot["triples"] = triplesArr;
    }

    // Store RAG data
    if (ragStorage) {
        Json kbsArr = Json::array();
        for (const auto& kb : ragStorage->listKnowledgeBases()) {
            kbsArr.push_back(kb.toJson());
        }
        snapshot["knowledgeBases"] = kbsArr;

        Json docsArr = Json::array();
        for (const auto& doc : ragStorage->listDocuments()) {
            docsArr.push_back(doc.toJson());
        }
        snapshot["documents"] = docsArr;
    }

    snapshot["metadata"] = {
        {"id", snapshotId},
        {"timestamp", ms},
        {"version", "1.0"}
    };

    // Write to file
    String path = snapshotFilePath(snapshotId);
    std::ofstream file(path);
    if (!file.is_open()) return "";
    file << snapshot.dump(2);
    file.close();

    // Cleanup old snapshots
    cleanupOldSnapshots();

    return snapshotId;
}

bool SnapshotManager::restoreSnapshot(
    const String& snapshotId,
    StoragePtr storage,
    std::shared_ptr<RagStorage> ragStorage
) {
    std::lock_guard<std::mutex> lock(mutex_);

    String path = snapshotFilePath(snapshotId);
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    try {
        Json snapshot = Json::parse(buffer.str());

        // Clear existing data
        if (storage) storage->clear();

        // Restore classes
        if (snapshot.contains("classes") && storage) {
            for (const auto& cj : snapshot["classes"]) {
                Class cls;
                cls.id = cj.value("id", "");
                cls.name = cj.value("name", "");
                cls.description = cj.value("description", "");
                if (cj.contains("superClasses")) {
                    for (const auto& sc : cj["superClasses"]) cls.superClasses.push_back(sc.get<String>());
                }
                cls.validFrom = cj.value("validFrom", "");
                cls.validTo = cj.value("validTo", "");
                storage->addClass(cls);
            }
        }

        // Restore relations
        if (snapshot.contains("relations") && storage) {
            for (const auto& rj : snapshot["relations"]) {
                Relation rel;
                rel.id = rj.value("id", "");
                rel.name = rj.value("name", "");
                rel.description = rj.value("description", "");
                rel.domain = rj.value("domain", "");
                rel.range = rj.value("range", "");
                rel.isFunctional = rj.value("isFunctional", false);
                rel.isInverseFunctional = rj.value("isInverseFunctional", false);
                rel.isTransitive = rj.value("isTransitive", false);
                rel.isSymmetric = rj.value("isSymmetric", false);
                rel.isReflexive = rj.value("isReflexive", false);
                rel.isAntisymmetric = rj.value("isAntisymmetric", false);
                rel.inverseProperty = rj.value("inverseProperty", "");
                if (rj.contains("superProperties")) {
                    rel.superProperties = rj["superProperties"].get<std::vector<String>>();
                }
                if (rj.contains("embedding")) {
                    rel.embedding = rj["embedding"].get<std::vector<float>>();
                }
                storage->addRelation(rel);
            }
        }

        // Restore individuals
        if (snapshot.contains("individuals") && storage) {
            for (const auto& ij : snapshot["individuals"]) {
                Individual ind;
                ind.id = ij.value("id", "");
                ind.name = ij.value("name", "");
                ind.classId = ij.value("classId", "");
                if (ij.contains("properties")) ind.properties = ij["properties"];
                if (ij.contains("relations")) ind.relations = ij["relations"].get<std::unordered_map<String, std::vector<String>>>();
                ind.importance = ij.value("importance", 1.0f);
                if (ij.contains("embedding")) {
                    ind.embedding = ij["embedding"].get<std::vector<float>>();
                }
                ind.validFrom = ij.value("validFrom", "");
                ind.validTo = ij.value("validTo", "");
                storage->addIndividual(ind);
            }
        }

        // Restore triples
        if (snapshot.contains("triples") && storage) {
            for (const auto& tj : snapshot["triples"]) {
                Triple t;
                t.subject = tj.value("subject", "");
                t.predicate = tj.value("predicate", "");
                t.object = tj.value("object", "");
                t.isLiteral = tj.value("isLiteral", false);
                t.confidence = tj.value("confidence", 1.0f);
                t.weight = tj.value("weight", 1.0f);
                t.source = tj.value("source", "");
                t.provenance = tj.value("provenance", "");
                t.validFrom = tj.value("validFrom", "");
                t.validTo = tj.value("validTo", "");
                if (tj.contains("embedding")) {
                    t.embedding = tj["embedding"].get<std::vector<float>>();
                }
                storage->addTriple(t);
            }
        }

        return true;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("RAG JSON error: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        spdlog::error("RAG error: {}", e.what());
        return false;
    }
}

std::vector<String> SnapshotManager::listSnapshots() {
    std::vector<String> snapshots;

    for (const auto& entry : fs::directory_iterator(config_.snapshotDirectory)) {
        if (entry.path().extension() == ".json") {
            String stem = entry.path().stem().string();
            if (stem.substr(0, 5) == "snap_") {
                snapshots.push_back(stem);
            }
        }
    }

    std::sort(snapshots.begin(), snapshots.end(), std::greater<String>());
    return snapshots;
}

bool SnapshotManager::deleteSnapshot(const String& snapshotId) {
    String path = snapshotFilePath(snapshotId);
    if (fs::exists(path)) {
        return fs::remove(path);
    }
    return false;
}

String SnapshotManager::latestSnapshotId() {
    auto snapshots = listSnapshots();
    return snapshots.empty() ? "" : snapshots[0];
}

void SnapshotManager::startAutoSnapshot(
    StoragePtr storage,
    std::shared_ptr<RagStorage> ragStorage
) {
    if (autoSnapshotRunning_) return;
    autoSnapshotRunning_ = true;

    autoSnapshotThread_ = std::thread([this, storage, ragStorage]() {
        while (autoSnapshotRunning_) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock,
                    std::chrono::milliseconds(config_.snapshotIntervalMs),
                    [this] { return !autoSnapshotRunning_; });
            }

            if (!autoSnapshotRunning_) break;

            createSnapshot(storage, ragStorage);
        }
    });
}

void SnapshotManager::stopAutoSnapshot() {
    autoSnapshotRunning_ = false;
    cv_.notify_all();
    if (autoSnapshotThread_.joinable()) {
        autoSnapshotThread_.join();
    }
}

Json SnapshotManager::getStats() const {
    Json j;
    int count = 0;
    int64_t totalSize = 0;
    for (const auto& entry : fs::directory_iterator(config_.snapshotDirectory)) {
        if (entry.path().extension() == ".json") {
            count++;
            totalSize += static_cast<int64_t>(fs::file_size(entry.path()));
        }
    }
    j["snapshotCount"] = count;
    j["totalSizeBytes"] = totalSize;
    j["autoSnapshotRunning"] = autoSnapshotRunning_;
    return j;
}

String SnapshotManager::snapshotFilePath(const String& id) const {
    return config_.snapshotDirectory + "/" + id + ".json";
}

void SnapshotManager::cleanupOldSnapshots() {
    auto snapshots = listSnapshots();

    while (static_cast<int>(snapshots.size()) > config_.maxSnapshots) {
        String oldest = snapshots.back();
        snapshots.pop_back();
        String path = snapshotFilePath(oldest);
        if (fs::exists(path)) {
            fs::remove(path);
        }
    }
}

// ============================================================================
// StreamGenerator
// ============================================================================

void StreamGenerator::generateStreamingAnswer(
    const std::vector<Json>& resultJsons,
    const std::vector<Json>& communityJsons,
    const std::vector<Triple>& graphFacts,
    Callback callback,
    int chunkDelayMs
) {
    // Progress: start
    StreamChunk startChunk;
    startChunk.type = StreamChunk::Type::Progress;
    startChunk.progress = 0.0f;
    startChunk.content = "开始检索...";
    callback(startChunk);

    // Stream retrieved chunks
    if (!resultJsons.empty()) {
        StreamChunk headerChunk;
        headerChunk.type = StreamChunk::Type::Text;
        headerChunk.content = "=== 相关文档片段 ===\n\n";
        headerChunk.progress = 0.1f;
        callback(headerChunk);

        int totalChunks = static_cast<int>(resultJsons.size());
        for (int i = 0; i < totalChunks; i++) {
            const auto& r = resultJsons[i];

            String docId = r.value("documentId", "");
            float fusedScore = r.value("fusedScore", 0.0f);

            // Stream each chunk as a citation
            StreamChunk citChunk;
            citChunk.type = StreamChunk::Type::Citation;
            citChunk.content = "【文档 " + docId +
                " | 相关度: " + std::to_string(static_cast<int>(fusedScore * 100)) + "%】\n";
            citChunk.progress = 0.1f + 0.5f * (static_cast<float>(i + 1) / totalChunks);
            citChunk.metadata = r;
            callback(citChunk);

            // Stream text
            StreamChunk textChunk;
            textChunk.type = StreamChunk::Type::Text;
            String text = r.value("text", "");
            if (text.length() > 500) text = text.substr(0, 500) + "...";
            textChunk.content = text + "\n\n";
            callback(textChunk);

            if (chunkDelayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(chunkDelayMs));
            }
        }
    }

    // Stream entities
    StreamChunk entityHeader;
    entityHeader.type = StreamChunk::Type::Text;
    entityHeader.content = "\n=== 相关实体 ===\n\n";
    entityHeader.progress = 0.7f;
    callback(entityHeader);

    for (const auto& r : resultJsons) {
        if (r.contains("matchedEntities") && r["matchedEntities"].is_array()) {
            StreamChunk entChunk;
            entChunk.type = StreamChunk::Type::Entity;
            entChunk.content = "";
            for (const auto& e : r["matchedEntities"]) {
                entChunk.content += "- " + e.get<String>() + "\n";
            }
            entChunk.progress = 0.7f;
            callback(entChunk);
        }
    }

    // Stream graph facts
    if (!graphFacts.empty()) {
        StreamChunk factHeader;
        factHeader.type = StreamChunk::Type::Text;
        factHeader.content = "\n=== 知识图谱 ===\n\n";
        factHeader.progress = 0.85f;
        callback(factHeader);

        int factCount = 0;
        for (const auto& t : graphFacts) {
            if (factCount++ >= 20) break;

            StreamChunk factChunk;
            factChunk.type = StreamChunk::Type::GraphFact;
            factChunk.content = "- " + t.subject + " → " + t.predicate + " → " + t.object +
                " (置信度: " + std::to_string(static_cast<int>(t.confidence * 100)) + "%)\n";
            factChunk.metadata = t.toJson();
            factChunk.progress = 0.85f + 0.1f * (static_cast<float>(factCount) / 20.0f);
            callback(factChunk);
        }
    }

    // Stream community summaries
    if (!communityJsons.empty()) {
        StreamChunk commHeader;
        commHeader.type = StreamChunk::Type::Text;
        commHeader.content = "\n=== 社区知识 ===\n\n";
        commHeader.progress = 0.95f;
        callback(commHeader);

        for (size_t i = 0; i < communityJsons.size() && i < 5; i++) {
            StreamChunk commChunk;
            commChunk.type = StreamChunk::Type::Text;
            String commId = communityJsons[i].value("id", "");
            String summary = communityJsons[i].value("summary", "");
            commChunk.content = "【社区 " + commId + "】" + summary + "\n\n";
            callback(commChunk);
        }
    }

    // Done
    StreamChunk doneChunk;
    doneChunk.type = StreamChunk::Type::Done;
    doneChunk.content = "";
    doneChunk.progress = 1.0f;
    callback(doneChunk);
}

void StreamGenerator::generateStreamingRetrieval(
    const String& query,
    Callback callback,
    const std::vector<Json>& resultJsons,
    const std::vector<Json>& communityJsons,
    const std::vector<Triple>& graphFacts
) {
    // Step 1: Query analysis
    StreamChunk step1;
    step1.type = StreamChunk::Type::Progress;
    step1.content = "分析查询...";
    step1.progress = 0.05f;
    callback(step1);

    // Step 2: Retrieval info
    StreamChunk step2;
    step2.type = StreamChunk::Type::Progress;
    step2.content = "检索相关文档...";
    step2.progress = 0.2f;
    callback(step2);

    StreamChunk step2done;
    step2done.type = StreamChunk::Type::DebugInfo;
    step2done.content = "检索到 " + std::to_string(resultJsons.size()) + " 个相关片段";
    step2done.progress = 0.4f;
    callback(step2done);

    // Step 3: Community info
    StreamChunk step3;
    step3.type = StreamChunk::Type::Progress;
    step3.content = "搜索知识社区...";
    step3.progress = 0.5f;
    callback(step3);

    StreamChunk step3done;
    step3done.type = StreamChunk::Type::DebugInfo;
    step3done.content = "找到 " + std::to_string(communityJsons.size()) + " 个相关社区";
    step3done.progress = 0.6f;
    callback(step3done);

    // Step 4: Generate streaming answer
    generateStreamingAnswer(resultJsons, communityJsons, graphFacts, callback, 0);
}

} // namespace ontology
