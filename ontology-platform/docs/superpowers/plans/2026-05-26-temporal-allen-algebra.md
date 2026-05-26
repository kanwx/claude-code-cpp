# Temporal Reasoning (Allen Algebra) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add temporal reasoning with Allen interval algebra, path consistency checking, temporal SWRL built-ins, and bitemporal triple support.

**Architecture:** Extend `Temporal.hpp` with `AllenRelation` enum, `TemporalInterval` struct, composition table, and path consistency. Add temporal built-ins to `SwrlBuiltIns`. Extend `Triple` with bitemporal fields.

**Tech Stack:** C++17, existing `Temporal.hpp`, `Swrl.hpp`, `Core.hpp`.

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `include/ontology/Temporal.hpp` | Modify | Add AllenRelation, TemporalInterval, composition table, path consistency |
| `src/inference/Temporal.cpp` | Create | Implement Allen relation computation, composition, path consistency |
| `include/ontology/Swrl.hpp` | Modify | Add temporal built-in declarations |
| `src/swrl/Swrl.cpp` | Modify | Implement temporal built-in evaluation |
| `include/ontology/Core.hpp` | Modify | Add bitemporal fields to Triple struct |
| `tests/test_temporal.cpp` | Create | Allen relation tests, consistency tests, temporal SWRL tests |
| `tests/CMakeLists.txt` | Modify | Add test target |

---

### Task 1: Extend Triple with Bitemporal Fields

**Files:**
- Modify: `include/ontology/Core.hpp`

- [ ] **Step 1: Add bitemporal fields to Triple struct**

Find the `Triple` struct definition in `include/ontology/Core.hpp`. It should have fields like `subject`, `predicate`, `object`, `confidence`. Add bitemporal fields after the existing fields:

```cpp
    // Bitemporal: when the fact was true in the real world
    String validFrom;
    String validTo;
    // Bitemporal: when the fact was recorded in the system
    String recordedAt;
```

Also add a convenience constructor update. Find the existing constructors and add or modify so that bitemporal fields default to empty:

If there's a constructor like:
```cpp
Triple(String s, String p, String o, String src = "", float conf = 1.0f, String vf = "", String vt = "")
```

Add the `recordedAt` parameter:
```cpp
Triple(String s, String p, String o, String src = "", float conf = 1.0f,
       String vf = "", String vt = "", String recAt = "")
    : subject(s), predicate(p), object(o), source(src), confidence(conf),
      validFrom(vf), validTo(vt), recordedAt(recAt) {}
```

If there's an aggregate initializer constructor (no explicit constructor), the new fields will be default-initialized to empty strings, which is fine.

- [ ] **Step 2: Commit**

```bash
git add include/ontology/Core.hpp
git commit -m "feat(core): add bitemporal fields (validFrom, validTo, recordedAt) to Triple struct"
```

---

### Task 2: Allen Relation Types and TemporalInterval

**Files:**
- Modify: `include/ontology/Temporal.hpp`
- Create: `src/inference/Temporal.cpp`

- [ ] **Step 1: Add Allen relation and TemporalInterval to Temporal.hpp**

Add before the closing `} // namespace ontology` in `include/ontology/Temporal.hpp`:

```cpp
// ============================================================================
// Allen Interval Algebra
// ============================================================================

/// Allen interval relation (13 relations + Unknown)
enum class AllenRelation {
    Before,         // A ends before B starts
    After,          // A starts after B ends (inverse of Before)
    Meets,          // A end = B start
    MetBy,          // B end = A start (inverse of Meets)
    Overlaps,       // A starts before B, overlaps
    OverlappedBy,   // B starts before A, overlaps (inverse of Overlaps)
    During,         // A fully within B
    Contains,       // B fully within A (inverse of During)
    Starts,         // A and B start together, A ends first
    StartedBy,      // A and B start together, B ends first (inverse of Starts)
    Finishes,       // A and B end together, A starts later
    FinishedBy,     // A and B end together, B starts later (inverse of Finishes)
    Equals,         // Same interval
    Unknown         // Cannot determine
};

/// Convert AllenRelation to string
String allenRelationToString(AllenRelation r);

/// Convert string to AllenRelation
AllenRelation stringToAllenRelation(const String& s);

/// Temporal interval with ISO 8601 timestamps
struct TemporalInterval {
    String start;  // ISO 8601 start time
    String end;    // ISO 8601 end time

    /// Compute Allen relation between this interval and another
    AllenRelation relationTo(const TemporalInterval& other) const;

    /// Check if interval is valid (start <= end)
    bool isValid() const;
};

/// Allen algebra composition: given R1(A,B) and R2(B,C), infer possible R3(A,C)
std::set<AllenRelation> allenCompose(AllenRelation r1, AllenRelation r2);

/// Allen algebra inverse: given R, return R^{-1}
AllenRelation allenInverse(AllenRelation r);

/// Path consistency: check if a set of interval relations is consistent
/// intervals: list of intervals, relations: map from (i,j) to set of AllenRelations
bool isPathConsistent(
    const std::vector<TemporalInterval>& intervals,
    std::unordered_map<std::pair<int,int>, std::set<AllenRelation>>& relations);
```

Also add the missing include at the top:
```cpp
#include <set>
```

- [ ] **Step 2: Create Temporal.cpp with Allen relation implementation**

Create `src/inference/Temporal.cpp`:

```cpp
#include <ontology/Temporal.hpp>
#include <cmath>
#include <algorithm>
#include <queue>

namespace ontology {

// ============================================================================
// Existing Temporal helpers (moved from inline if needed)
// ============================================================================

// These are already declared in Temporal.hpp — if they were previously
// implemented inline or elsewhere, they stay there. This file adds the
// Allen algebra implementations only.

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

// Composition table: allenCompose(Before, Before) = {Before}
// Full 13x13 table — returns set of possible relations
std::set<AllenRelation> allenCompose(AllenRelation r1, AllenRelation r2) {
    // Index: 0=Before, 1=After, 2=Meets, 3=MetBy, 4=Overlaps, 5=OverlappedBy,
    //        6=During, 7=Contains, 8=Starts, 9=StartedBy, 10=Finishes, 11=FinishedBy, 12=Equals

    using AR = AllenRelation;
    using S = std::set<AR>;

    // Compact composition table — row r1, column r2
    // Each entry is the set of possible R3 where R1(A,B) ∘ R2(B,C) → R3(A,C)
    static const S table[13][13] = {
        // Before  After   Meets   MetBy   Overlaps OverlappedBy During Contains Starts StartedBy Finishes FinishedBy Equals
        {{AR::Before}, {AR::Before,AR::After,AR::Meets,AR::MetBy,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy,AR::Equals}, {AR::Before}, {AR::Before,AR::Overlaps,AR::Starts}, {AR::Before}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets}, {AR::Before}, {AR::Before,AR::After,AR::Meets,AR::MetBy,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy,AR::Equals}, {AR::Before}, {AR::Before,AR::Overlaps,AR::Starts}, {AR::Before}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets}, {AR::Before}}, // Before
        {{AR::Before,AR::After,AR::Meets,AR::MetBy,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy,AR::Equals}, {AR::After}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy}, {AR::After}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::Contains,AR::MetBy}, {AR::After}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy,AR::During}, {AR::After}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy,AR::During}, {AR::After}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::Contains,AR::MetBy}, {AR::After}, {AR::After}}, // After
        {{AR::Before}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy}, {AR::Before}, {AR::Equals,AR::Starts,AR::StartedBy}, {AR::Before}, {AR::Overlaps,AR::Starts,AR::Equals}, {AR::Before}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy}, {AR::Before}, {AR::Equals,AR::Starts,AR::StartedBy}, {AR::Before}, {AR::Overlaps,AR::Starts,AR::Equals}, {AR::Meets}}, // Meets
        {{AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets}, {AR::After}, {AR::Equals,AR::Finishes,AR::FinishedBy}, {AR::After}, {AR::OverlappedBy,AR::Finishes,AR::Equals}, {AR::After}, {AR::During,AR::Starts,AR::Equals,AR::Finishes}, {AR::After}, {AR::During,AR::Starts,AR::Equals,AR::Finishes}, {AR::After}, {AR::Equals,AR::Finishes,AR::FinishedBy}, {AR::After}, {AR::MetBy}}, // MetBy
        {{AR::Before}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy}, {AR::Before}, {AR::OverlappedBy,AR::Finishes,AR::FinishedBy}, {AR::Before,AR::Meets}, {AR::Equals,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy}, {AR::Before,AR::Meets}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy}, {AR::Before,AR::Meets}, {AR::OverlappedBy,AR::Finishes,AR::FinishedBy}, {AR::Before}, {AR::Equals,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy}, {AR::Overlaps}}, // Overlaps
        {{AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets}, {AR::After}, {AR::Overlaps,AR::Starts,AR::StartedBy}, {AR::After}, {AR::Equals,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy}, {AR::After,AR::MetBy}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps}, {AR::After,AR::MetBy}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps}, {AR::After,AR::MetBy}, {AR::Equals,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy}, {AR::After,AR::MetBy}, {AR::OverlappedBy}}, // OverlappedBy
        {{AR::Before}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy,AR::During}, {AR::Before}, {AR::OverlappedBy,AR::Finishes}, {AR::Before,AR::Meets,AR::During}, {AR::OverlappedBy,AR::Finishes,AR::During,AR::Equals,AR::Contains,AR::StartedBy,AR::FinishedBy}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps,AR::Before,AR::Meets}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy,AR::During,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::Equals}, {AR::During,AR::Starts}, {AR::OverlappedBy,AR::Finishes,AR::During,AR::Equals,AR::Contains,AR::StartedBy,AR::FinishedBy}, {AR::During,AR::Finishes}, {AR::OverlappedBy,AR::Finishes,AR::During,AR::Equals,AR::Contains,AR::StartedBy,AR::FinishedBy}, {AR::During}}, // During
        {{AR::Before,AR::After,AR::Meets,AR::MetBy,AR::Overlaps,AR::OverlappedBy,AR::During,AR::Contains,AR::Starts,AR::StartedBy,AR::Finishes,AR::FinishedBy,AR::Equals}, {AR::After}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets}, {AR::After,AR::MetBy}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::After,AR::MetBy,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::OverlappedBy,AR::Equals}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::Contains,AR::StartedBy,AR::FinishedBy,AR::Equals,AR::After,AR::MetBy,AR::OverlappedBy,AR::During}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::Contains,AR::StartedBy}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::Contains,AR::FinishedBy}, {AR::Contains}}, // Contains
        {{AR::Before}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy}, {AR::Before}, {AR::Equals,AR::Finishes,AR::FinishedBy}, {AR::Before,AR::Meets}, {AR::Equals,AR::Finishes,AR::FinishedBy,AR::OverlappedBy,AR::During,AR::Contains,AR::StartedBy}, {AR::Before,AR::Meets,AR::During,AR::Starts,AR::Equals,AR::Overlaps}, {AR::After,AR::OverlappedBy,AR::FinishedBy,AR::MetBy,AR::During,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::Equals}, {AR::Before,AR::Meets}, {AR::Equals,AR::Finishes,AR::FinishedBy,AR::OverlappedBy,AR::During,AR::Contains,AR::StartedBy}, {AR::Starts}, {AR::Equals,AR::Finishes,AR::FinishedBy,AR::OverlappedBy,AR::During,AR::Contains,AR::StartedBy}, {AR::Starts}}, // Starts
        {{AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets}, {AR::After}, {AR::Overlaps,AR::Starts,AR::StartedBy}, {AR::After,AR::MetBy,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::OverlappedBy,AR::Equals}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::After,AR::MetBy,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::OverlappedBy,AR::Equals}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps,AR::Before,AR::Meets}, {AR::Contains,AR::StartedBy,AR::FinishedBy,AR::Equals,AR::After,AR::MetBy,AR::OverlappedBy,AR::During}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps,AR::Before,AR::Meets}, {AR::StartedBy}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps,AR::Before,AR::Meets}, {AR::Contains,AR::FinishedBy}, {AR::StartedBy}}, // StartedBy
        {{AR::Before}, {AR::After}, {AR::Before,AR::Overlaps,AR::Starts}, {AR::After,AR::MetBy,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::OverlappedBy,AR::Equals}, {AR::Before}, {AR::After,AR::MetBy,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::OverlappedBy,AR::Equals}, {AR::During,AR::Finishes,AR::Equals,AR::OverlappedBy,AR::After,AR::MetBy}, {AR::Contains,AR::StartedBy,AR::FinishedBy,AR::Equals,AR::After,AR::MetBy,AR::OverlappedBy,AR::During}, {AR::Finishes}, {AR::Contains,AR::StartedBy,AR::FinishedBy,AR::Equals,AR::After,AR::MetBy,AR::OverlappedBy,AR::During}, {AR::During,AR::Finishes,AR::Equals,AR::OverlappedBy,AR::After,AR::MetBy}, {AR::FinishedBy}, {AR::Finishes}}, // Finishes
        {{AR::Before,AR::Overlaps,AR::Starts}, {AR::After}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::After,AR::MetBy}, {AR::Before,AR::Overlaps,AR::Starts,AR::During,AR::Meets,AR::Contains,AR::StartedBy,AR::Equals}, {AR::After,AR::MetBy,AR::Contains,AR::StartedBy,AR::FinishedBy,AR::OverlappedBy,AR::Equals}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps,AR::Before,AR::Meets}, {AR::Contains,AR::FinishedBy}, {AR::During,AR::Starts,AR::Equals,AR::Overlaps,AR::Before,AR::Meets}, {AR::Contains,AR::FinishedBy}, {AR::FinishedBy}, {AR::FinishedBy}, {AR::FinishedBy}}, // FinishedBy
        {{AR::Before}, {AR::After}, {AR::Meets}, {AR::MetBy}, {AR::Overlaps}, {AR::OverlappedBy}, {AR::During}, {AR::Contains}, {AR::Starts}, {AR::StartedBy}, {AR::Finishes}, {AR::FinishedBy}, {AR::Equals}}, // Equals
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
    std::unordered_map<std::pair<int,int>, std::set<AllenRelation>>& relations)
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
```

- [ ] **Step 3: Add Temporal.cpp to CMakeLists**

Find the `ontology_core` library target in the main `CMakeLists.txt`. Add `src/inference/Temporal.cpp` to the source list. Look for a pattern like:

```cmake
src/inference/HybridReasoner.cpp
```

Add after the existing inference source files:
```cmake
src/inference/Temporal.cpp
```

- [ ] **Step 4: Commit**

```bash
git add include/ontology/Temporal.hpp src/inference/Temporal.cpp CMakeLists.txt
git commit -m "feat(temporal): add Allen interval algebra with 13 relations, composition table, and path consistency"
```

---

### Task 3: Temporal SWRL Built-ins

**Files:**
- Modify: `include/ontology/Swrl.hpp`
- Modify: `src/swrl/Swrl.cpp`

- [ ] **Step 1: Add temporal built-in declarations to SwrlBuiltIns**

In `include/ontology/Swrl.hpp`, add to the `SwrlBuiltIns` class public section (before `execute`):

```cpp
    /// Temporal functions (Allen algebra)
    static bool temporalBefore(const String& t1, const String& t2);
    static bool temporalAfter(const String& t1, const String& t2);
    static bool temporalOverlaps(const String& t1_start, const String& t1_end,
                                  const String& t2_start, const String& t2_end);
    static bool temporalDuring(const String& t1_start, const String& t1_end,
                                const String& t2_start, const String& t2_end);
    static bool temporalContains(const String& t1_start, const String& t1_end,
                                  const String& t2_start, const String& t2_end);
```

- [ ] **Step 2: Implement temporal built-ins in Swrl.cpp**

Add at the end of `src/swrl/Swrl.cpp` before the closing namespace brace:

```cpp
// ============================================================================
// Temporal built-ins
// ============================================================================

bool SwrlBuiltIns::temporalBefore(const String& t1, const String& t2) {
    int64_t e1 = isoToEpochMs(t1);
    int64_t s2 = isoToEpochMs(t2);
    return e1 < s2;
}

bool SwrlBuiltIns::temporalAfter(const String& t1, const String& t2) {
    int64_t s1 = isoToEpochMs(t1);
    int64_t e2 = isoToEpochMs(t2);
    return s1 > e2;
}

bool SwrlBuiltIns::temporalOverlaps(const String& t1_start, const String& t1_end,
                                     const String& t2_start, const String& t2_end) {
    TemporalInterval i1{t1_start, t1_end};
    TemporalInterval i2{t2_start, t2_end};
    auto r = i1.relationTo(i2);
    return r == AllenRelation::Overlaps || r == AllenRelation::OverlappedBy;
}

bool SwrlBuiltIns::temporalDuring(const String& t1_start, const String& t1_end,
                                   const String& t2_start, const String& t2_end) {
    TemporalInterval i1{t1_start, t1_end};
    TemporalInterval i2{t2_start, t2_end};
    return i1.relationTo(i2) == AllenRelation::During;
}

bool SwrlBuiltIns::temporalContains(const String& t1_start, const String& t1_end,
                                     const String& t2_start, const String& t2_end) {
    TemporalInterval i1{t1_start, t1_end};
    TemporalInterval i2{t2_start, t2_end};
    return i1.relationTo(i2) == AllenRelation::Contains;
}
```

Add the include at the top of `src/swrl/Swrl.cpp`:
```cpp
#include <ontology/Temporal.hpp>
```

Also extend the `execute` method to handle temporal built-in names. Find the `execute` method and add cases for the temporal built-ins. Look for the switch or if-else chain in `execute()` and add:

```cpp
    if (name == "temporal:before" || name == "temporalBefore") {
        return temporalBefore(args[0], args[1]);
    }
    if (name == "temporal:after" || name == "temporalAfter") {
        return temporalAfter(args[0], args[1]);
    }
    if (name == "temporal:overlaps" || name == "temporalOverlaps") {
        if (args.size() >= 4) return temporalOverlaps(args[0], args[1], args[2], args[3]);
        return false;
    }
    if (name == "temporal:during" || name == "temporalDuring") {
        if (args.size() >= 4) return temporalDuring(args[0], args[1], args[2], args[3]);
        return false;
    }
    if (name == "temporal:contains" || name == "temporalContains") {
        if (args.size() >= 4) return temporalContains(args[0], args[1], args[2], args[3]);
        return false;
    }
```

- [ ] **Step 3: Commit**

```bash
git add include/ontology/Swrl.hpp src/swrl/Swrl.cpp
git commit -m "feat(swrl): add temporal built-ins (before, after, overlaps, during, contains) using Allen algebra"
```

---

### Task 4: Temporal Reasoning Unit Tests

**Files:**
- Create: `tests/test_temporal.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create test file**

```cpp
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
    std::unordered_map<std::pair<int,int>, std::set<AllenRelation>> relations;
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
    std::unordered_map<std::pair<int,int>, std::set<AllenRelation>> relations;
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
```

- [ ] **Step 2: Add test target to CMakeLists.txt**

Append to `tests/CMakeLists.txt`:

```cmake
# Unit test: Temporal Reasoning (Allen Algebra)
add_executable(test_temporal test_temporal.cpp)
target_link_libraries(test_temporal PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME temporal COMMAND test_temporal)
```

- [ ] **Step 3: Build and test**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && cmake .. -DBUILD_TESTS=ON && make test_temporal && ./tests/test_temporal`

Expected: All 14 tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/test_temporal.cpp tests/CMakeLists.txt
git commit -m "test(temporal): add Allen algebra unit tests — relations, composition, path consistency, SWRL built-ins"
```

---

### Task 5: Full Build Verification

**Files:** None (verification only)

- [ ] **Step 1: Full build**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && cmake .. -DBUILD_TESTS=ON && make -j$(sysctl -n hw.ncpu)`

Expected: Build succeeds.

- [ ] **Step 2: Run all tests**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && ctest --output-on-failure`

Expected: All tests pass.
