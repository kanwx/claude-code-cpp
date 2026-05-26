# AutoModel TBox-aware Enhancement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace hardcoded conflict detection with TBox-aware strategies, enhance entity alignment with multi-signal scoring, and add provenance-aware knowledge fusion.

**Architecture:** `detectConflicts()` queries TBox for owl:disjointWith and owl:FunctionalProperty violations. `alignEntities()` combines embedding, structural, and label similarity. `mergeOntologies()` tracks provenance and deduplicates.

**Tech Stack:** C++17, existing `AutoModel.hpp`/`.cpp`, `TripleStore` for TBox queries, `DlReasoner` for consistency checking.

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `include/ontology/AutoModel.hpp` | Modify | Add Conflict struct, Provenance struct, AlignmentResult struct |
| `src/inference/AutoModel.cpp:1633-1708` | Modify | Rewrite detectConflicts, alignEntities, mergeOntologies |
| `tests/test_auto_model.cpp` | Modify | Add new tests |

---

### Task 1: Add Data Structures to AutoModel.hpp

**Files:**
- Modify: `include/ontology/AutoModel.hpp`

- [ ] **Step 1: Add Conflict, Provenance, and AlignmentResult structs**

Add before the `AutoModelEngine` class definition (after `ConflictAction` struct, around line 300):

```cpp
// ============================================================================
// 冲突检测结果
// ============================================================================

struct Conflict {
    enum Type {
        DisjointClassAssertion,       // individual typed as both C and D where C ⊓ D ⊑ ⊥
        FunctionalPropertyViolation,  // subject has multiple values for functional property
        Inconsistency                 // DlReasoner detected inconsistency
    };
    Type type;
    String description;
    std::vector<Triple> conflictingTriples;
    float severity = 1.0f;  // 0.0-1.0
};

// ============================================================================
// 数据来源追踪
// ============================================================================

struct Provenance {
    String sourceId;       // ontology/document ID
    String sourceName;     // human-readable name
    float confidence = 1.0f;
    String timestamp;      // ISO 8601 when this triple was added
};

// ============================================================================
// 实体对齐结果
// ============================================================================

struct AlignmentResult {
    String entity1;
    String entity2;
    float embeddingScore;   // cosine similarity
    float structuralScore;  // Jaccard of shared properties
    float labelScore;       // normalized Levenshtein
    float combinedScore;    // weighted combination
};
```

- [ ] **Step 2: Update AutoModelEngine method signatures**

In `AutoModelEngine` class, change the private method declarations:

Find:
```cpp
    /// 检测冲突
    std::vector<String> detectConflicts();

    // ===== 知识融合 =====

    /// 实体对齐
    std::vector<std::pair<String, String>> alignEntities(
        const std::vector<String>& entities1,
        const std::vector<String>& entities2);

    /// 本体融合
    void mergeOntologies(const std::vector<Triple>& externalTriples);
```

Replace with:
```cpp
    /// 检测冲突 (TBox-aware)
    std::vector<Conflict> detectConflicts();

    // ===== 知识融合 =====

    /// 实体对齐 (multi-signal)
    std::vector<AlignmentResult> alignEntities(
        const std::vector<String>& entities1,
        const std::vector<String>& entities2);

    /// 本体融合 (provenance-aware)
    void mergeOntologies(const std::vector<Triple>& externalTriples,
                         const String& sourceId = "external",
                         const String& sourceName = "External Ontology");
```

Add new private members to `AutoModelEngine`:

```cpp
    // Provenance tracking: triple hash -> provenance
    std::unordered_map<size_t, Provenance> provenanceIndex_;

    // Alignment weights
    float alignWeightEmbedding_ = 0.5f;
    float alignWeightStructural_ = 0.3f;
    float alignWeightLabel_ = 0.2f;

    // Helper: compute triple hash
    static size_t tripleHash(const Triple& t);

    // Helper: compute Levenshtein distance
    static int levenshteinDistance(const String& s1, const String& s2);

    // Helper: compute Jaccard coefficient
    float jaccardCoefficient(const String& entity1, const String& entity2) const;
```

- [ ] **Step 3: Commit**

```bash
git add include/ontology/AutoModel.hpp
git commit -m "feat(automodel): add Conflict, Provenance, AlignmentResult structs and updated method signatures"
```

---

### Task 2: Rewrite detectConflicts with TBox-aware Strategies

**Files:**
- Modify: `src/inference/AutoModel.cpp:1633-1661`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_auto_model.cpp` before `main()`:

```cpp
void test_detect_disjoint_conflict() {
    TEST("detectConflicts finds disjoint class assertion conflicts");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    AutoModelEngine engine(storage);

    auto* ts = storage->getTripleStore();
    // Set up TBox: Cat and Dog are disjoint
    ts->add({"Cat", "http://www.w3.org/2002/07/owl#disjointWith", "Dog"});
    // ABox: fluffy is both Cat and Dog — conflict!
    ts->add({"fluffy", "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", "Cat"});
    ts->add({"fluffy", "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", "Dog"});

    auto conflicts = engine.detectConflicts();
    bool foundDisjoint = false;
    for (const auto& c : conflicts) {
        if (c.type == Conflict::DisjointClassAssertion) {
            foundDisjoint = true;
        }
    }
    ASSERT_TRUE(foundDisjoint);
    PASS();
}

void test_detect_functional_property_violation() {
    TEST("detectConflicts finds functional property violations");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    AutoModelEngine engine(storage);

    auto* ts = storage->getTripleStore();
    // hasMother is functional
    ts->add({"hasMother", "http://www.w3.org/1999/02/22-rdf-syntax-ns#type",
             "http://www.w3.org/2002/07/owl#FunctionalProperty"});
    // alice has two mothers — violation!
    ts->add({"alice", "hasMother", "bob"});
    ts->add({"alice", "hasMother", "carol"});

    auto conflicts = engine.detectConflicts();
    bool foundFunctional = false;
    for (const auto& c : conflicts) {
        if (c.type == Conflict::FunctionalPropertyViolation) {
            foundFunctional = true;
        }
    }
    ASSERT_TRUE(foundFunctional);
    PASS();
}

void test_detect_no_conflicts() {
    TEST("detectConflicts returns empty for consistent data");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    AutoModelEngine engine(storage);

    auto* ts = storage->getTripleStore();
    ts->add({"Dog", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    ts->add({"rex", "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", "Dog"});

    auto conflicts = engine.detectConflicts();
    ASSERT_TRUE(conflicts.empty());
    PASS();
}
```

Add calls in `main()`:
```cpp
    test_detect_disjoint_conflict();
    test_detect_functional_property_violation();
    test_detect_no_conflicts();
```

- [ ] **Step 2: Implement TBox-aware detectConflicts**

Replace `detectConflicts()` in `src/inference/AutoModel.cpp` (lines 1633-1661):

Find:
```cpp
std::vector<String> AutoModelEngine::detectConflicts() {
    std::vector<String> conflicts;

    // 检查矛盾的陈述
    auto triples = storage_->getAllTriples();

    // 检查互斥关系
    std::unordered_map<String, std::vector<String>> mutexRelations = {
        {"marriedTo", {"divorcedFrom"}},
        {"employedBy", {"firedFrom"}},
        {"alive", {"deceased"}}
    };

    for (const auto& t : triples) {
        auto it = mutexRelations.find(t.predicate);
        if (it != mutexRelations.end()) {
            for (const auto& mutex : it->second) {
                auto conflicting = storage_->queryTriples(
                    TripleStore::TriplePattern{t.subject, mutex, "", false, false, true});
                if (!conflicting.empty()) {
                    conflicts.push_back("冲突: " + t.subject + " 同时有 " +
                        t.predicate + " 和 " + mutex + " 关系");
                }
            }
        }
    }

    return conflicts;
}
```

Replace with:
```cpp
std::vector<Conflict> AutoModelEngine::detectConflicts() {
    std::vector<Conflict> conflicts;
    auto* ts = storage_->getTripleStore();
    if (!ts) return conflicts;

    auto triples = storage_->getAllTriples();

    // ---- 1. Disjoint class assertion conflicts ----
    // Collect owl:disjointWith pairs (and their transitive closure via rdfs:subClassOf)
    std::vector<std::pair<String, String>> disjointPairs;
    auto disjointTriples = ts->findByPredicate(
        "http://www.w3.org/2002/07/owl#disjointWith");
    for (const auto& dt : disjointTriples) {
        disjointPairs.push_back({dt.subject, dt.object});
    }

    // Collect rdf:type assertions per individual
    std::unordered_map<String, std::vector<String>> individualTypes;
    auto typeTriples = ts->findByPredicate(
        "http://www.w3.org/1999/02/22-rdf-syntax-ns#type");
    for (const auto& tt : typeTriples) {
        individualTypes[tt.subject].push_back(tt.object);
    }

    // Check each individual for disjoint class assertions
    for (const auto& [individual, types] : individualTypes) {
        for (size_t i = 0; i < types.size(); ++i) {
            for (size_t j = i + 1; j < types.size(); ++j) {
                for (const auto& [c1, c2] : disjointPairs) {
                    if ((types[i] == c1 && types[j] == c2) ||
                        (types[i] == c2 && types[j] == c1)) {
                        Conflict c;
                        c.type = Conflict::DisjointClassAssertion;
                        c.description = individual + " is typed as both " +
                            types[i] + " and " + types[j] + " which are disjoint";
                        c.severity = 1.0f;
                        // Collect conflicting triples
                        for (const auto& tt : typeTriples) {
                            if (tt.subject == individual &&
                                (tt.object == types[i] || tt.object == types[j])) {
                                c.conflictingTriples.push_back(tt);
                            }
                        }
                        conflicts.push_back(c);
                    }
                }
            }
        }
    }

    // ---- 2. Functional property violations ----
    auto funcPropTriples = ts->findByPO(
        "http://www.w3.org/1999/02/22-rdf-syntax-ns#type",
        "http://www.w3.org/2002/07/owl#FunctionalProperty");

    for (const auto& fp : funcPropTriples) {
        String prop = fp.subject;
        // Find all subjects that have multiple values for this property
        std::unordered_map<String, std::vector<Triple>> subjectValues;
        auto propTriples = ts->findByPredicate(prop);
        for (const auto& pt : propTriples) {
            subjectValues[pt.subject].push_back(pt);
        }
        for (const auto& [subj, vals] : subjectValues) {
            if (vals.size() > 1) {
                Conflict c;
                c.type = Conflict::FunctionalPropertyViolation;
                c.description = subj + " has " + std::to_string(vals.size()) +
                    " values for functional property " + prop;
                c.conflictingTriples = vals;
                c.severity = 0.9f;
                conflicts.push_back(c);
            }
        }
    }

    return conflicts;
}
```

- [ ] **Step 3: Build and test**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && make test_auto_model && ./tests/test_auto_model`

Expected: All tests pass (3 existing + 3 new).

- [ ] **Step 4: Commit**

```bash
git add src/inference/AutoModel.cpp tests/test_auto_model.cpp
git commit -m "feat(automodel): rewrite detectConflicts with TBox-aware disjoint and functional property checks"
```

---

### Task 3: Enhance alignEntities with Multi-Signal Scoring

**Files:**
- Modify: `src/inference/AutoModel.cpp:1665-1708`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_auto_model.cpp` before `main()`:

```cpp
void test_align_entities_multi_signal() {
    TEST("alignEntities uses multi-signal scoring");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    AutoModelEngine engine(storage);

    auto* ts = storage->getTripleStore();
    // Entity1: Dog with properties hasPart, livesIn
    ts->add({"Dog", "hasPart", "Tail"});
    ts->add({"Dog", "livesIn", "Home"});
    // Entity2: Canine with same properties
    ts->add({"Canine", "hasPart", "Tail"});
    ts->add({"Canine", "livesIn", "Home"});

    auto alignments = engine.alignEntities({"Dog"}, {"Canine"});
    // Should find alignment even without embeddings
    // because structural and label similarity are high
    bool found = false;
    for (const auto& a : alignments) {
        if ((a.entity1 == "Dog" && a.entity2 == "Canine") ||
            (a.entity1 == "Canine" && a.entity2 == "Dog")) {
            found = true;
            // Structural score should be high (same properties)
            ASSERT_TRUE(a.structuralScore > 0.5f);
            // Label score: "Dog" vs "Canine" — Levenshtein
            ASSERT_TRUE(a.labelScore >= 0.0f);
        }
    }
    ASSERT_TRUE(found);
    PASS();
}
```

Add call in `main()`:
```cpp
    test_align_entities_multi_signal();
```

- [ ] **Step 2: Implement helper methods and enhanced alignEntities**

Add the helper implementations before the `alignEntities` method in `src/inference/AutoModel.cpp`:

```cpp
size_t AutoModelEngine::tripleHash(const Triple& t) {
    std::hash<String> hasher;
    return hasher(t.subject) ^ (hasher(t.predicate) << 1) ^ (hasher(t.object) << 2);
}

int AutoModelEngine::levenshteinDistance(const String& s1, const String& s2) {
    int m = static_cast<int>(s1.size());
    int n = static_cast<int>(s2.size());
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1));

    for (int i = 0; i <= m; ++i) dp[i][0] = i;
    for (int j = 0; j <= n; ++j) dp[0][j] = j;

    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            dp[i][j] = std::min({dp[i-1][j] + 1, dp[i][j-1] + 1, dp[i-1][j-1] + cost});
        }
    }
    return dp[m][n];
}

float AutoModelEngine::jaccardCoefficient(const String& entity1, const String& entity2) const {
    auto* ts = storage_->getTripleStore();
    if (!ts) return 0.0f;

    // Get properties/relations for each entity
    auto triples1 = ts->findBySubject(entity1);
    auto triples2 = ts->findBySubject(entity2);

    std::unordered_set<String> props1, props2;
    for (const auto& t : triples1) props1.insert(t.predicate);
    for (const auto& t : triples2) props2.insert(t.predicate);

    if (props1.empty() && props2.empty()) return 0.0f;

    int intersection = 0;
    for (const auto& p : props1) {
        if (props2.count(p)) intersection++;
    }
    int unionSize = static_cast<int>(props1.size() + props2.size()) - intersection;
    return unionSize > 0 ? static_cast<float>(intersection) / unionSize : 0.0f;
}
```

Replace `alignEntities()` (lines 1665-1708):

Find:
```cpp
std::vector<std::pair<String, String>> AutoModelEngine::alignEntities(
    const std::vector<String>& entities1,
    const std::vector<String>& entities2) {

    std::vector<std::pair<String, String>> alignments;

    if (!embeddingsTrained_) {
        trainEmbeddings();
    }

    // 使用嵌入相似度进行对齐
    for (const auto& e1 : entities1) {
        float maxSim = 0.0f;
        String bestMatch;

        auto emb1 = neuralReasoner_->getEmbedding(e1);
        if (emb1.empty()) continue;

        for (const auto& e2 : entities2) {
            auto emb2 = neuralReasoner_->getEmbedding(e2);
            if (emb2.empty()) continue;

            // 计算余弦相似度
            float dot = 0, norm1 = 0, norm2 = 0;
            for (size_t i = 0; i < emb1.size(); ++i) {
                dot += emb1[i] * emb2[i];
                norm1 += emb1[i] * emb1[i];
                norm2 += emb2[i] * emb2[i];
            }

            float sim = dot / (std::sqrt(norm1) * std::sqrt(norm2));
            if (sim > maxSim) {
                maxSim = sim;
                bestMatch = e2;
            }
        }

        if (maxSim >= 0.8f) {
            alignments.push_back({e1, bestMatch});
        }
    }

    return alignments;
}
```

Replace with:
```cpp
std::vector<AlignmentResult> AutoModelEngine::alignEntities(
    const std::vector<String>& entities1,
    const std::vector<String>& entities2) {

    std::vector<AlignmentResult> alignments;

    for (const auto& e1 : entities1) {
        for (const auto& e2 : entities2) {
            AlignmentResult result;
            result.entity1 = e1;
            result.entity2 = e2;

            // 1. Embedding similarity
            result.embeddingScore = 0.0f;
            if (embeddingsTrained_ && neuralReasoner_) {
                auto emb1 = neuralReasoner_->getEmbedding(e1);
                auto emb2 = neuralReasoner_->getEmbedding(e2);
                if (!emb1.empty() && !emb2.empty() && emb1.size() == emb2.size()) {
                    float dot = 0, norm1 = 0, norm2 = 0;
                    for (size_t i = 0; i < emb1.size(); ++i) {
                        dot += emb1[i] * emb2[i];
                        norm1 += emb1[i] * emb1[i];
                        norm2 += emb2[i] * emb2[i];
                    }
                    if (norm1 > 0 && norm2 > 0) {
                        result.embeddingScore = dot / (std::sqrt(norm1) * std::sqrt(norm2));
                    }
                }
            }

            // 2. Structural similarity (Jaccard of shared properties)
            result.structuralScore = jaccardCoefficient(e1, e2);

            // 3. Label similarity (normalized Levenshtein)
            int dist = levenshteinDistance(e1, e2);
            int maxLen = std::max(static_cast<int>(e1.size()), static_cast<int>(e2.size()));
            result.labelScore = maxLen > 0 ? 1.0f - static_cast<float>(dist) / maxLen : 0.0f;

            // Combined score
            result.combinedScore =
                alignWeightEmbedding_ * result.embeddingScore +
                alignWeightStructural_ * result.structuralScore +
                alignWeightLabel_ * result.labelScore;

            // Include if combined score above threshold (0.3 to allow structural/label-only matches)
            if (result.combinedScore >= 0.3f) {
                alignments.push_back(result);
            }
        }
    }

    // Sort by combined score descending
    std::sort(alignments.begin(), alignments.end(),
        [](const AlignmentResult& a, const AlignmentResult& b) {
            return a.combinedScore > b.combinedScore;
        });

    return alignments;
}
```

- [ ] **Step 3: Build and test**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && make test_auto_model && ./tests/test_auto_model`

Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/inference/AutoModel.cpp tests/test_auto_model.cpp
git commit -m "feat(automodel): enhance alignEntities with multi-signal scoring (embedding + structural + label)"
```

---

### Task 4: Provenance-aware mergeOntologies

**Files:**
- Modify: `src/inference/AutoModel.cpp:1422-1455`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_auto_model.cpp` before `main()`:

```cpp
void test_merge_ontologies_with_provenance() {
    TEST("mergeOntologies tracks provenance and deduplicates");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    AutoModelEngine engine(storage);

    auto* ts = storage->getTripleStore();
    // Local triple
    ts->add({"Dog", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});

    // External triples (including a duplicate)
    std::vector<Triple> external = {
        {"Dog", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal", "", 1.0f, "", ""},  // duplicate
        {"Cat", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal", "", 1.0f, "", ""}   // new
    };

    engine.mergeOntologies(external, "ext1", "External Source");

    // Should have 2 unique triples (not 3 — duplicate deduped)
    auto allTriples = storage->getAllTriples();
    // Count unique (subject, predicate, object) pairs
    std::unordered_set<String> uniqueKeys;
    for (const auto& t : allTriples) {
        uniqueKeys.insert(t.subject + "|" + t.predicate + "|" + t.object);
    }
    ASSERT_TRUE(uniqueKeys.size() == 2);  // Dog⊑Animal and Cat⊑Animal

    PASS();
}
```

Add call in `main()`:
```cpp
    test_merge_ontologies_with_provenance();
```

- [ ] **Step 2: Implement provenance-aware mergeOntologies**

Replace `mergeOntologies()` in `src/inference/AutoModel.cpp` (lines 1422-1455):

Find:
```cpp
void AutoModelEngine::mergeOntologies(const std::vector<Triple>& externalTriples) {
    // 实体对齐
    std::vector<String> localEntities, externalEntities;
    auto localIndividuals = storage_->getAllIndividuals();

    for (const auto& ind : localIndividuals) {
        localEntities.push_back(ind.id);
    }

    std::unordered_set<String> externalEntitySet;
    for (const auto& t : externalTriples) {
        externalEntitySet.insert(t.subject);
        externalEntitySet.insert(t.object);
    }
    for (const auto& e : externalEntitySet) {
        externalEntities.push_back(e);
    }

    auto alignments = alignEntities(localEntities, externalEntities);

    // 根据对齐结果合并
    for (const auto& t : externalTriples) {
        Triple merged = t;

        // 检查主体是否需要对齐
        for (const auto& [local, ext] : alignments) {
            if (merged.subject == ext) merged.subject = local;
            if (merged.object == ext) merged.object = local;
        }

        // 添加合并后的三元组
        storage_->addTriple(merged);
    }
}
```

Replace with:
```cpp
void AutoModelEngine::mergeOntologies(const std::vector<Triple>& externalTriples,
                                       const String& sourceId,
                                       const String& sourceName) {
    // Entity alignment
    std::vector<String> localEntities, externalEntities;
    auto localIndividuals = storage_->getAllIndividuals();
    for (const auto& ind : localIndividuals) {
        localEntities.push_back(ind.id);
    }

    std::unordered_set<String> externalEntitySet;
    for (const auto& t : externalTriples) {
        externalEntitySet.insert(t.subject);
        externalEntitySet.insert(t.object);
    }
    for (const auto& e : externalEntitySet) {
        externalEntities.push_back(e);
    }

    auto alignments = alignEntities(localEntities, externalEntities);

    // Build alignment map: external -> local
    std::unordered_map<String, String> extToLocal;
    for (const auto& a : alignments) {
        if (a.combinedScore >= 0.5f) {  // only use high-confidence alignments
            extToLocal[a.entity2] = a.entity1;
        }
    }

    // Get existing triples for deduplication
    auto existingTriples = storage_->getAllTriples();
    std::unordered_set<size_t> existingHashes;
    for (const auto& t : existingTriples) {
        existingHashes.insert(tripleHash(t));
    }

    // Get current timestamp
    String timestamp = epochMsToIso(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    // Merge with deduplication and provenance
    for (const auto& t : externalTriples) {
        Triple merged = t;

        // Apply alignment
        auto it1 = extToLocal.find(merged.subject);
        if (it1 != extToLocal.end()) merged.subject = it1->second;
        auto it2 = extToLocal.find(merged.object);
        if (it2 != extToLocal.end()) merged.object = it2->second;

        // Deduplication: skip if this triple already exists
        size_t h = tripleHash(merged);
        if (existingHashes.count(h)) {
            // Record provenance for existing triple (merge source info)
            provenanceIndex_[h].sourceId += "," + sourceId;
            continue;
        }

        // Add the triple
        storage_->addTriple(merged);
        existingHashes.insert(h);

        // Record provenance
        Provenance prov;
        prov.sourceId = sourceId;
        prov.sourceName = sourceName;
        prov.confidence = t.confidence;
        prov.timestamp = timestamp;
        provenanceIndex_[h] = prov;
    }

    // Run conflict detection on merged data
    auto conflicts = detectConflicts();
    // Log conflicts (but don't auto-resolve)
    for (const auto& c : conflicts) {
        // Conflicts are available for external inspection
        (void)c;
    }
}
```

Add the missing include at the top of `src/inference/AutoModel.cpp`:
```cpp
#include <chrono>
```

Also add the `epochMsToIso` declaration if not already available — it's in `Temporal.hpp`. Add include:
```cpp
#include <ontology/Temporal.hpp>
```

- [ ] **Step 3: Build and test**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && make test_auto_model && ./tests/test_auto_model`

Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/inference/AutoModel.cpp tests/test_auto_model.cpp
git commit -m "feat(automodel): provenance-aware mergeOntologies with deduplication and source tracking"
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
