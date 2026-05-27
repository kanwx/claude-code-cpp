#include "TestUtils.hpp"
#include <ontology/Temporal.hpp>
#include <ontology/Swrl.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

void test_allen_before() {
    TEST("Allen Before: A ends before B starts");
    TemporalInterval a{"2024-01-01T00:00:00Z", "2024-01-10T00:00:00Z"};
    TemporalInterval b{"2024-02-01T00:00:00Z", "2024-02-10T00:00:00Z"};
    ASSERT_TRUE(a.relationTo(b) == AllenRelation::Before);
    ASSERT_TRUE(b.relationTo(a) == AllenRelation::After);
    PASS();
}

void test_allen_meets() {
    TEST("Allen Meets: A end = B start");
    TemporalInterval a{"2024-01-01T00:00:00Z", "2024-01-10T00:00:00Z"};
    TemporalInterval b{"2024-01-10T00:00:00Z", "2024-01-20T00:00:00Z"};
    ASSERT_TRUE(a.relationTo(b) == AllenRelation::Meets);
    ASSERT_TRUE(b.relationTo(a) == AllenRelation::MetBy);
    PASS();
}

void test_allen_overlaps() {
    TEST("Allen Overlaps: A starts before B, overlaps");
    TemporalInterval a{"2024-01-01T00:00:00Z", "2024-01-15T00:00:00Z"};
    TemporalInterval b{"2024-01-10T00:00:00Z", "2024-01-20T00:00:00Z"};
    ASSERT_TRUE(a.relationTo(b) == AllenRelation::Overlaps);
    ASSERT_TRUE(b.relationTo(a) == AllenRelation::OverlappedBy);
    PASS();
}

void test_allen_during() {
    TEST("Allen During: A fully within B");
    TemporalInterval a{"2024-01-05T00:00:00Z", "2024-01-10T00:00:00Z"};
    TemporalInterval b{"2024-01-01T00:00:00Z", "2024-01-20T00:00:00Z"};
    ASSERT_TRUE(a.relationTo(b) == AllenRelation::During);
    ASSERT_TRUE(b.relationTo(a) == AllenRelation::Contains);
    PASS();
}

void test_allen_starts() {
    TEST("Allen Starts: A and B start together, A ends first");
    TemporalInterval a{"2024-01-01T00:00:00Z", "2024-01-10T00:00:00Z"};
    TemporalInterval b{"2024-01-01T00:00:00Z", "2024-01-20T00:00:00Z"};
    ASSERT_TRUE(a.relationTo(b) == AllenRelation::Starts);
    ASSERT_TRUE(b.relationTo(a) == AllenRelation::StartedBy);
    PASS();
}

void test_allen_finishes() {
    TEST("Allen Finishes: A and B end together, A starts later");
    TemporalInterval a{"2024-01-10T00:00:00Z", "2024-01-20T00:00:00Z"};
    TemporalInterval b{"2024-01-01T00:00:00Z", "2024-01-20T00:00:00Z"};
    ASSERT_TRUE(a.relationTo(b) == AllenRelation::Finishes);
    ASSERT_TRUE(b.relationTo(a) == AllenRelation::FinishedBy);
    PASS();
}

void test_allen_equals() {
    TEST("Allen Equals: same interval");
    TemporalInterval a{"2024-01-01T00:00:00Z", "2024-01-20T00:00:00Z"};
    TemporalInterval b{"2024-01-01T00:00:00Z", "2024-01-20T00:00:00Z"};
    ASSERT_TRUE(a.relationTo(b) == AllenRelation::Equals);
    PASS();
}

void test_allen_inverse() {
    TEST("Allen inverse relations are correct");
    ASSERT_TRUE(allenInverse(AllenRelation::Before) == AllenRelation::After);
    ASSERT_TRUE(allenInverse(AllenRelation::Meets) == AllenRelation::MetBy);
    ASSERT_TRUE(allenInverse(AllenRelation::Overlaps) == AllenRelation::OverlappedBy);
    ASSERT_TRUE(allenInverse(AllenRelation::During) == AllenRelation::Contains);
    ASSERT_TRUE(allenInverse(AllenRelation::Starts) == AllenRelation::StartedBy);
    ASSERT_TRUE(allenInverse(AllenRelation::Finishes) == AllenRelation::FinishedBy);
    ASSERT_TRUE(allenInverse(AllenRelation::Equals) == AllenRelation::Equals);
    PASS();
}

void test_allen_composition() {
    TEST("Allen composition: Before ∘ Before = Before");
    auto result = allenCompose(AllenRelation::Before, AllenRelation::Before);
    ASSERT_TRUE(result.count(AllenRelation::Before) > 0);
    ASSERT_TRUE(result.size() == 1);
    PASS();
}

void test_allen_composition_meets_before() {
    TEST("Allen composition: Meets ∘ Before = Before");
    auto result = allenCompose(AllenRelation::Meets, AllenRelation::Before);
    ASSERT_TRUE(result.count(AllenRelation::Before) > 0);
    PASS();
}

void test_path_consistency_consistent() {
    TEST("Path consistency: consistent intervals");
    std::vector<TemporalInterval> intervals = {
        {"2024-01-01T00:00:00Z", "2024-01-10T00:00:00Z"},  // 0
        {"2024-01-20T00:00:00Z", "2024-01-30T00:00:00Z"},  // 1
        {"2024-02-01T00:00:00Z", "2024-02-10T00:00:00Z"}   // 2
    };
    std::unordered_map<std::pair<int,int>, std::set<AllenRelation>, PairHash> relations;
    ASSERT_TRUE(isPathConsistent(intervals, relations));
    PASS();
}

void test_path_consistency_inconsistent() {
    TEST("Path consistency: detects inconsistent constraints");
    // A before B, B before C, but we force A overlaps C (contradicts composition)
    std::vector<TemporalInterval> intervals = {
        {"2024-01-01T00:00:00Z", "2024-01-10T00:00:00Z"},  // 0
        {"2024-01-20T00:00:00Z", "2024-01-30T00:00:00Z"},  // 1
        {"2024-02-01T00:00:00Z", "2024-02-10T00:00:00Z"}   // 2
    };
    std::unordered_map<std::pair<int,int>, std::set<AllenRelation>, PairHash> relations;
    // Force A overlaps C (which is impossible given A before B and B before C)
    relations[{0,2}] = {AllenRelation::Overlaps};
    ASSERT_TRUE(!isPathConsistent(intervals, relations));
    PASS();
}

void test_temporal_swrl_builtin() {
    TEST("SWRL temporal:before built-in");
    ASSERT_TRUE(SwrlBuiltIns::temporalBefore("2024-01-10T00:00:00Z", "2024-02-01T00:00:00Z"));
    ASSERT_TRUE(!SwrlBuiltIns::temporalBefore("2024-02-01T00:00:00Z", "2024-01-10T00:00:00Z"));
    PASS();
}

void test_temporal_swrl_during() {
    TEST("SWRL temporal:during built-in");
    ASSERT_TRUE(SwrlBuiltIns::temporalDuring(
        "2024-01-05T00:00:00Z", "2024-01-10T00:00:00Z",
        "2024-01-01T00:00:00Z", "2024-01-20T00:00:00Z"));
    ASSERT_TRUE(!SwrlBuiltIns::temporalDuring(
        "2024-01-01T00:00:00Z", "2024-01-20T00:00:00Z",
        "2024-01-05T00:00:00Z", "2024-01-10T00:00:00Z"));
    PASS();
}

int main() {
    test_allen_before();
    test_allen_meets();
    test_allen_overlaps();
    test_allen_during();
    test_allen_starts();
    test_allen_finishes();
    test_allen_equals();
    test_allen_inverse();
    test_allen_composition();
    test_allen_composition_meets_before();
    test_path_consistency_consistent();
    test_path_consistency_inconsistent();
    test_temporal_swrl_builtin();
    test_temporal_swrl_during();

    std::cout << "\nTemporal tests: " << testsPassed << " passed, "
              << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
