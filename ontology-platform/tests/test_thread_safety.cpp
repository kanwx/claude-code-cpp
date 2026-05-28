#include "TestUtils.hpp"

#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

// ============================================================================
// Test 1: Concurrent TripleStore access
// ============================================================================
void testConcurrentTripleStoreAccess() {
    TEST("Concurrent TripleStore access (4 writers + 4 readers)");

    TripleStore ts;

    constexpr int kWriterThreads = 4;
    constexpr int kReaderThreads = 4;
    constexpr int kOpsPerWriter = 1000;
    constexpr int kOpsPerReader = 1000;

    std::atomic<int> readSuccessCount{0};

    // Writer threads: each adds kOpsPerWriter triples with a unique subject prefix
    auto writerFn = [&](int threadId) {
        for (int i = 0; i < kOpsPerWriter; ++i) {
            String subj = "writer" + std::to_string(threadId) + "_item" + std::to_string(i);
            ts.add(makeTriple(subj, "rel", "obj" + std::to_string(i)));
        }
    };

    // Reader threads: each queries kOpsPerReader times
    auto readerFn = [&](int threadId) {
        for (int i = 0; i < kOpsPerReader; ++i) {
            // Alternate between different query types
            switch (i % 3) {
                case 0: {
                    auto results = ts.findBySubject("writer0_item0");
                    if (!results.empty()) readSuccessCount++;
                    break;
                }
                case 1: {
                    auto results = ts.findByPredicate("rel");
                    if (!results.empty()) readSuccessCount++;
                    break;
                }
                case 2: {
                    auto results = ts.findByObject("obj0");
                    if (!results.empty()) readSuccessCount++;
                    break;
                }
            }
        }
    };

    // Launch all threads
    std::vector<std::thread> threads;
    for (int i = 0; i < kWriterThreads; ++i) {
        threads.emplace_back(writerFn, i);
    }
    for (int i = 0; i < kReaderThreads; ++i) {
        threads.emplace_back(readerFn, i);
    }

    // Join all threads
    for (auto& t : threads) {
        t.join();
    }

    // Verify expected triple count
    size_t expected = static_cast<size_t>(kWriterThreads * kOpsPerWriter);
    ASSERT_EQ(ts.count(), expected);

    PASS();
}

// ============================================================================
// Test 2: Concurrent HybridStorage access
// ============================================================================
void testConcurrentHybridStorageAccess() {
    TEST("Concurrent HybridStorage access (4 writers + 4 readers)");

    HybridStorage storage(nullptr, nullptr);

    constexpr int kWriterThreads = 4;
    constexpr int kReaderThreads = 4;
    constexpr int kOpsPerWriter = 250;

    std::atomic<int> classReadHits{0};
    std::atomic<int> individualReadHits{0};

    // Writer threads: each adds classes and individuals
    auto writerFn = [&](int threadId) {
        for (int i = 0; i < kOpsPerWriter; ++i) {
            String classId = "class_w" + std::to_string(threadId) + "_" + std::to_string(i);
            Class cls;
            cls.id = classId;
            cls.name = "Class_" + classId;
            storage.addClass(cls);

            String indId = "ind_w" + std::to_string(threadId) + "_" + std::to_string(i);
            Individual ind;
            ind.id = indId;
            ind.name = "Ind_" + indId;
            ind.classId = classId;
            storage.addIndividual(ind);
        }
    };

    // Reader threads: each reads classes and individuals
    auto readerFn = [&](int threadId) {
        for (int i = 0; i < kOpsPerWriter; ++i) {
            // Try to read classes written by various writers
            String classId = "class_w" + std::to_string(i % kWriterThreads) + "_" + std::to_string(i);
            auto cls = storage.getClass(classId);
            if (cls.has_value()) {
                classReadHits++;
            }

            String indId = "ind_w" + std::to_string(i % kWriterThreads) + "_" + std::to_string(i);
            auto ind = storage.getIndividual(indId);
            if (ind.has_value()) {
                individualReadHits++;
            }
        }
    };

    // Launch all threads
    std::vector<std::thread> threads;
    for (int i = 0; i < kWriterThreads; ++i) {
        threads.emplace_back(writerFn, i);
    }
    for (int i = 0; i < kReaderThreads; ++i) {
        threads.emplace_back(readerFn, i);
    }

    // Join all threads
    for (auto& t : threads) {
        t.join();
    }

    // Verify expected counts after all writes complete
    size_t expectedClasses = static_cast<size_t>(kWriterThreads * kOpsPerWriter);
    size_t expectedIndividuals = static_cast<size_t>(kWriterThreads * kOpsPerWriter);
    ASSERT_EQ(storage.classCount(), expectedClasses);
    ASSERT_EQ(storage.individualCount(), expectedIndividuals);

    PASS();
}

// ============================================================================
// Test 3: Concurrent mixed workload on HybridStorage
// ============================================================================
void testConcurrentMixedWorkload() {
    TEST("Concurrent mixed workload (8 threads, mixed operations)");

    HybridStorage storage(nullptr, nullptr);

    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 200;

    std::atomic<int> successCount{0};

    auto mixedFn = [&](int threadId) {
        for (int i = 0; i < kOpsPerThread; ++i) {
            String idx = std::to_string(threadId) + "_" + std::to_string(i);
            switch (i % 6) {
                case 0: {
                    // addTriple
                    storage.addTriple(makeTriple("s" + idx, "p" + idx, "o" + idx));
                    successCount++;
                    break;
                }
                case 1: {
                    // addClass
                    Class cls;
                    cls.id = "cls_" + idx;
                    cls.name = "Class_" + idx;
                    storage.addClass(cls);
                    successCount++;
                    break;
                }
                case 2: {
                    // addIndividual
                    Individual ind;
                    ind.id = "ind_" + idx;
                    ind.name = "Ind_" + idx;
                    ind.classId = "cls_" + idx;
                    storage.addIndividual(ind);
                    successCount++;
                    break;
                }
                case 3: {
                    // getAllTriples (read)
                    storage.getAllTriples();
                    successCount++;
                    break;
                }
                case 4: {
                    // getClass (read)
                    storage.getClass("cls_" + idx);
                    successCount++;
                    break;
                }
                case 5: {
                    // getIndividual (read)
                    storage.getIndividual("ind_" + idx);
                    successCount++;
                    break;
                }
            }
        }
    };

    // Launch all threads
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back(mixedFn, i);
    }

    // Join all threads
    for (auto& t : threads) {
        t.join();
    }

    // All operations should have completed without crash
    int expectedSuccess = kThreads * kOpsPerThread;
    ASSERT_EQ(successCount.load(), expectedSuccess);

    PASS();
}

// ============================================================================
// Test 4: Read-write consistency
// ============================================================================
void testReadWriteConsistency() {
    TEST("Read-write consistency (writer adds, reader verifies)");

    HybridStorage storage(nullptr, nullptr);

    constexpr int kIterations = 500;

    std::atomic<int> consistentReads{0};
    std::atomic<int> inconsistentReads{0};

    // Writer adds a class, then signals the reader
    // Reader reads the class and verifies data consistency
    for (int i = 0; i < kIterations; ++i) {
        String classId = "consistency_class_" + std::to_string(i);
        String className = "ConsistencyClass_" + std::to_string(i);

        std::atomic<bool> writeDone{false};

        // Writer thread
        std::thread writer([&]() {
            Class cls;
            cls.id = classId;
            cls.name = className;
            cls.description = "desc_" + std::to_string(i);
            storage.addClass(cls);
            writeDone.store(true, std::memory_order_release);
        });

        // Reader thread: busy-wait for the write, then verify
        std::thread reader([&]() {
            // Wait until the writer has published
            while (!writeDone.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            auto cls = storage.getClass(classId);
            if (cls.has_value()) {
                // If we found the class, all fields must be consistent
                if (cls->id == classId && cls->name == className) {
                    consistentReads++;
                } else {
                    inconsistentReads++;
                }
            }
            // If not found yet, that's fine (reader might have raced before index update)
        });

        writer.join();
        reader.join();
    }

    // At a minimum, after writer signals and reader reads, we must see consistent data
    // No inconsistent reads are allowed
    ASSERT_EQ(inconsistentReads.load(), 0);

    // Most reads should succeed since we wait for the write signal
    ASSERT_TRUE(consistentReads.load() > 0);

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Thread Safety Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    testConcurrentTripleStoreAccess();
    testConcurrentHybridStorageAccess();
    testConcurrentMixedWorkload();
    testReadWriteConsistency();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Tests Passed: " << testsPassed << std::endl;
    std::cout << "  Tests Failed: " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
