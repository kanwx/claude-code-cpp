#include "TestUtils.hpp"
#include <ontology/Persistence.hpp>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <unistd.h>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

static String makeTempDir(const String& base) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    String dir = base + "_" + std::to_string(ms) + "_" + std::to_string(::getpid());
    std::filesystem::create_directories(dir);
    return dir;
}

static void removeDir(const String& dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ============================================================================
// Test 1: WAL append returns valid LSNs
// ============================================================================
void testWalAppendLsn() {
    TEST("WalManager append returns valid LSNs");

    String walDir = makeTempDir("/tmp/ontology_test_wal_lsn");

    WalManager::Config config;
    config.walDirectory = walDir;
    config.enableSync = false;
    config.checkpointInterval = 10000;

    {
        WalManager wal(config);

        Json data;
        data["subject"] = "alice";
        data["predicate"] = "knows";
        data["object"] = "bob";

        int64_t lsn1 = wal.append(WalEntryType::AddTriple, data);
        ASSERT_TRUE(lsn1 > 0);

        int64_t lsn2 = wal.append(WalEntryType::AddClass, {{"id", "Person"}});
        ASSERT_TRUE(lsn2 > lsn1);

        ASSERT_EQ(wal.currentLsn(), lsn2);

        // Destructor flushes the stream
    }

    // Re-open and verify data persisted
    {
        WalManager wal2(config);
        auto entries = wal2.readFromLsn(1);
        ASSERT_EQ(entries.size(), 2u);
        ASSERT_EQ(static_cast<int>(entries[0].type), static_cast<int>(WalEntryType::AddTriple));
        ASSERT_EQ(entries[0].data["subject"].get<String>(), "alice");
    }

    removeDir(walDir);
    PASS();
}

// ============================================================================
// Test 2: WAL checkpoint
// ============================================================================
void testWalCheckpoint() {
    TEST("WalManager checkpoint");

    String walDir = makeTempDir("/tmp/ontology_test_wal_ckpt");

    WalManager::Config config;
    config.walDirectory = walDir;
    config.enableSync = false;
    config.checkpointInterval = 10000;

    WalManager wal(config);

    int64_t lsn1 = wal.append(WalEntryType::AddTriple, {{"s", "a"}, {"p", "b"}, {"o", "c"}});
    int64_t lsn2 = wal.append(WalEntryType::AddClass, {{"id", "X"}});
    int64_t lsn3 = wal.append(WalEntryType::AddIndividual, {{"id", "i1"}});

    // Checkpoint at lsn2
    wal.checkpoint(lsn2);

    // Stats should reflect the checkpoint
    auto stats = wal.getStats();
    ASSERT_EQ(stats["checkpointLsn"].get<int64_t>(), lsn2);

    // Verify checkpoint file was written
    std::ifstream cpFile(walDir + "/checkpoint.txt");
    ASSERT_TRUE(cpFile.is_open());
    int64_t persistedLsn = 0;
    cpFile >> persistedLsn;
    ASSERT_EQ(persistedLsn, lsn2);

    removeDir(walDir);
    PASS();
}

// ============================================================================
// Test 3: Snapshot create and restore
// ============================================================================
void testSnapshotCreateAndRestore() {
    TEST("SnapshotManager create and restore");

    String snapDir = makeTempDir("/tmp/ontology_test_snap");

    SnapshotManager::Config config;
    config.snapshotDirectory = snapDir;
    config.maxSnapshots = 5;

    SnapshotManager mgr(config);

    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);

    Class cls;
    cls.id = "Animal";
    cls.name = "Animal";
    storage->addClass(cls);

    Individual ind;
    ind.id = "cat";
    ind.name = "Cat";
    ind.classId = "Animal";
    storage->addIndividual(ind);

    storage->addTriple(makeTriple("cat", "isA", "Animal"));

    String snapId = mgr.createSnapshot(storage);
    ASSERT_TRUE(!snapId.empty());

    auto snapshots = mgr.listSnapshots();
    ASSERT_EQ(snapshots.size(), 1u);

    // Add more data after snapshot
    Class cls2;
    cls2.id = "Plant";
    cls2.name = "Plant";
    storage->addClass(cls2);
    ASSERT_EQ(storage->classCount(), 2u);

    // Restore snapshot
    bool ok = mgr.restoreSnapshot(snapId, storage);
    ASSERT_TRUE(ok);

    ASSERT_EQ(storage->classCount(), 1u);
    ASSERT_TRUE(storage->getClass("Animal").has_value());
    ASSERT_TRUE(!storage->getClass("Plant").has_value());

    ASSERT_EQ(storage->individualCount(), 1u);
    auto restoredInd = storage->getIndividual("cat");
    ASSERT_TRUE(restoredInd.has_value());
    ASSERT_EQ(restoredInd->name, "Cat");

    removeDir(snapDir);
    PASS();
}

// ============================================================================
// Test 4: WAL batch append
// ============================================================================
void testWalBatchAppend() {
    TEST("WalManager batch append");

    String walDir = makeTempDir("/tmp/ontology_test_wal_batch");

    WalManager::Config config;
    config.walDirectory = walDir;
    config.enableSync = false;
    config.checkpointInterval = 10000;

    {
        WalManager wal(config);

        std::vector<std::pair<WalEntryType, Json>> entries;
        for (int i = 0; i < 5; ++i) {
            entries.push_back({WalEntryType::AddTriple, {{"s", "s" + std::to_string(i)}}});
        }

        auto lsns = wal.appendBatch(entries);
        ASSERT_EQ(lsns.size(), 5u);

        for (size_t i = 1; i < lsns.size(); ++i) {
            ASSERT_TRUE(lsns[i] > lsns[i - 1]);
        }
    }

    // Verify via re-open
    {
        WalManager wal2(config);
        auto read = wal2.readFromLsn(1);
        ASSERT_EQ(read.size(), 5u);
    }

    removeDir(walDir);
    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  WAL Persistence Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    testWalAppendLsn();
    testWalCheckpoint();
    testSnapshotCreateAndRestore();
    testWalBatchAppend();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
