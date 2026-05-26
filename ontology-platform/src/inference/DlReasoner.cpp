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
    for (const auto& other : nodes) {
        if (other.name == node.name) continue;
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
    auto& expr = node.concepts[conceptIdx];
    if (expr->type != ExpressionType::Intersection) return false;

    bool changed = false;
    for (const auto& op : expr->operands) {
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
    // Disjunction is handled directly in isSatisfiableHelper via backtracking
    return false;
}

bool DlReasoner::applyExistentialRule(std::vector<TableauxNode>& nodes,
                                       TableauxNode& node, size_t conceptIdx)
{
    auto& expr = node.concepts[conceptIdx];
    if (expr->type != ExpressionType::ObjectSomeValuesFrom) return false;
    if (!expr->filler) return false;
    if (node.isBlocked) return false;

    // Check if there's already an R-successor with the filler
    for (const auto& [prop, target] : node.successors) {
        if (prop == expr->property) {
            auto* targetNode = findNode(nodes, target);
            if (targetNode && targetNode->hasConcept(*expr->filler)) {
                return false;  // already exists
            }
        }
    }

    // Create new individual
    String newName = freshName();
    TableauxNode newNode;
    newNode.name = newName;

    // Add filler concept
    newNode.concepts.push_back(expr->filler);

    // Add internalized TBox concept
    if (internalizedTBoxConcept_) {
        newNode.concepts.push_back(internalizedTBoxConcept_);
    }

    // Add role assertion
    node.successors.push_back({expr->property, newName});
    newNode.predecessors.push_back({expr->property, node.name});

    nodes.push_back(std::move(newNode));
    return true;
}

bool DlReasoner::applyUniversalRule(std::vector<TableauxNode>& nodes,
                                     TableauxNode& node, size_t conceptIdx)
{
    auto& expr = node.concepts[conceptIdx];
    if (expr->type != ExpressionType::ObjectAllValuesFrom) return false;
    if (!expr->filler) return false;

    bool changed = false;
    for (const auto& [prop, target] : node.successors) {
        if (prop == expr->property) {
            auto* targetNode = findNode(nodes, target);
            if (targetNode && !targetNode->hasConcept(*expr->filler)) {
                targetNode->concepts.push_back(expr->filler);
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

    for (const auto& node : nodes) {
        if (node.hasClash()) return true;
    }
    return false;
}

// ============================================================================
// Reasoning services
// ============================================================================

bool DlReasoner::isSatisfiableHelper(std::vector<TableauxNode> nodes) {
    bool hasClash = expand(nodes);
    if (hasClash) return false;

    // Find an unexpanded disjunction
    for (auto& node : nodes) {
        if (node.isBlocked) continue;
        for (size_t i = 0; i < node.concepts.size(); ++i) {
            if (node.concepts[i]->type == ExpressionType::Union) {
                auto& disj = node.concepts[i];
                for (const auto& disjunct : disj->operands) {
                    auto branchNodes = nodes;
                    auto* branchNode = findNode(branchNodes, node.name);
                    if (branchNode) {
                        branchNode->concepts.push_back(disjunct);
                        if (isSatisfiableHelper(std::move(branchNodes))) {
                            return true;
                        }
                    }
                }
                return false;  // all branches lead to clash
            }
        }
    }

    return true;  // no unexpanded disjunctions and no clashes
}

bool DlReasoner::isSatisfiable(ClassExpressionPtr conceptExpr) {
    auto nnf = ClassExpressionEvaluator::toNNF(*conceptExpr);

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

bool DlReasoner::isSubsumedBy(ClassExpressionPtr sub, ClassExpressionPtr sup) {
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
    std::vector<TableauxNode> nodes;

    std::unordered_set<String> individuals;
    for (const auto& ca : aboxConcepts_) individuals.insert(ca.individual);
    for (const auto& ra : aboxRoles_) {
        individuals.insert(ra.subject);
        individuals.insert(ra.object);
    }

    for (const auto& ind : individuals) {
        TableauxNode node;
        node.name = ind;
        for (const auto& ca : aboxConcepts_) {
            if (ca.individual == ind) {
                auto nnf = ClassExpressionEvaluator::toNNF(*ca.conceptExpr);
                node.concepts.push_back(nnf);
            }
        }
        if (internalizedTBoxConcept_) {
            node.concepts.push_back(
                ClassExpressionEvaluator::toNNF(*internalizedTBoxConcept_));
        }
        for (const auto& ra : aboxRoles_) {
            if (ra.subject == ind) {
                node.successors.push_back({ra.property, ra.object});
            }
        }
        nodes.push_back(std::move(node));
    }

    for (const auto& node : nodes) {
        if (node.hasClash()) return false;
    }

    return isSatisfiableHelper(nodes);
}

std::unordered_map<String, std::vector<String>> DlReasoner::classify() {
    std::unordered_map<String, std::vector<String>> hierarchy;

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

    std::unordered_set<String> asserted;
    for (const auto& ca : aboxConcepts_) {
        if (ca.individual == individual && ca.conceptExpr->type == ExpressionType::Atomic) {
            asserted.insert(ca.conceptExpr->className);
        }
    }

    for (const auto& cls : namedClasses_) {
        if (asserted.count(cls)) {
            types.push_back(cls);
            continue;
        }
        auto savedConcepts = aboxConcepts_;
        addConceptAssertion(individual, ClassExpression::atomic(cls));
        bool consistent = isConsistent();
        aboxConcepts_ = std::move(savedConcepts);

        if (consistent) {
            types.push_back(cls);
        }
    }

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

} // namespace ontology
