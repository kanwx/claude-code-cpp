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

    // Satisfiability helper (with backtracking for disjunctions)
    bool isSatisfiableHelper(std::vector<TableauxNode> nodes);
};

} // namespace ontology
