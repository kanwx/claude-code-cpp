#include <ontology/DlReasoner.hpp>
#include <ontology/Storage.hpp>
#include <algorithm>
#include <queue>

namespace ontology {

// ============================================================================
// TableauxNode helpers
// ============================================================================

bool TableauxNode::hasConcept(const ClassExpression& expr) const {
    for (const auto& c : concepts) {
        if (c->type == expr.type && c->className == expr.className
            && c->property == expr.property
            && c->cardinality == expr.cardinality) {
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
    addSubClassOf(c1, c2);
    addSubClassOf(c2, c1);
}

void DlReasoner::addDisjointClasses(ClassExpressionPtr c1, ClassExpressionPtr c2) {
    auto intersection = ClassExpression::intersection({c1, c2});
    addSubClassOf(intersection, ClassExpression::bottom());
}

void DlReasoner::loadFromTripleStore(TripleStore* store) {
    if (!store) return;

    auto subClassTriples = store->findByPredicate(
        "http://www.w3.org/2000/01/rdf-schema#subClassOf");
    for (const auto& t : subClassTriples) {
        addSubClassOf(ClassExpression::atomic(t.subject),
                      ClassExpression::atomic(t.object));
    }

    auto equivTriples = store->findByPredicate(
        "http://www.w3.org/2002/07/owl#equivalentClass");
    for (const auto& t : equivTriples) {
        addEquivalentClasses(ClassExpression::atomic(t.subject),
                             ClassExpression::atomic(t.object));
    }

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

void DlReasoner::addConceptAssertion(const String& individual, ClassExpressionPtr conceptExpr) {
    ConceptAssertion ca{individual, conceptExpr};
    aboxConcepts_.push_back(std::move(ca));
    if (conceptExpr->type == ExpressionType::Atomic) namedClasses_.insert(conceptExpr->className);
}

void DlReasoner::addRoleAssertion(const String& subject, const String& property, const String& object) {
    aboxRoles_.push_back({subject, property, object});
}

// ============================================================================
// TBox internalization
// ============================================================================

void DlReasoner::internalizeTBox() {
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

// ============================================================================
// Stub implementations for reasoning services (to be implemented in next task)
// ============================================================================

bool DlReasoner::isSatisfiable(ClassExpressionPtr conceptExpr) {
    return true; // stub
}

bool DlReasoner::isSubsumedBy(ClassExpressionPtr sub, ClassExpressionPtr sup) {
    return false; // stub
}

bool DlReasoner::isEquivalent(ClassExpressionPtr c1, ClassExpressionPtr c2) {
    return false; // stub
}

bool DlReasoner::isConsistent() {
    return true; // stub
}

std::unordered_map<String, std::vector<String>> DlReasoner::classify() {
    return {}; // stub
}

std::vector<String> DlReasoner::realize(const String& individual) {
    return {}; // stub
}

bool DlReasoner::expand(std::vector<TableauxNode>& nodes) {
    return false; // stub
}

bool DlReasoner::applyConjunctionRule(std::vector<TableauxNode>& nodes, TableauxNode& node, size_t conceptIdx) {
    return false; // stub
}

bool DlReasoner::applyDisjunctionRule(std::vector<TableauxNode>& nodes, TableauxNode& node, size_t conceptIdx) {
    return false; // stub
}

bool DlReasoner::applyExistentialRule(std::vector<TableauxNode>& nodes, TableauxNode& node, size_t conceptIdx) {
    return false; // stub
}

bool DlReasoner::applyUniversalRule(std::vector<TableauxNode>& nodes, TableauxNode& node, size_t conceptIdx) {
    return false; // stub
}

bool DlReasoner::isBlocked(const std::vector<TableauxNode>& nodes, const TableauxNode& node) const {
    return false; // stub
}

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

bool DlReasoner::isSatisfiableHelper(std::vector<TableauxNode> nodes) {
    return true; // stub
}

} // namespace ontology
