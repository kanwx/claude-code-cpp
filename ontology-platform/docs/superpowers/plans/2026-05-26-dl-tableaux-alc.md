# DL/Tableaux ALC Reasoner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement an ALC description logic reasoner with Tableaux algorithm for satisfiability, subsumption, equivalence, consistency, classification, and realization.

**Architecture:** New `DlReasoner` class owns a TBox (GCIs) and ABox (individual assertions). Tableaux algorithm works on completion graphs with subset blocking. DlReasoner integrates alongside HybridReasoner via an optional pointer.

**Tech Stack:** C++17, existing `ClassExpression` types, `TripleStore` for axiom storage.

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `include/ontology/DlReasoner.hpp` | Create | DlReasoner class, CompletionGraph, TableauxNode |
| `src/inference/DlReasoner.cpp` | Create | Full Tableaux algorithm, NNF conversion, blocking, classification |
| `include/ontology/Inference.hpp` | Modify | Add DlReasoner pointer to HybridReasoner |
| `src/inference/HybridReasoner.cpp` | Modify | Delegate consistency/subsumption to DlReasoner when set |
| `src/owl/ExpressionParser.cpp` | Modify | Add NNF conversion to ClassExpressionEvaluator |
| `include/ontology/ClassExpression.hpp` | Modify | Add NNF method declaration |
| `tests/test_alc_reasoner.cpp` | Create | Unit tests |
| `tests/CMakeLists.txt` | Modify | Add test target |

---

### Task 1: ClassExpression NNF Conversion

**Files:**
- Modify: `include/ontology/ClassExpression.hpp:215-259`
- Modify: `src/owl/ExpressionParser.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_class_expression.cpp`, before `main()`:

```cpp
void test_nnf_double_negation() {
    TEST("NNF converts double negation");
    auto expr = ClassExpression::complement(
        ClassExpression::complement(ClassExpression::atomic("A")));
    auto nnf = ClassExpressionEvaluator::toNNF(*expr);
    ASSERT_TRUE(nnf->type == ExpressionType::Atomic);
    ASSERT_TRUE(nnf->className == "A");
    PASS();
}

void test_nnf_conjunction_de_morgan() {
    TEST("NNF pushes negation through intersection (De Morgan)");
    // ¬(A ⊓ B) → ¬A ⊔ ¬B
    auto expr = ClassExpression::complement(
        ClassExpression::intersection({
            ClassExpression::atomic("A"),
            ClassExpression::atomic("B")
        }));
    auto nnf = ClassExpressionEvaluator::toNNF(*expr);
    ASSERT_TRUE(nnf->type == ExpressionType::Union);
    ASSERT_EQ(nnf->operands.size(), 2u);
    ASSERT_TRUE(nnf->operands[0]->type == ExpressionType::Complement);
    ASSERT_TRUE(nnf->operands[1]->type == ExpressionType::Complement);
    PASS();
}

void test_nnf_quantifier_negation() {
    TEST("NNF pushes negation through quantifiers");
    // ¬∃R.A → ∀R.¬A
    auto expr = ClassExpression::complement(
        ClassExpression::someValuesFrom("R", ClassExpression::atomic("A")));
    auto nnf = ClassExpressionEvaluator::toNNF(*expr);
    ASSERT_TRUE(nnf->type == ExpressionType::ObjectAllValuesFrom);
    ASSERT_TRUE(nnf->property == "R");
    ASSERT_TRUE(nnf->filler->type == ExpressionType::Complement);
    PASS();
}

void test_nnf_disjunction_de_morgan() {
    TEST("NNF pushes negation through union (De Morgan)");
    // ¬(A ⊔ B) → ¬A ⊓ ¬B
    auto expr = ClassExpression::complement(
        ClassExpression::union_({
            ClassExpression::atomic("A"),
            ClassExpression::atomic("B")
        }));
    auto nnf = ClassExpressionEvaluator::toNNF(*expr);
    ASSERT_TRUE(nnf->type == ExpressionType::Intersection);
    ASSERT_EQ(nnf->operands.size(), 2u);
    ASSERT_TRUE(nnf->operands[0]->type == ExpressionType::Complement);
    ASSERT_TRUE(nnf->operands[1]->type == ExpressionType::Complement);
    PASS();
}
```

Add calls in `main()`:
```cpp
    test_nnf_double_negation();
    test_nnf_conjunction_de_morgan();
    test_nnf_quantifier_negation();
    test_nnf_disjunction_de_morgan();
```

- [ ] **Step 2: Add toNNF declaration to ClassExpression.hpp**

Add to `ClassExpressionEvaluator` class (before closing `};`):

```cpp
    // Negation Normal Form conversion (push negations inward)
    static ClassExpressionPtr toNNF(const ClassExpression& expr);
```

- [ ] **Step 3: Implement toNNF in ExpressionParser.cpp**

Add at the end of the file, before the closing namespace brace:

```cpp
ClassExpressionPtr ClassExpressionEvaluator::toNNF(const ClassExpression& expr) {
    switch (expr.type) {
        case ExpressionType::Atomic:
        case ExpressionType::Top:
        case ExpressionType::Bottom:
            return std::make_shared<ClassExpression>(expr);

        case ExpressionType::Complement: {
            if (!expr.complementOf) return ClassExpression::top();
            auto& inner = *expr.complementOf;
            // ¬¬A → A
            if (inner.type == ExpressionType::Complement && inner.complementOf) {
                return toNNF(*inner.complementOf);
            }
            // ¬(A ⊓ B) → ¬A ⊔ ¬B (De Morgan)
            if (inner.type == ExpressionType::Intersection) {
                std::vector<ClassExpressionPtr> negOps;
                for (const auto& op : inner.operands) {
                    negOps.push_back(toNNF(*ClassExpression::complement(op)));
                }
                return ClassExpression::union_(negOps);
            }
            // ¬(A ⊔ B) → ¬A ⊓ ¬B (De Morgan)
            if (inner.type == ExpressionType::Union) {
                std::vector<ClassExpressionPtr> negOps;
                for (const auto& op : inner.operands) {
                    negOps.push_back(toNNF(*ClassExpression::complement(op)));
                }
                return ClassExpression::intersection(negOps);
            }
            // ¬∃R.C → ∀R.¬C
            if (inner.type == ExpressionType::ObjectSomeValuesFrom) {
                auto negFiller = toNNF(*ClassExpression::complement(inner.filler));
                return ClassExpression::allValuesFrom(inner.property, negFiller);
            }
            // ¬∀R.C → ∃R.¬C
            if (inner.type == ExpressionType::ObjectAllValuesFrom) {
                auto negFiller = toNNF(*ClassExpression::complement(inner.filler));
                return ClassExpression::someValuesFrom(inner.property, negFiller);
            }
            // ¬Top → Bottom
            if (inner.type == ExpressionType::Top) {
                return ClassExpression::bottom();
            }
            // ¬Bottom → Top
            if (inner.type == ExpressionType::Bottom) {
                return ClassExpression::top();
            }
            // ¬Atomic — keep as is (negated atomic is in NNF)
            auto result = std::make_shared<ClassExpression>(expr);
            result->complementOf = toNNF(inner);
            return result;
        }

        case ExpressionType::Intersection: {
            std::vector<ClassExpressionPtr> ops;
            for (const auto& op : expr.operands) {
                ops.push_back(toNNF(*op));
            }
            return ClassExpression::intersection(ops);
        }

        case ExpressionType::Union: {
            std::vector<ClassExpressionPtr> ops;
            for (const auto& op : expr.operands) {
                ops.push_back(toNNF(*op));
            }
            return ClassExpression::union_(ops);
        }

        case ExpressionType::ObjectSomeValuesFrom: {
            auto filler = toNNF(*expr.filler);
            return ClassExpression::someValuesFrom(expr.property, filler);
        }

        case ExpressionType::ObjectAllValuesFrom: {
            auto filler = toNNF(*expr.filler);
            return ClassExpression::allValuesFrom(expr.property, filler);
        }

        default:
            return std::make_shared<ClassExpression>(expr);
    }
}
```

- [ ] **Step 4: Build and test**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && make test_class_expression && ./tests/test_class_expression`

Expected: All 15 tests pass (11 existing + 4 new).

- [ ] **Step 5: Commit**

```bash
git add include/ontology/ClassExpression.hpp src/owl/ExpressionParser.cpp tests/test_class_expression.cpp
git commit -m "feat(classexpr): add Negation Normal Form (NNF) conversion for Tableaux reasoner"
```

---

### Task 2: DlReasoner Header — Data Structures

**Files:**
- Create: `include/ontology/DlReasoner.hpp`

- [ ] **Step 1: Create the header file**

```cpp
#pragma once

#include <ontology/ClassExpression.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <optional>

namespace ontology {

// ============================================================================
// ALC Description Logic Reasoner with Tableaux Algorithm
// ============================================================================

// Forward declarations
class TripleStore;

// TBox axiom: General Concept Inclusion (GCI) C ⊑ D
struct TBoxAxiom {
    ClassExpressionPtr subConcept;   // C
    ClassExpressionPtr superConcept;  // D
};

// ABox concept assertion: a : C
struct ConceptAssertion {
    String individual;
    ClassExpressionPtr concept;
};

// ABox role assertion: (a, R, b)
struct RoleAssertion {
    String subject;
    String property;
    String object;
};

// ============================================================================
// Completion Graph Node (Tableaux individual)
// ============================================================================

struct TableauxNode {
    String name;                                     // individual name (or generated "x_N")
    std::vector<ClassExpressionPtr> concepts;        // concept labels
    std::vector<std::pair<String, String>> successors; // (property, target) edges
    std::vector<std::pair<String, String>> predecessors; // (property, source) edges
    bool isBlocked = false;
    String blockedBy;                               // name of blocking node

    bool hasConcept(const ClassExpression& concept) const;
    bool hasClash() const;
};

// ============================================================================
// DlReasoner — ALC Tableaux Reasoner
// ============================================================================

class DlReasoner {
public:
    DlReasoner() = default;

    // ---- TBox management ----
    void addSubClassOf(ClassExpressionPtr sub, ClassExpressionPtr sup);
    void addEquivalentClasses(ClassExpressionPtr c1, ClassExpressionPtr c2);
    void addDisjointClasses(ClassExpressionPtr c1, ClassExpressionPtr c2);

    // Load TBox axioms from TripleStore (rdfs:subClassOf, owl:equivalentClass, owl:disjointWith)
    void loadFromTripleStore(TripleStore* store);

    // ---- ABox management ----
    void addConceptAssertion(const String& individual, ClassExpressionPtr concept);
    void addRoleAssertion(const String& subject, const String& property, const String& object);

    // ---- Reasoning services ----

    // Core: is concept C satisfiable w.r.t. TBox?
    bool isSatisfiable(ClassExpressionPtr concept);

    // Subsumption: A ⊑ B iff A ⊓ ¬B is unsatisfiable
    bool isSubsumedBy(ClassExpressionPtr sub, ClassExpressionPtr sup);

    // Equivalence: A ≡ B iff A ⊑ B and B ⊑ A
    bool isEquivalent(ClassExpressionPtr c1, ClassExpressionPtr c2);

    // Consistency: is the ABox+TBox consistent?
    bool isConsistent();

    // Classification: compute full class hierarchy
    std::unordered_map<String, std::vector<String>> classify();

    // Realization: find most specific types for an individual
    std::vector<String> realize(const String& individual);

    // ---- Accessors ----
    const std::vector<TBoxAxiom>& getTBoxAxioms() const { return tboxAxioms_; }
    const std::vector<ConceptAssertion>& getConceptAssertions() const { return aboxConcepts_; }
    const std::vector<RoleAssertion>& getRoleAssertions() const { return aboxRoles_; }

private:
    // TBox and ABox
    std::vector<TBoxAxiom> tboxAxioms_;
    std::vector<ConceptAssertion> aboxConcepts_;
    std::vector<RoleAssertion> aboxRoles_;
    std::unordered_set<String> namedClasses_;

    // Tableaux state
    int nameCounter_ = 0;
    String freshName();

    // Internalized TBox: ⊤ ⊓ (¬C ⊔ D) for each GCI C ⊑ D
    ClassExpressionPtr internalizedTBoxConcept_;

    void internalizeTBox();

    // Tableaux expansion
    bool expand(std::vector<TableauxNode>& nodes);

    // Tableaux rules
    bool applyConjunctionRule(std::vector<TableauxNode>& nodes, TableauxNode& node, size_t conceptIdx);
    bool applyDisjunctionRule(std::vector<TableauxNode>& nodes, TableauxNode& node, size_t conceptIdx);
    bool applyExistentialRule(std::vector<TableauxNode>& nodes, TableauxNode& node, size_t conceptIdx);
    bool applyUniversalRule(std::vector<TableauxNode>& nodes, TableauxNode& node, size_t conceptIdx);

    // Blocking check
    bool isBlocked(const std::vector<TableauxNode>& nodes, const TableauxNode& node) const;

    // Find node by name
    TableauxNode* findNode(std::vector<TableauxNode>& nodes, const String& name);

    // Concept equivalence check (structural, for blocking)
    bool conceptSetsOverlap(
        const std::vector<ClassExpressionPtr>& set1,
        const std::vector<ClassExpressionPtr>& set2) const;
};

} // namespace ontology
```

- [ ] **Step 2: Commit**

```bash
git add include/ontology/DlReasoner.hpp
git commit -m "feat(dl): add DlReasoner header with ALC Tableaux data structures and interface"
```

---

### Task 3: DlReasoner Core — TBox + ABox + Internalization

**Files:**
- Create: `src/inference/DlReasoner.cpp` (partial — TBox/ABox management + internalization)

- [ ] **Step 1: Create the implementation file with TBox/ABox methods**

```cpp
#include <ontology/DlReasoner.hpp>
#include <ontology/Storage.hpp>
#include <algorithm>
#include <queue>

namespace ontology {

// ============================================================================
// TableauxNode helpers
// ============================================================================

bool TableauxNode::hasConcept(const ClassExpression& concept) const {
    for (const auto& c : concepts) {
        if (c->type == concept.type && c->className == concept.className
            && c->property == concept.property
            && c->cardinality == concept.cardinality) {
            return true;
        }
    }
    return false;
}

bool TableauxNode::hasClash() const {
    // Check for ⊥
    for (const auto& c : concepts) {
        if (c->type == ExpressionType::Bottom) return true;
    }
    // Check for C and ¬C
    for (size_t i = 0; i < concepts.size(); ++i) {
        if (concepts[i]->type == ExpressionType::Complement && concepts[i]->complementOf) {
            for (size_t j = 0; j < concepts.size(); ++j) {
                if (i == j) continue;
                if (concepts[j]->type == concepts[i]->complementOf->type
                    && concepts[j]->className == concepts[i]->complementOf->className) {
                    return true;
                }
            }
        }
    }
    return false;
}

// ============================================================================
// TBox management
// ============================================================================

void DlReasoner::addSubClassOf(ClassExpressionPtr sub, ClassExpressionPtr sup) {
    tboxAxioms_.push_back({sub, sup});
    if (sub->type == ExpressionType::Atomic) namedClasses_.insert(sub->className);
    if (sup->type == ExpressionType::Atomic) namedClasses_.insert(sup->className);
    internalizeTBox();
}

void DlReasoner::addEquivalentClasses(ClassExpressionPtr c1, ClassExpressionPtr c2) {
    // A ≡ B is A ⊑ B ∧ B ⊑ A
    addSubClassOf(c1, c2);
    addSubClassOf(c2, c1);
}

void DlReasoner::addDisjointClasses(ClassExpressionPtr c1, ClassExpressionPtr c2) {
    // C ⊓ D ⊑ ⊥
    auto intersection = ClassExpression::intersection({c1, c2});
    addSubClassOf(intersection, ClassExpression::bottom());
}

void DlReasoner::loadFromTripleStore(TripleStore* store) {
    if (!store) return;

    // Load rdfs:subClassOf axioms
    auto subClassTriples = store->findByPredicate(
        "http://www.w3.org/2000/01/rdf-schema#subClassOf");
    for (const auto& t : subClassTriples) {
        addSubClassOf(ClassExpression::atomic(t.subject),
                      ClassExpression::atomic(t.object));
    }

    // Load owl:equivalentClass axioms
    auto equivTriples = store->findByPredicate(
        "http://www.w3.org/2002/07/owl#equivalentClass");
    for (const auto& t : equivTriples) {
        addEquivalentClasses(ClassExpression::atomic(t.subject),
                             ClassExpression::atomic(t.object));
    }

    // Load owl:disjointWith axioms
    auto disjointTriples = store->findByPredicate(
        "http://www.w3.org/2002/07/owl#disjointWith");
    for (const auto& t : disjointTriples) {
        addDisjointClasses(ClassExpression::atomic(t.subject),
                           ClassExpression::atomic(t.object));
    }
}

// ============================================================================
// ABox management
// ============================================================================

void DlReasoner::addConceptAssertion(const String& individual, ClassExpressionPtr concept) {
    aboxConcepts_.push_back({individual, concept});
    if (concept->type == ExpressionType::Atomic) namedClasses_.insert(concept->className);
}

void DlReasoner::addRoleAssertion(const String& subject, const String& property, const String& object) {
    aboxRoles_.push_back({subject, property, object});
}

// ============================================================================
// TBox internalization
// ============================================================================

void DlReasoner::internalizeTBox() {
    // For each GCI C ⊑ D, create ¬C ⊔ D
    // The internalized concept is ⊤ ⊓ (¬C1 ⊔ D1) ⊓ (¬C2 ⊔ D2) ⊓ ...
    if (tboxAxioms_.empty()) {
        internalizedTBoxConcept_ = ClassExpression::top();
        return;
    }

    std::vector<ClassExpressionPtr> conjuncts;
    for (const auto& axiom : tboxAxioms_) {
        auto notSub = ClassExpression::complement(axiom.subConcept);
        auto disj = ClassExpression::union_({notSub, axiom.superConcept});
        conjuncts.push_back(disj);
    }

    if (conjuncts.size() == 1) {
        internalizedTBoxConcept_ = conjuncts[0];
    } else {
        internalizedTBoxConcept_ = ClassExpression::intersection(conjuncts);
    }
}

String DlReasoner::freshName() {
    return "x_" + std::to_string(nameCounter_++);
}

} // namespace ontology
```

- [ ] **Step 2: Commit**

```bash
git add src/inference/DlReasoner.cpp
git commit -m "feat(dl): implement DlReasoner TBox/ABox management and TBox internalization"
```

---

### Task 4: DlReasoner Tableaux Algorithm

**Files:**
- Modify: `src/inference/DlReasoner.cpp` — add Tableaux expansion, rules, blocking, and reasoning services

- [ ] **Step 1: Implement Tableaux expansion and rules**

Append to `src/inference/DlReasoner.cpp` before the closing `} // namespace ontology`:

```cpp
// ============================================================================
// Tableaux expansion
// ============================================================================

TableauxNode* DlReasoner::findNode(std::vector<TableauxNode>& nodes, const String& name) {
    for (auto& node : nodes) {
        if (node.name == name) return &node;
    }
    return nullptr;
}

bool DlReasoner::conceptSetsOverlap(
    const std::vector<ClassExpressionPtr>& set1,
    const std::vector<ClassExpressionPtr>& set2) const
{
    for (const auto& c1 : set1) {
        for (const auto& c2 : set2) {
            if (c1->type == c2->type && c1->className == c2->className
                && c1->property == c2->property) {
                return true;
            }
        }
    }
    return false;
}

bool DlReasoner::isBlocked(const std::vector<TableauxNode>& nodes, const TableauxNode& node) const {
    // Subset blocking: node is blocked if any ancestor has a superset of its concepts
    for (const auto& other : nodes) {
        if (other.name == node.name) continue;
        // Check if other is an ancestor (connected via role assertions)
        // Simplified: check if other's concept set is a superset of node's
        bool isSuperset = true;
        for (const auto& c : node.concepts) {
            bool found = false;
            for (const auto& oc : other.concepts) {
                if (c->type == oc->type && c->className == oc->className
                    && c->property == oc->property) {
                    found = true;
                    break;
                }
            }
            if (!found) { isSuperset = false; break; }
        }
        if (isSuperset && other.name != node.name) return true;
    }
    return false;
}

bool DlReasoner::applyConjunctionRule(std::vector<TableauxNode>& nodes,
                                       TableauxNode& node, size_t conceptIdx)
{
    auto& concept = node.concepts[conceptIdx];
    if (concept->type != ExpressionType::Intersection) return false;

    bool changed = false;
    for (const auto& op : concept->operands) {
        if (!node.hasConcept(*op)) {
            node.concepts.push_back(op);
            changed = true;
        }
    }
    return changed;
}

bool DlReasoner::applyDisjunctionRule(std::vector<TableauxNode>& nodes,
                                       TableauxNode& node, size_t conceptIdx)
{
    auto& concept = node.concepts[conceptIdx];
    if (concept->type != ExpressionType::Union) return false;
    if (concept->operands.size() == 0) return false;

    // Non-deterministic: try each disjunct. For simplicity, use backtracking.
    // We'll handle this by returning false and letting isSatisfiable do backtracking.
    return false;  // handled directly in isSatisfiable
}

bool DlReasoner::applyExistentialRule(std::vector<TableauxNode>& nodes,
                                       TableauxNode& node, size_t conceptIdx)
{
    auto& concept = node.concepts[conceptIdx];
    if (concept->type != ExpressionType::ObjectSomeValuesFrom) return false;
    if (!concept->filler) return false;
    if (node.isBlocked) return false;

    // Check if there's already an R-successor with the filler
    for (const auto& [prop, target] : node.successors) {
        if (prop == concept->property) {
            auto* targetNode = findNode(nodes, target);
            if (targetNode && targetNode->hasConcept(*concept->filler)) {
                return false;  // already exists
            }
        }
    }

    // Create new individual
    String newName = freshName();
    TableauxNode newNode;
    newNode.name = newName;

    // Add filler concept
    newNode.concepts.push_back(concept->filler);

    // Add internalized TBox concept
    if (internalizedTBoxConcept_) {
        newNode.concepts.push_back(internalizedTBoxConcept_);
    }

    // Add role assertion
    node.successors.push_back({concept->property, newName});
    newNode.predecessors.push_back({concept->property, node.name});

    nodes.push_back(std::move(newNode));
    return true;
}

bool DlReasoner::applyUniversalRule(std::vector<TableauxNode>& nodes,
                                     TableauxNode& node, size_t conceptIdx)
{
    auto& concept = node.concepts[conceptIdx];
    if (concept->type != ExpressionType::ObjectAllValuesFrom) return false;
    if (!concept->filler) return false;

    bool changed = false;
    // For each R-successor, add the filler concept
    for (const auto& [prop, target] : node.successors) {
        if (prop == concept->property) {
            auto* targetNode = findNode(nodes, target);
            if (targetNode && !targetNode->hasConcept(*concept->filler)) {
                targetNode->concepts.push_back(concept->filler);
                changed = true;
            }
        }
    }
    return changed;
}

bool DlReasoner::expand(std::vector<TableauxNode>& nodes) {
    bool changed = true;
    while (changed) {
        changed = false;

        for (auto& node : nodes) {
            if (node.hasClash()) return true;  // clash found

            // Check blocking
            node.isBlocked = isBlocked(nodes, node);
            if (node.isBlocked) continue;

            for (size_t i = 0; i < node.concepts.size(); ++i) {
                if (applyConjunctionRule(nodes, node, i)) { changed = true; break; }
            }
            if (changed) continue;

            for (size_t i = 0; i < node.concepts.size(); ++i) {
                if (applyExistentialRule(nodes, node, i)) { changed = true; break; }
            }
            if (changed) continue;

            for (size_t i = 0; i < node.concepts.size(); ++i) {
                if (applyUniversalRule(nodes, node, i)) { changed = true; break; }
            }
            if (changed) continue;
        }
    }

    // Check for clashes
    for (const auto& node : nodes) {
        if (node.hasClash()) return true;
    }
    return false;
}
```

- [ ] **Step 2: Implement isSatisfiable with disjunction backtracking**

Append before closing namespace:

```cpp
// ============================================================================
// Reasoning services
// ============================================================================

bool DlReasoner::isSatisfiable(ClassExpressionPtr concept) {
    // Convert to NNF
    auto nnf = ClassExpressionEvaluator::toNNF(*concept);

    // Build initial completion graph: one node with the concept + internalized TBox
    std::vector<TableauxNode> nodes;
    TableauxNode root;
    root.name = "a_0";
    root.concepts.push_back(nnf);
    if (internalizedTBoxConcept_) {
        root.concepts.push_back(
            ClassExpressionEvaluator::toNNF(*internalizedTBoxConcept_));
    }
    nodes.push_back(std::move(root));

    return isSatisfiableHelper(nodes);
}

bool DlReasoner::isSatisfiableHelper(std::vector<TableauxNode> nodes) {
    // First, apply deterministic rules (∧, ∃, ∀)
    bool hasClash = expand(nodes);
    if (hasClash) return false;

    // Find an unexpanded disjunction
    for (auto& node : nodes) {
        if (node.isBlocked) continue;
        for (size_t i = 0; i < node.concepts.size(); ++i) {
            if (node.concepts[i]->type == ExpressionType::Union) {
                auto& disj = node.concepts[i];
                // Try each disjunct via backtracking
                for (const auto& disjunct : disj->operands) {
                    // Branch: create copy with this disjunct added
                    auto branchNodes = nodes;
                    auto* branchNode = findNode(branchNodes, node.name);
                    if (branchNode) {
                        branchNode->concepts.push_back(disjunct);
                        if (isSatisfiableHelper(std::move(branchNodes))) {
                            return true;  // found a consistent branch
                        }
                    }
                }
                return false;  // all branches lead to clash
            }
        }
    }

    // No unexpanded disjunctions and no clashes → satisfiable
    return true;
}
```

Note: `isSatisfiableHelper` is a private method. Add its declaration to the class in the header:

```cpp
    bool isSatisfiableHelper(std::vector<TableauxNode> nodes);
```

- [ ] **Step 3: Implement subsumption, equivalence, consistency**

Append before closing namespace:

```cpp
bool DlReasoner::isSubsumedBy(ClassExpressionPtr sub, ClassExpressionPtr sup) {
    // A ⊑ B iff A ⊓ ¬B is unsatisfiable
    auto aAndNotB = ClassExpression::intersection({
        sub,
        ClassExpression::complement(sup)
    });
    return !isSatisfiable(aAndNotB);
}

bool DlReasoner::isEquivalent(ClassExpressionPtr c1, ClassExpressionPtr c2) {
    return isSubsumedBy(c1, c2) && isSubsumedBy(c2, c1);
}

bool DlReasoner::isConsistent() {
    // Build initial graph from all ABox assertions
    std::vector<TableauxNode> nodes;

    // Create nodes for all individuals
    std::unordered_set<String> individuals;
    for (const auto& ca : aboxConcepts_) individuals.insert(ca.individual);
    for (const auto& ra : aboxRoles_) {
        individuals.insert(ra.subject);
        individuals.insert(ra.object);
    }

    for (const auto& ind : individuals) {
        TableauxNode node;
        node.name = ind;
        // Add all concept assertions for this individual
        for (const auto& ca : aboxConcepts_) {
            if (ca.individual == ind) {
                auto nnf = ClassExpressionEvaluator::toNNF(*ca.concept);
                node.concepts.push_back(nnf);
            }
        }
        // Add internalized TBox
        if (internalizedTBoxConcept_) {
            node.concepts.push_back(
                ClassExpressionEvaluator::toNNF(*internalizedTBoxConcept_));
        }
        // Add role assertions
        for (const auto& ra : aboxRoles_) {
            if (ra.subject == ind) {
                node.successors.push_back({ra.property, ra.object});
            }
        }
        nodes.push_back(std::move(node));
    }

    // Check for immediate clashes
    for (const auto& node : nodes) {
        if (node.hasClash()) return false;
    }

    // Expand
    return isSatisfiableHelper(nodes);
}
```

- [ ] **Step 4: Implement classify and realize**

Append before closing namespace:

```cpp
std::unordered_map<String, std::vector<String>> DlReasoner::classify() {
    std::unordered_map<String, std::vector<String>> hierarchy;

    // Get all named classes from TBox
    std::vector<String> classes(namedClasses_.begin(), namedClasses_.end());

    for (const auto& sub : classes) {
        for (const auto& sup : classes) {
            if (sub == sup) continue;
            if (isSubsumedBy(ClassExpression::atomic(sub),
                            ClassExpression::atomic(sup))) {
                hierarchy[sub].push_back(sup);
            }
        }
    }

    return hierarchy;
}

std::vector<String> DlReasoner::realize(const String& individual) {
    std::vector<String> types;

    // Collect all classes the individual is asserted to be an instance of
    std::unordered_set<String> asserted;
    for (const auto& ca : aboxConcepts_) {
        if (ca.individual == individual && ca.concept->type == ExpressionType::Atomic) {
            asserted.insert(ca.concept->className);
        }
    }

    // For each named class, check if individual is an instance
    for (const auto& cls : namedClasses_) {
        if (asserted.count(cls)) {
            types.push_back(cls);
            continue;
        }
        // Check via consistency: add a : cls, is ABox still consistent?
        auto savedConcepts = aboxConcepts_;
        addConceptAssertion(individual, ClassExpression::atomic(cls));
        bool consistent = isConsistent();
        aboxConcepts_ = std::move(savedConcepts);

        if (consistent) {
            types.push_back(cls);
        }
    }

    // Filter to most specific types
    std::vector<String> mostSpecific;
    for (const auto& t1 : types) {
        bool isMostSpecific = true;
        for (const auto& t2 : types) {
            if (t1 != t2 && isSubsumedBy(ClassExpression::atomic(t1),
                                         ClassExpression::atomic(t2))) {
                isMostSpecific = false;
                break;
            }
        }
        if (isMostSpecific) mostSpecific.push_back(t1);
    }

    return mostSpecific;
}
```

- [ ] **Step 5: Commit**

```bash
git add include/ontology/DlReasoner.hpp src/inference/DlReasoner.cpp
git commit -m "feat(dl): implement ALC Tableaux algorithm with satisfiability, subsumption, equivalence, consistency, classification, realization"
```

---

### Task 5: DlReasoner Unit Tests

**Files:**
- Create: `tests/test_alc_reasoner.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create test file**

```cpp
#include "TestUtils.hpp"
#include <ontology/DlReasoner.hpp>
#include <ontology/Storage.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

void test_satisfiable_atomic() {
    TEST("Atomic class is satisfiable");
    DlReasoner reasoner;
    ASSERT_TRUE(reasoner.isSatisfiable(ClassExpression::atomic("Person")));
    PASS();
}

void test_unsatisfiable_bottom() {
    TEST("Bottom is unsatisfiable");
    DlReasoner reasoner;
    ASSERT_TRUE(!reasoner.isSatisfiable(ClassExpression::bottom()));
    PASS();
}

void test_satisfiable_top() {
    TEST("Top is satisfiable");
    DlReasoner reasoner;
    ASSERT_TRUE(reasoner.isSatisfiable(ClassExpression::top()));
    PASS();
}

void test_unsatisfiable_contradiction() {
    TEST("A ⊓ ¬A is unsatisfiable");
    DlReasoner reasoner;
    auto aAndNotA = ClassExpression::intersection({
        ClassExpression::atomic("Cat"),
        ClassExpression::complement(ClassExpression::atomic("Cat"))
    });
    ASSERT_TRUE(!reasoner.isSatisfiable(aAndNotA));
    PASS();
}

void test_satisfiable_intersection() {
    TEST("A ⊓ B is satisfiable when A and B are not disjoint");
    DlReasoner reasoner;
    auto aAndB = ClassExpression::intersection({
        ClassExpression::atomic("Dog"),
        ClassExpression::atomic("Pet")
    });
    ASSERT_TRUE(reasoner.isSatisfiable(aAndB));
    PASS();
}

void test_unsatisfiable_disjoint_intersection() {
    TEST("A ⊓ B is unsatisfiable when A and B are disjoint");
    DlReasoner reasoner;
    reasoner.addDisjointClasses(ClassExpression::atomic("Cat"),
                                ClassExpression::atomic("Dog"));
    auto catAndDog = ClassExpression::intersection({
        ClassExpression::atomic("Cat"),
        ClassExpression::atomic("Dog")
    });
    ASSERT_TRUE(!reasoner.isSatisfiable(catAndDog));
    PASS();
}

void test_subsumption_with_tbox() {
    TEST("Subsumption: Dog ⊑ Animal via TBox");
    DlReasoner reasoner;
    reasoner.addSubClassOf(ClassExpression::atomic("Dog"),
                           ClassExpression::atomic("Animal"));
    ASSERT_TRUE(reasoner.isSubsumedBy(ClassExpression::atomic("Dog"),
                                      ClassExpression::atomic("Animal")));
    ASSERT_TRUE(!reasoner.isSubsumedBy(ClassExpression::atomic("Animal"),
                                       ClassExpression::atomic("Dog")));
    PASS();
}

void test_equivalence() {
    TEST("Equivalence: Person ≡ Human");
    DlReasoner reasoner;
    reasoner.addEquivalentClasses(ClassExpression::atomic("Person"),
                                  ClassExpression::atomic("Human"));
    ASSERT_TRUE(reasoner.isEquivalent(ClassExpression::atomic("Person"),
                                      ClassExpression::atomic("Human")));
    PASS();
}

void test_consistency() {
    TEST("Consistent ABox");
    DlReasoner reasoner;
    reasoner.addConceptAssertion("alice", ClassExpression::atomic("Person"));
    reasoner.addConceptAssertion("bob", ClassExpression::atomic("Dog"));
    ASSERT_TRUE(reasoner.isConsistent());
    PASS();
}

void test_inconsistent_abox() {
    TEST("Inconsistent ABox (disjoint class assertions)");
    DlReasoner reasoner;
    reasoner.addDisjointClasses(ClassExpression::atomic("Cat"),
                                ClassExpression::atomic("Dog"));
    reasoner.addConceptAssertion("fluffy", ClassExpression::atomic("Cat"));
    reasoner.addConceptAssertion("fluffy", ClassExpression::atomic("Dog"));
    ASSERT_TRUE(!reasoner.isConsistent());
    PASS();
}

void test_classify() {
    TEST("Classification produces hierarchy");
    DlReasoner reasoner;
    reasoner.addSubClassOf(ClassExpression::atomic("Dog"),
                           ClassExpression::atomic("Animal"));
    reasoner.addSubClassOf(ClassExpression::atomic("Cat"),
                           ClassExpression::atomic("Animal"));
    auto hierarchy = reasoner.classify();
    ASSERT_TRUE(hierarchy.count("Dog") > 0);
    ASSERT_TRUE(hierarchy.count("Cat") > 0);
    // Dog ⊑ Animal should appear
    bool foundDogAnimal = false;
    for (const auto& sup : hierarchy["Dog"]) {
        if (sup == "Animal") foundDogAnimal = true;
    }
    ASSERT_TRUE(foundDogAnimal);
    PASS();
}

void test_load_from_triplestore() {
    TEST("Load TBox from TripleStore");
    auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
    auto* ts = storage->getTripleStore();
    ts->add({"Dog", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    ts->add({"Cat", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    ts->add({"Cat", "http://www.w3.org/2002/07/owl#disjointWith", "Dog"});

    DlReasoner reasoner;
    reasoner.loadFromTripleStore(ts);

    ASSERT_TRUE(reasoner.isSubsumedBy(ClassExpression::atomic("Dog"),
                                      ClassExpression::atomic("Animal")));
    auto catAndDog = ClassExpression::intersection({
        ClassExpression::atomic("Cat"),
        ClassExpression::atomic("Dog")
    });
    ASSERT_TRUE(!reasoner.isSatisfiable(catAndDog));
    PASS();
}

void test_existential_rule() {
    TEST("∃R.A is satisfiable");
    DlReasoner reasoner;
    auto existsRA = ClassExpression::someValuesFrom("hasPart",
        ClassExpression::atomic("Wheel"));
    ASSERT_TRUE(reasoner.isSatisfiable(existsRA));
    PASS();
}

void test_universal_rule() {
    TEST("∀R.⊥ with ∃R.⊤ is unsatisfiable");
    DlReasoner reasoner;
    // ∀R.⊥ ⊓ ∃R.⊤ — all R-successors are Bottom, but there exists an R-successor
    auto allBottom = ClassExpression::allValuesFrom("hasPart", ClassExpression::bottom());
    auto existsTop = ClassExpression::someValuesFrom("hasPart", ClassExpression::top());
    auto both = ClassExpression::intersection({allBottom, existsTop});
    ASSERT_TRUE(!reasoner.isSatisfiable(both));
    PASS();
}

int main() {
    test_satisfiable_atomic();
    test_unsatisfiable_bottom();
    test_satisfiable_top();
    test_unsatisfiable_contradiction();
    test_satisfiable_intersection();
    test_unsatisfiable_disjoint_intersection();
    test_subsumption_with_tbox();
    test_equivalence();
    test_consistency();
    test_inconsistent_abox();
    test_classify();
    test_load_from_triplestore();
    test_existential_rule();
    test_universal_rule();

    std::cout << "\nALC Reasoner tests: " << testsPassed << " passed, "
              << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
```

- [ ] **Step 2: Add test target to CMakeLists.txt**

Append:
```cmake
# 单元测试: ALC Description Logic Reasoner
add_executable(test_alc_reasoner test_alc_reasoner.cpp)
target_link_libraries(test_alc_reasoner PRIVATE ontology_core ${APPLE_FRAMEWORKS})
add_test(NAME alc_reasoner COMMAND test_alc_reasoner)
```

- [ ] **Step 3: Build and test**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && cmake .. -DBUILD_TESTS=ON && make test_alc_reasoner && ./tests/test_alc_reasoner`

Expected: All 14 tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/test_alc_reasoner.cpp tests/CMakeLists.txt
git commit -m "test(dl): add ALC reasoner unit tests — satisfiability, subsumption, equivalence, consistency, classification"
```

---

### Task 6: Integrate DlReasoner with HybridReasoner

**Files:**
- Modify: `include/ontology/Inference.hpp`
- Modify: `src/inference/HybridReasoner.cpp`

- [ ] **Step 1: Add DlReasoner pointer to HybridReasoner**

In `include/ontology/Inference.hpp`, add forward declaration before HybridReasoner class:

```cpp
class DlReasoner;
```

Add to HybridReasoner's public interface:

```cpp
    void setDlReasoner(DlReasoner* reasoner) { dlReasoner_ = reasoner; }
    DlReasoner* getDlReasoner() const { return dlReasoner_; }
```

Add to HybridReasoner's private members:

```cpp
    DlReasoner* dlReasoner_ = nullptr;
```

- [ ] **Step 2: Enhance detectContradictions to use DlReasoner**

In `src/inference/HybridReasoner.cpp`, modify `detectContradictions()`:

Find the existing method that delegates to `symbolic_->checkConsistency()`. Replace it with:

```cpp
std::vector<Contradiction> HybridReasoner::detectContradictions() {
    std::vector<Contradiction> contradictions;

    // Use DlReasoner for consistency if available
    if (dlReasoner_) {
        // Load current TBox from storage
        if (hybridStorage_) {
            dlReasoner_->loadFromTripleStore(hybridStorage_->getTripleStore());
        }

        if (!dlReasoner_->isConsistent()) {
            contradictions.push_back({"TBox/ABox inconsistency detected by DL reasoner", 1.0f});
        }
    }

    // Also check with symbolic reasoner for ABox-level conflicts
    if (config_.enableSymbolic && symbolic_) {
        auto conflicts = symbolic_->checkConsistency();
        for (const auto& c : conflicts) {
            contradictions.push_back({c.description, 1.0f});
        }
    }

    return contradictions;
}
```

- [ ] **Step 3: Build and test**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && make test_hybrid_reasoner test_alc_reasoner && ./tests/test_hybrid_reasoner && ./tests/test_alc_reasoner`

Expected: Both test suites pass.

- [ ] **Step 4: Commit**

```bash
git add include/ontology/Inference.hpp src/inference/HybridReasoner.cpp
git commit -m "feat(dl): integrate DlReasoner with HybridReasoner for TBox-aware consistency checking"
```

---

### Task 7: Full Build Verification

**Files:** None (verification only)

- [ ] **Step 1: Full build**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && cmake .. -DBUILD_TESTS=ON && make -j$(sysctl -n hw.ncpu)`

Expected: Build succeeds.

- [ ] **Step 2: Run all tests**

Run: `cd /Users/kankan/claude-code/ontology-platform/build && ctest --output-on-failure`

Expected: All 17+ tests pass (16 existing + new alc_reasoner).

- [ ] **Step 3: Fix any issues and commit if needed**
