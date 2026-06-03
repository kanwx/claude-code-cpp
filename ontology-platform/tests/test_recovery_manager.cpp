#include "TestUtils.hpp"
#include <ontology/storage/HybridStorage.hpp>
#include <ontology/bootstrap/RecoveryManager.hpp>
#include <ontology/Persistence.hpp>
#include <filesystem>
#include <unistd.h>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

static String makeTempDir(const String& base) {
    String dir = base + "_" + std::to_string(::getpid());
    std::filesystem::create_directories(dir);
    return dir;
}

static void removeDir(const String& dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void testRecoveryNoSources() {
    TEST("RecoveryManager with no sources");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    RecoveryManager rm(storage, nullptr, nullptr, nullptr);
    auto result = rm.recover();
    ASSERT_TRUE(!result.success);
    ASSERT_TRUE(rm.isReadOnly());
    PASS();
}

void testRecoverySnapshotFallback() {
    TEST("RecoveryManager snapshot fallback");
    String tmpDir = makeTempDir("/tmp/ontology_test_recovery");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);

    WalManager::Config walConfig;
    walConfig.walDirectory = tmpDir + "/wal";
    walConfig.enableSync = false;
    SnapshotManager::Config snapConfig;
    snapConfig.snapshotDirectory = tmpDir + "/snap";

    auto walMgr = std::make_shared<WalManager>(walConfig);
    auto snapMgr = std::make_shared<SnapshotManager>(snapConfig);

    // Add data and create a snapshot
    Class cls;
    cls.id = "TestClass";
    cls.name = "TestClass";
    storage->addClass(cls);
    snapMgr->createSnapshot(storage);

    // Clear storage
    storage->clear();

    // Recovery should find the snapshot
    RecoveryManager rm(storage, nullptr, walMgr, snapMgr);
    auto result = rm.recover();
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(rm.isReadOnly());

    // Cleanup
    removeDir(tmpDir);
    PASS();
}

void testRecoveryWALFallback() {
    TEST("RecoveryManager WAL fallback");
    String tmpDir = makeTempDir("/tmp/ontology_test_wal");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);

    WalManager::Config walConfig;
    walConfig.walDirectory = tmpDir + "/wal";
    walConfig.enableSync = false;
    SnapshotManager::Config snapConfig;
    snapConfig.snapshotDirectory = tmpDir + "/snap";

    // Write WAL entries in a separate scope so the stream is flushed to disk
    {
        auto writeWal = std::make_shared<WalManager>(walConfig);
        Json data;
        data["id"] = "WALClass";
        data["name"] = "WALClass";
        writeWal->append(WalEntryType::AddClass, data);
    }

    // Re-open WAL for recovery
    auto walMgr = std::make_shared<WalManager>(walConfig);
    auto snapMgr = std::make_shared<SnapshotManager>(snapConfig);

    // Recovery with WAL
    RecoveryManager rm(storage, nullptr, walMgr, snapMgr);
    auto result = rm.recover();
    ASSERT_TRUE(result.success);

    // Verify the class was replayed
    auto retrieved = storage->getClass("WALClass");
    ASSERT_TRUE(retrieved.has_value());
    ASSERT_EQ(retrieved->name, "WALClass");

    // Cleanup
    removeDir(tmpDir);
    PASS();
}

int main() {
    testRecoveryNoSources();
    testRecoverySnapshotFallback();
    testRecoveryWALFallback();
    std::cout << "\n=== " << testsPassed << "/" << (testsPassed + testsFailed) << " tests passed ===\n";
    return testsFailed > 0 ? 1 : 0;
}
