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

    auto* shard = storage.shardManager().getShard(0);
    ASSERT_TRUE(shard != nullptr);
    auto leaderResults = shard->leaderStore()->findBySubject("subj1");
    ASSERT_TRUE(leaderResults.size() == 1);

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

    storage.failReplica(0, "shard0_replica1");

    ASSERT_TRUE(storage.addTriple({"subj2", "pred2", "obj2"}));

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

    storage.addTriple({"subj2", "pred2", "obj2"});

    storage.recoverReplica(0, "shard0_replica1");

    auto* shard = storage.shardManager().getShard(0);
    auto* follower = shard->getFollower(0);
    ASSERT_TRUE(follower != nullptr);
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

    ASSERT_TRUE(vc2.happensBefore(vc1));
    ASSERT_TRUE(!vc1.happensBefore(vc2));
    PASS();
}

void test_vector_clock_concurrent() {
    TEST("VectorClock: concurrent writes detected");
    VectorClock vc1, vc2;
    vc1.increment(0);
    vc2.increment(1);
    ASSERT_TRUE(vc1.isConcurrent(vc2));
    ASSERT_TRUE(vc2.isConcurrent(vc1));
    PASS();
}

void test_vector_clock_merge() {
    TEST("VectorClock: merge takes max of each counter");
    VectorClock vc1, vc2;
    vc1.increment(0);
    vc2.increment(1);
    vc2.increment(1);

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
