#include <ontology/Temporal.hpp>
#include <cmath>
#include <algorithm>
#include <queue>

namespace ontology {

// ============================================================================
// Allen Relation utilities
// ============================================================================

String allenRelationToString(AllenRelation r) {
    switch (r) {
        case AllenRelation::Before: return "Before";
        case AllenRelation::After: return "After";
        case AllenRelation::Meets: return "Meets";
        case AllenRelation::MetBy: return "MetBy";
        case AllenRelation::Overlaps: return "Overlaps";
        case AllenRelation::OverlappedBy: return "OverlappedBy";
        case AllenRelation::During: return "During";
        case AllenRelation::Contains: return "Contains";
        case AllenRelation::Starts: return "Starts";
        case AllenRelation::StartedBy: return "StartedBy";
        case AllenRelation::Finishes: return "Finishes";
        case AllenRelation::FinishedBy: return "FinishedBy";
        case AllenRelation::Equals: return "Equals";
        default: return "Unknown";
    }
}

AllenRelation stringToAllenRelation(const String& s) {
    if (s == "Before" || s == "before") return AllenRelation::Before;
    if (s == "After" || s == "after") return AllenRelation::After;
    if (s == "Meets" || s == "meets") return AllenRelation::Meets;
    if (s == "MetBy" || s == "metBy") return AllenRelation::MetBy;
    if (s == "Overlaps" || s == "overlaps") return AllenRelation::Overlaps;
    if (s == "OverlappedBy" || s == "overlappedBy") return AllenRelation::OverlappedBy;
    if (s == "During" || s == "during") return AllenRelation::During;
    if (s == "Contains" || s == "contains") return AllenRelation::Contains;
    if (s == "Starts" || s == "starts") return AllenRelation::Starts;
    if (s == "StartedBy" || s == "startedBy") return AllenRelation::StartedBy;
    if (s == "Finishes" || s == "finishes") return AllenRelation::Finishes;
    if (s == "FinishedBy" || s == "finishedBy") return AllenRelation::FinishedBy;
    if (s == "Equals" || s == "equals") return AllenRelation::Equals;
    return AllenRelation::Unknown;
}

// ============================================================================
// TemporalInterval
// ============================================================================

bool TemporalInterval::isValid() const {
    if (start.empty() || end.empty()) return false;
    return isoToEpochMs(start) <= isoToEpochMs(end);
}

AllenRelation TemporalInterval::relationTo(const TemporalInterval& other) const {
    int64_t a_s = isoToEpochMs(start);
    int64_t a_e = isoToEpochMs(end);
    int64_t b_s = isoToEpochMs(other.start);
    int64_t b_e = isoToEpochMs(other.end);

    if (a_s == b_s && a_e == b_e) return AllenRelation::Equals;
    if (a_e < b_s) return AllenRelation::Before;
    if (a_s > b_e) return AllenRelation::After;
    if (a_e == b_s) return AllenRelation::Meets;
    if (b_e == a_s) return AllenRelation::MetBy;

    // Overlapping cases
    if (a_s < b_s && a_e > b_s && a_e < b_e) return AllenRelation::Overlaps;
    if (b_s < a_s && b_e > a_s && b_e < a_e) return AllenRelation::OverlappedBy;

    // Containment
    if (a_s > b_s && a_e < b_e) return AllenRelation::During;
    if (a_s < b_s && a_e > b_e) return AllenRelation::Contains;

    // Starting together
    if (a_s == b_s && a_e < b_e) return AllenRelation::Starts;
    if (a_s == b_s && a_e > b_e) return AllenRelation::StartedBy;

    // Ending together
    if (a_e == b_e && a_s > b_s) return AllenRelation::Finishes;
    if (a_e == b_e && a_s < b_s) return AllenRelation::FinishedBy;

    return AllenRelation::Unknown;
}

// ============================================================================
// Allen composition table
// ============================================================================

AllenRelation allenInverse(AllenRelation r) {
    switch (r) {
        case AllenRelation::Before: return AllenRelation::After;
        case AllenRelation::After: return AllenRelation::Before;
        case AllenRelation::Meets: return AllenRelation::MetBy;
        case AllenRelation::MetBy: return AllenRelation::Meets;
        case AllenRelation::Overlaps: return AllenRelation::OverlappedBy;
        case AllenRelation::OverlappedBy: return AllenRelation::Overlaps;
        case AllenRelation::During: return AllenRelation::Contains;
        case AllenRelation::Contains: return AllenRelation::During;
        case AllenRelation::Starts: return AllenRelation::StartedBy;
        case AllenRelation::StartedBy: return AllenRelation::Starts;
        case AllenRelation::Finishes: return AllenRelation::FinishedBy;
        case AllenRelation::FinishedBy: return AllenRelation::Finishes;
        case AllenRelation::Equals: return AllenRelation::Equals;
        default: return AllenRelation::Unknown;
    }
}

// Full 13x13 Allen composition table
std::set<AllenRelation> allenCompose(AllenRelation r1, AllenRelation r2) {
    using AR = AllenRelation;
    using S = std::set<AR>;

    static const S table[13][13] = {
        // Before
        {{AR::Before}, {AR::Before,AR::After,AR::Meets,AR::MetBy,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy,AR::Equals}, {AR::Before}, {AR::Before,AR::Overlaps,AR::Starts}, {AR::Before}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets}, {AR::Before}, {AR::Before,AR::After,AR::Meets,AR::MetBy,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy,AR::Equals}, {AR::Before}, {AR::Before,AR::Overlaps,AR::Starts}, {AR::Before}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets}, {AR::Before}},
        // After
        {{AR::Before,AR::After,AR::Meets,AR::MetBy,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy,AR::Equals}, {AR::After}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy}, {AR::After}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::Contains,AR::MetBy}, {AR::After}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy,AR::During}, {AR::After}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy,AR::During}, {AR::After}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::Contains,AR::MetBy}, {AR::After}, {AR::After}},
        // Meets
        {{AR::Before}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy}, {AR::Before}, {AR::Equals,AR::Starts,AR::StartedBy}, {AR::Before}, {AR::Overlaps,AR::Starts,AR::Equals}, {AR::Before}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy}, {AR::Before}, {AR::Equals,AR::Starts,AR::StartedBy}, {AR::Before}, {AR::Overlaps,AR::Starts,AR::Equals}, {AR::Meets}},
        // MetBy
        {{AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets}, {AR::After}, {AR::Equals,AR::Finishes,AR::FinishedBy}, {AR::After}, {AR::OverlappedBy,AR::Finishes,AR::Equals}, {AR::After}, {AR::During,AR::Starts,AR::Equals,AR::Finishes}, {AR::After}, {AR::During,AR::Starts,AR::Equals,AR::Finishes}, {AR::After}, {AR::Equals,AR::Finishes,AR::FinishedBy}, {AR::After}, {AR::MetBy}},
        // Overlaps
        {{AR::Before}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy}, {AR::Before}, {AR::OverlappedBy,AR::Finishes,AR::FinishedBy}, {AR::Before,AR::Meets}, {AR::Equals,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy}, {AR::Before,AR::Meets}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy}, {AR::Before,AR::Meets}, {AR::OverlappedBy,AR::Finishes,AR::FinishedBy}, {AR::Before}, {AR::Equals,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy}, {AR::Overlaps}},
        // OverlappedBy
        {{AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets}, {AR::After}, {AR::Overlaps,AR::Starts,AR::StartedBy}, {AR::After}, {AR::Equals,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy}, {AR::After,AR::MetBy}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps}, {AR::After,AR::MetBy}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps}, {AR::After,AR::MetBy}, {AR::Equals,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy}, {AR::After,AR::MetBy}, {AR::OverlappedBy}},
        // During
        {{AR::Before}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy,AR::During}, {AR::Before}, {AR::OverlappedBy,AR::Finishes}, {AR::Before,AR::Meets,AR::During}, {AR::OverlappedBy,AR::Finishes,AR::During,AR::Equals,AR::Contains,AR::StartedBy,AR::FinishedBy}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps,AR::Before,AR::Meets}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy,AR::During,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::Equals}, {AR::During,AR::Starts}, {AR::OverlappedBy,AR::Finishes,AR::During,AR::Equals,AR::Contains,AR::StartedBy,AR::FinishedBy}, {AR::During,AR::Finishes}, {AR::OverlappedBy,AR::Finishes,AR::During,AR::Equals,AR::Contains,AR::StartedBy,AR::FinishedBy}, {AR::During}},
        // Contains
        {{AR::Before,AR::After,AR::Meets,AR::MetBy,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy,AR::Equals}, {AR::After}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets}, {AR::After,AR::MetBy}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::After,AR::MetBy,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::OverlappedBy,AR::Equals}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::Contains,AR::StartedBy,AR::FinishedBy,AR::Equals,AR::After,AR::MetBy,AR::OverlappedBy,AR::During}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::Contains,AR::StartedBy}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::Contains,AR::FinishedBy}, {AR::Contains}},
        // Starts
        {{AR::Before}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy}, {AR::Before}, {AR::Equals,AR::Finishes,AR::FinishedBy}, {AR::Before,AR::Meets}, {AR::Equals,AR::Finishes,AR::FinishedBy,AR::OverlappedBy,AR::During,AR::Contains,AR::StartedBy}, {AR::Before,AR::Meets,AR::During,AR::Starts,AR::Equals,AR::Overlaps}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy,AR::During,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::Equals}, {AR::Before,AR::Meets}, {AR::Equals,AR::Finishes,AR::FinishedBy,AR::OverlappedBy,AR::During,AR::Contains,AR::StartedBy}, {AR::Starts}, {AR::Equals,AR::Finishes,AR::FinishedBy,AR::OverlappedBy,AR::During,AR::Contains,AR::StartedBy}, {AR::Starts}},
        // StartedBy
        {{AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets}, {AR::After}, {AR::Overlaps,AR::Starts,AR::StartedBy}, {AR::After,AR::MetBy,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::OverlappedBy,AR::Equals}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::After,AR::MetBy,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::OverlappedBy,AR::Equals}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps,AR::Before,AR::Meets}, {AR::Contains,AR::StartedBy,AR::FinishedBy,AR::Equals,AR::After,AR::MetBy,AR::OverlappedBy,AR::During}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps,AR::Before,AR::Meets}, {AR::StartedBy}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps,AR::Before,AR::Meets}, {AR::Contains,AR::FinishedBy}, {AR::StartedBy}},
        // Finishes
        {{AR::Before}, {AR::After}, {AR::Before,AR::Overlaps,AR::Starts}, {AR::After,AR::MetBy,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::OverlappedBy,AR::Equals}, {AR::Before}, {AR::After,AR::MetBy,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::OverlappedBy,AR::Equals}, {AR::During,AR::Finishes,AR::Equals,AR::OverlappedBy,AR::After,AR::MetBy}, {AR::Contains,AR::StartedBy,AR::FinishedBy,AR::Equals,AR::After,AR::MetBy,AR::OverlappedBy,AR::During}, {AR::Finishes}, {AR::Contains,AR::StartedBy,AR::FinishedBy,AR::Equals,AR::After,AR::MetBy,AR::OverlappedBy,AR::During}, {AR::During,AR::Finishes,AR::Equals,AR::OverlappedBy,AR::After,AR::MetBy}, {AR::FinishedBy}, {AR::Finishes}},
        // FinishedBy
        {{AR::Before,AR::Overlaps,AR::Starts}, {AR::After}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::After,AR::MetBy}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::After,AR::MetBy,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::OverlappedBy,AR::Equals}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps,AR::Before,AR::Meets}, {AR::Contains,AR::FinishedBy}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps,AR::Before,AR::Meets}, {AR::Contains,AR::FinishedBy}, {AR::FinishedBy}, {AR::FinishedBy}, {AR::FinishedBy}},
        // Equals
        {{AR::Before}, {AR::After}, {AR::Meets}, {AR::MetBy}, {AR::Overlaps}, {AR::OverlappedBy}, {AR::During}, {AR::Contains}, {AR::Starts}, {AR::StartedBy}, {AR::Finishes}, {AR::FinishedBy}, {AR::Equals}},
    };

    int i1 = static_cast<int>(r1);
    int i2 = static_cast<int>(r2);
    if (i1 < 0 || i1 > 12 || i2 < 0 || i2 > 12) return {AR::Unknown};
    return table[i1][i2];
}

// ============================================================================
// Path consistency
// ============================================================================

bool isPathConsistent(
    const std::vector<TemporalInterval>& intervals,
    std::unordered_map<std::pair<int,int>, std::set<AllenRelation>, PairHash>& relations)
{
    int n = static_cast<int>(intervals.size());
    if (n <= 1) return true;

    // Initialize relations from interval computations if not already set
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (i == j) {
                relations[{i,j}] = {AllenRelation::Equals};
            } else if (relations.find({i,j}) == relations.end()) {
                relations[{i,j}] = {intervals[i].relationTo(intervals[j])};
            }
        }
    }

    // Path consistency: for all i,j,k: R(i,j) = R(i,j) ∩ (R(i,k) ∘ R(k,j))
    bool changed = true;
    int maxIterations = n * n * n;
    int iter = 0;
    while (changed && iter < maxIterations) {
        changed = false;
        iter++;
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                if (i == j) continue;
                for (int k = 0; k < n; ++k) {
                    if (k == i || k == j) continue;

                    auto& rij = relations[{i,j}];
                    const auto& rik = relations[{i,k}];
                    const auto& rkj = relations[{k,j}];

                    // Compose: R(i,k) ∘ R(k,j)
                    std::set<AllenRelation> composed;
                    for (const auto& r1 : rik) {
                        for (const auto& r2 : rkj) {
                            auto result = allenCompose(r1, r2);
                            composed.insert(result.begin(), result.end());
                        }
                    }

                    // Intersect with existing R(i,j)
                    std::set<AllenRelation> intersection;
                    std::set_intersection(rij.begin(), rij.end(),
                                         composed.begin(), composed.end(),
                                         std::inserter(intersection, intersection.begin()));

                    if (intersection != rij) {
                        rij = intersection;
                        changed = true;
                    }

                    // Empty set means inconsistency
                    if (rij.empty()) return false;
                }
            }
        }
    }

    return true;
}

} // namespace ontology
