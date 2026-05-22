#include <ontology/SwrlBackwardChainer.hpp>
#include <ontology/Storage.hpp>
#include <algorithm>
#include <sstream>

namespace ontology {

// ============================================================================
// SwrlBackwardChainer Implementation
// ============================================================================

SwrlBackwardChainer::SwrlBackwardChainer(StoragePtr storage)
    : storage_(std::move(storage)) {}

void SwrlBackwardChainer::setRules(std::vector<SwrlRule>& rules) {
    rules_ = &rules;
}

// ============================================================================
// Public entry points
// ============================================================================

Bindings SwrlBackwardChainer::prove(const std::vector<SwrlAtom>& goal, int maxDepth) {
    if (goal.empty()) return {{}};
    Binding initial;
    return proveAllAtoms(goal, initial, maxDepth);
}

ProofNode SwrlBackwardChainer::buildProofTree(
    const std::vector<SwrlAtom>& goal, int maxDepth) {
    ProofNode root;
    root.proven = false;

    if (goal.empty()) {
        root.proven = true;
        return root;
    }

    // Build a combined proof tree for the conjunction of goal atoms
    Binding binding;
    bool allProven = true;

    for (const auto& atom : goal) {
        auto subNode = proveAtomWithTree(atom, binding, maxDepth);
        if (!subNode.proven) {
            allProven = false;
        }

        // Accumulate bindings from proven sub-goals
        if (subNode.proven) {
            for (const auto& [k, v] : subNode.bindings) {
                binding[k] = v;
            }
        }

        root.subGoals.push_back(std::move(subNode));
    }

    root.proven = allProven;
    root.bindings = binding;
    return root;
}

// ============================================================================
// Core recursive proof
// ============================================================================

Bindings SwrlBackwardChainer::proveAtom(const SwrlAtom& goal, const Binding& initial, int depth) {
    Bindings results;

    if (depth <= 0) return results;

    // 1. Try matching against facts in storage
    SwrlAtom resolved = resolveAtom(goal, initial);
    if (matchesFact(resolved, initial)) {
        // If the resolved atom has no variables, we have a direct fact match
        if (!resolved.hasVariables()) {
            results.push_back(initial);
        } else {
            // The atom still has unresolved variables — collect bindings from storage
            // We treat it like a query: find all bindings that make this atom true
            auto factBindings = queryFactBindings(resolved, initial);
            for (auto& b : factBindings) {
                // Merge with initial binding
                for (const auto& [k, v] : initial) {
                    if (b.find(k) == b.end()) {
                        b[k] = v;
                    }
                }
                results.push_back(std::move(b));
            }
        }
    }

    // 2. Try matching against rule heads
    if (!rules_) return results;

    auto matchingRules = findMatchingRules(goal);
    for (auto* rule : matchingRules) {
        if (!rule || !rule->enabled) continue;

        // For each head atom in the rule, try to unify with the goal
        for (const auto& headAtom : rule->head) {
            Binding unified = initial;
            if (!unify(goal, headAtom, unified)) continue;

            // Now prove all body atoms with the unified binding
            auto bodyBindings = proveAllAtoms(rule->body, unified, depth - 1);

            for (auto& b : bodyBindings) {
                // Make sure the goal's variables are bound in the result
                results.push_back(std::move(b));
            }
        }
    }

    return results;
}

Bindings SwrlBackwardChainer::proveAllAtoms(
    const std::vector<SwrlAtom>& goals,
    const Binding& initial,
    int depth) {
    Bindings solutions;
    solutions.push_back(initial);

    for (const auto& atom : goals) {
        Bindings newSolutions;

        for (const auto& binding : solutions) {
            auto matches = proveAtom(atom, binding, depth);
            newSolutions.insert(
                newSolutions.end(),
                std::make_move_iterator(matches.begin()),
                std::make_move_iterator(matches.end()));
        }

        solutions = std::move(newSolutions);
        if (solutions.empty()) break;
    }

    return solutions;
}

// ============================================================================
// Unification
// ============================================================================

bool SwrlBackwardChainer::unify(
    const SwrlAtom& goal, const SwrlAtom& head, Binding& binding) {
    // Type must match (or be compatible)
    if (goal.type != head.type) return false;

    switch (goal.type) {
        case SwrlAtomType::ClassAtom: {
            // classId must match
            if (goal.classId != head.classId) return false;
            // Unify arguments
            return unifyArgs(goal.classArgument, head.classArgument, binding);
        }

        case SwrlAtomType::ObjectPropertyAtom:
        case SwrlAtomType::DataPropertyAtom: {
            // propertyId must match
            if (goal.propertyId != head.propertyId) return false;
            // Unify both argument positions
            if (!unifyArgs(goal.argument1, head.argument1, binding)) return false;
            if (!unifyArgs(goal.argument2, head.argument2, binding)) return false;
            return true;
        }

        case SwrlAtomType::BuiltInAtom: {
            // BuiltIn atoms: name must match and args unify
            if (goal.builtInName != head.builtInName) return false;
            if (goal.builtInArgs.size() != head.builtInArgs.size()) return false;
            for (size_t i = 0; i < goal.builtInArgs.size(); ++i) {
                if (!unifyArgs(goal.builtInArgs[i], head.builtInArgs[i], binding)) {
                    return false;
                }
            }
            return true;
        }

        default:
            return false;
    }
}

bool SwrlBackwardChainer::unifyArgs(
    const String& goalArg, const String& headArg, Binding& binding) {
    bool goalIsVar = isVariable(goalArg);
    bool headIsVar = isVariable(headArg);

    if (goalIsVar && headIsVar) {
        // Both variables: unify them
        String goalVar = goalArg.substr(1);
        String headVar = headArg.substr(1);

        auto goalIt = binding.find(goalVar);
        auto headIt = binding.find(headVar);

        if (goalIt != binding.end() && headIt != binding.end()) {
            // Both already bound — must agree
            return goalIt->second == headIt->second;
        } else if (goalIt != binding.end()) {
            // Goal var already bound, bind head var to same value
            binding[headVar] = goalIt->second;
        } else if (headIt != binding.end()) {
            // Head var already bound, bind goal var to same value
            binding[goalVar] = headIt->second;
        } else {
            // Neither bound: link them (bind goal var to head var name)
            binding[goalVar] = headArg;
        }
        return true;
    }

    if (goalIsVar) {
        // Goal is variable, head is constant
        String goalVar = goalArg.substr(1);
        auto it = binding.find(goalVar);
        if (it != binding.end()) {
            // Already bound — must match head constant
            return resolveArg(it->second, binding) == resolveArg(headArg, binding);
        }
        // Bind goal variable to head constant
        binding[goalVar] = headArg;
        return true;
    }

    if (headIsVar) {
        // Head is variable, goal is constant
        String headVar = headArg.substr(1);
        auto it = binding.find(headVar);
        if (it != binding.end()) {
            // Already bound — must match goal constant
            return resolveArg(it->second, binding) == resolveArg(goalArg, binding);
        }
        // Bind head variable to goal constant
        binding[headVar] = goalArg;
        return true;
    }

    // Both constants — must be equal
    return goalArg == headArg;
}

// ============================================================================
// Variable resolution
// ============================================================================

String SwrlBackwardChainer::resolveArg(const String& arg, const Binding& binding) const {
    if (!isVariable(arg)) return arg;

    String varName = arg.substr(1);
    auto it = binding.find(varName);
    if (it == binding.end()) return arg; // Unbound variable stays as-is

    // Recursively resolve if bound value is also a variable
    String resolved = it->second;
    int guard = 20; // Prevent infinite loops
    while (isVariable(resolved) && guard-- > 0) {
        String nextVar = resolved.substr(1);
        auto nextIt = binding.find(nextVar);
        if (nextIt == binding.end()) break;
        resolved = nextIt->second;
    }

    return resolved;
}

SwrlAtom SwrlBackwardChainer::resolveAtom(const SwrlAtom& atom, const Binding& binding) const {
    SwrlAtom resolved = atom;

    switch (atom.type) {
        case SwrlAtomType::ClassAtom:
            resolved.classArgument = resolveArg(atom.classArgument, binding);
            break;

        case SwrlAtomType::ObjectPropertyAtom:
        case SwrlAtomType::DataPropertyAtom:
            resolved.argument1 = resolveArg(atom.argument1, binding);
            resolved.argument2 = resolveArg(atom.argument2, binding);
            break;

        case SwrlAtomType::BuiltInAtom:
            for (size_t i = 0; i < atom.builtInArgs.size(); ++i) {
                resolved.builtInArgs[i] = resolveArg(atom.builtInArgs[i], binding);
            }
            break;

        default:
            break;
    }

    return resolved;
}

// ============================================================================
// Fact matching
// ============================================================================

bool SwrlBackwardChainer::matchesFact(const SwrlAtom& atom, const Binding& binding) {
    SwrlAtom resolved = resolveAtom(atom, binding);

    switch (resolved.type) {
        case SwrlAtomType::ClassAtom: {
            String arg = resolved.classArgument;
            if (isVariable(arg)) {
                // Unbound variable — check if any individual exists for this class
                auto individuals = storage_->getIndividualsByClass(resolved.classId);
                return !individuals.empty();
            }
            // Constant: check if individual belongs to class
            auto ind = storage_->getIndividual(arg);
            return ind.has_value() && ind->classId == resolved.classId;
        }

        case SwrlAtomType::ObjectPropertyAtom: {
            String arg1 = resolved.argument1;
            String arg2 = resolved.argument2;

            bool var1 = isVariable(arg1);
            bool var2 = isVariable(arg2);

            if (!var1 && !var2) {
                // Both bound — check if triple exists
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{arg1, resolved.propertyId, arg2, false, false, false});
                return !t.empty();
            }
            if (!var1) {
                // arg1 bound, arg2 unbound
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{arg1, resolved.propertyId, "", false, false, true});
                return !t.empty();
            }
            if (!var2) {
                // arg2 bound, arg1 unbound
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{"", resolved.propertyId, arg2, true, false, false});
                return !t.empty();
            }
            // Both unbound
            auto t = storage_->queryTriples(
                TripleStore::TriplePattern{"", resolved.propertyId, "", true, false, true});
            return !t.empty();
        }

        case SwrlAtomType::DataPropertyAtom: {
            String arg1 = resolved.argument1;
            String arg2 = resolved.argument2;

            bool var1 = isVariable(arg1);
            bool var2 = isVariable(arg2);

            if (!var1 && !var2) {
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{arg1, resolved.propertyId, arg2, false, false, false});
                return !t.empty();
            }
            if (!var1) {
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{arg1, resolved.propertyId, "", false, false, true});
                return !t.empty();
            }
            if (!var2) {
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{"", resolved.propertyId, arg2, true, false, false});
                return !t.empty();
            }
            auto t = storage_->queryTriples(
                TripleStore::TriplePattern{"", resolved.propertyId, "", true, false, true});
            return !t.empty();
        }

        case SwrlAtomType::BuiltInAtom: {
            // Resolve all arguments
            std::vector<String> args;
            for (const auto& a : resolved.builtInArgs) {
                args.push_back(resolveArg(a, binding));
            }
            // Check if all args are resolved (no remaining variables)
            for (const auto& a : args) {
                if (isVariable(a)) return false;
            }
            std::vector<String> results;
            return SwrlBuiltIns::execute(resolved.builtInName, args, results);
        }

        default:
            return false;
    }
}

Bindings SwrlBackwardChainer::queryFactBindings(
    const SwrlAtom& atom, const Binding& initial) {
    Bindings results;
    SwrlAtom resolved = resolveAtom(atom, initial);

    switch (resolved.type) {
        case SwrlAtomType::ClassAtom: {
            String arg = resolved.classArgument;
            if (isVariable(arg)) {
                String varName = arg.substr(1);
                auto individuals = storage_->getIndividualsByClass(resolved.classId);
                for (const auto& ind : individuals) {
                    auto b = initial;
                    b[varName] = ind.id;
                    results.push_back(std::move(b));
                }
            } else {
                auto ind = storage_->getIndividual(arg);
                if (ind && ind->classId == resolved.classId) {
                    results.push_back(initial);
                }
            }
            break;
        }

        case SwrlAtomType::ObjectPropertyAtom: {
            String arg1 = resolved.argument1;
            String arg2 = resolved.argument2;
            bool var1 = isVariable(arg1);
            bool var2 = isVariable(arg2);
            String varName1 = var1 ? arg1.substr(1) : "";
            String varName2 = var2 ? arg2.substr(1) : "";

            if (!var1 && !var2) {
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{arg1, resolved.propertyId, arg2, false, false, false});
                if (!t.empty()) results.push_back(initial);
            } else if (!var1) {
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{arg1, resolved.propertyId, "", false, false, true});
                for (const auto& tr : t) {
                    auto b = initial;
                    b[varName2] = tr.object;
                    results.push_back(std::move(b));
                }
            } else if (!var2) {
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{"", resolved.propertyId, arg2, true, false, false});
                for (const auto& tr : t) {
                    auto b = initial;
                    b[varName1] = tr.subject;
                    results.push_back(std::move(b));
                }
            } else {
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{"", resolved.propertyId, "", true, false, true});
                for (const auto& tr : t) {
                    auto b = initial;
                    b[varName1] = tr.subject;
                    b[varName2] = tr.object;
                    results.push_back(std::move(b));
                }
            }
            break;
        }

        case SwrlAtomType::DataPropertyAtom: {
            String arg1 = resolved.argument1;
            String arg2 = resolved.argument2;
            bool var1 = isVariable(arg1);
            bool var2 = isVariable(arg2);
            String varName1 = var1 ? arg1.substr(1) : "";
            String varName2 = var2 ? arg2.substr(1) : "";

            if (!var1 && !var2) {
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{arg1, resolved.propertyId, arg2, false, false, false});
                if (!t.empty()) results.push_back(initial);
            } else if (!var1) {
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{arg1, resolved.propertyId, "", false, false, true});
                for (const auto& tr : t) {
                    auto b = initial;
                    b[varName2] = tr.object;
                    results.push_back(std::move(b));
                }
            } else if (!var2) {
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{"", resolved.propertyId, arg2, true, false, false});
                for (const auto& tr : t) {
                    auto b = initial;
                    b[varName1] = tr.subject;
                    results.push_back(std::move(b));
                }
            } else {
                auto t = storage_->queryTriples(
                    TripleStore::TriplePattern{"", resolved.propertyId, "", true, false, true});
                for (const auto& tr : t) {
                    auto b = initial;
                    b[varName1] = tr.subject;
                    b[varName2] = tr.object;
                    results.push_back(std::move(b));
                }
            }
            break;
        }

        case SwrlAtomType::BuiltInAtom: {
            std::vector<String> args;
            bool allResolved = true;
            for (const auto& a : resolved.builtInArgs) {
                if (isVariable(a)) { allResolved = false; break; }
                args.push_back(a);
            }
            if (allResolved) {
                std::vector<String> execResults;
                if (SwrlBuiltIns::execute(resolved.builtInName, args, execResults)) {
                    results.push_back(initial);
                }
            }
            break;
        }

        default:
            break;
    }

    return results;
}

// ============================================================================
// Rule matching
// ============================================================================

std::vector<SwrlRule*> SwrlBackwardChainer::findMatchingRules(const SwrlAtom& goal) {
    std::vector<SwrlRule*> result;

    if (!rules_) return result;

    for (auto& rule : *rules_) {
        if (!rule.enabled) continue;

        for (const auto& headAtom : rule.head) {
            bool match = false;

            switch (goal.type) {
                case SwrlAtomType::ClassAtom:
                    match = (headAtom.type == SwrlAtomType::ClassAtom &&
                             headAtom.classId == goal.classId);
                    break;

                case SwrlAtomType::ObjectPropertyAtom:
                    match = (headAtom.type == SwrlAtomType::ObjectPropertyAtom &&
                             headAtom.propertyId == goal.propertyId);
                    break;

                case SwrlAtomType::DataPropertyAtom:
                    match = (headAtom.type == SwrlAtomType::DataPropertyAtom &&
                             headAtom.propertyId == goal.propertyId);
                    break;

                case SwrlAtomType::BuiltInAtom:
                    match = (headAtom.type == SwrlAtomType::BuiltInAtom &&
                             headAtom.builtInName == goal.builtInName);
                    break;

                default:
                    break;
            }

            if (match) {
                result.push_back(&rule);
                break; // Only add each rule once
            }
        }
    }

    return result;
}

// ============================================================================
// Proof tree construction
// ============================================================================

ProofNode SwrlBackwardChainer::proveAtomWithTree(
    const SwrlAtom& goal, Binding& binding, int depth) {
    ProofNode node;
    node.goal = goal;
    node.proven = false;

    if (depth <= 0) return node;

    // 1. Try matching against facts
    SwrlAtom resolved = resolveAtom(goal, binding);
    if (matchesFact(resolved, binding)) {
        // Collect fact bindings
        auto factBindings = queryFactBindings(resolved, binding);
        if (!factBindings.empty()) {
            // Use the first successful binding
            node.proven = true;
            node.matchedRule = nullptr; // Fact, not rule
            node.bindings = factBindings[0];

            // Merge into the passed-in binding
            for (const auto& [k, v] : node.bindings) {
                binding[k] = v;
            }
            return node;
        }
    }

    // 2. Try matching against rule heads
    if (!rules_) return node;

    auto matchingRules = findMatchingRules(goal);
    for (auto* rule : matchingRules) {
        if (!rule || !rule->enabled) continue;

        for (const auto& headAtom : rule->head) {
            Binding unified = binding;
            if (!unify(goal, headAtom, unified)) continue;

            // Prove all body atoms, building sub-trees
            bool allProved = true;
            std::vector<ProofNode> subGoals;

            Binding bodyBinding = unified;
            for (const auto& bodyAtom : rule->body) {
                auto subNode = proveAtomWithTree(bodyAtom, bodyBinding, depth - 1);
                if (!subNode.proven) {
                    allProved = false;
                    break;
                }

                // Accumulate bindings
                for (const auto& [k, v] : subNode.bindings) {
                    bodyBinding[k] = v;
                }
                subGoals.push_back(std::move(subNode));
            }

            if (allProved) {
                node.proven = true;
                node.matchedRule = rule;
                node.bindings = bodyBinding;
                node.subGoals = std::move(subGoals);

                // Merge into passed-in binding
                for (const auto& [k, v] : node.bindings) {
                    binding[k] = v;
                }
                return node;
            }
        }
    }

    return node;
}

// ============================================================================
// ProofNode::toJson
// ============================================================================

Json ProofNode::toJson() const {
    Json j;
    j["goal"] = goal.toString();
    j["proven"] = proven;

    if (matchedRule) {
        j["rule"] = matchedRule->name.empty() ? matchedRule->id : matchedRule->name;
    }

    if (!bindings.empty()) {
        Json b = Json::object();
        for (const auto& [k, v] : bindings) {
            // Only include non-variable bindings (resolved values)
            if (!v.empty() && v[0] != '?') {
                b[k] = v;
            }
        }
        if (!b.empty()) {
            j["bindings"] = b;
        }
    }

    if (!subGoals.empty()) {
        j["subGoals"] = Json::array();
        for (const auto& sub : subGoals) {
            j["subGoals"].push_back(sub.toJson());
        }
    }

    return j;
}

// ============================================================================
// Static helpers
// ============================================================================

bool SwrlBackwardChainer::isVariable(const String& arg) {
    return !arg.empty() && arg[0] == '?';
}

} // namespace ontology
