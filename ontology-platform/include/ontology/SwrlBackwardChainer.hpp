#pragma once

#include "Swrl.hpp"
#include <vector>
#include <unordered_map>

namespace ontology {

// ProofNode is defined in Swrl.hpp to avoid circular dependencies

class SwrlBackwardChainer {
public:
    explicit SwrlBackwardChainer(StoragePtr storage);

    /// Prove a goal: find all variable bindings that satisfy the goal atoms
    Bindings prove(const std::vector<SwrlAtom>& goal, int maxDepth = 10);

    /// Build a proof tree for explanation
    ProofNode buildProofTree(const std::vector<SwrlAtom>& goal, int maxDepth = 10);

    /// Set the rules to use for backward chaining
    void setRules(std::vector<SwrlRule>& rules);

private:
    StoragePtr storage_;
    std::vector<SwrlRule>* rules_ = nullptr;

    // Unification: try to match goal atom against rule head or fact
    bool unify(const SwrlAtom& goal, const SwrlAtom& head, Binding& binding);

    // Resolve variables in an atom using current binding
    SwrlAtom resolveAtom(const SwrlAtom& atom, const Binding& binding) const;

    // Resolve a single argument (variable or constant)
    String resolveArg(const String& arg, const Binding& binding) const;

    // Recursive proof — find all bindings that prove a single atom
    Bindings proveAtom(const SwrlAtom& goal, const Binding& initial, int depth);

    // Prove all atoms in a list (conjunction)
    Bindings proveAllAtoms(const std::vector<SwrlAtom>& goals, const Binding& initial, int depth);

    // Build proof tree for a single atom
    ProofNode proveAtomWithTree(const SwrlAtom& goal, Binding& binding, int depth);

    // Check if atom matches a fact in storage
    bool matchesFact(const SwrlAtom& atom, const Binding& binding);

    // Find rules whose head might match the goal
    std::vector<SwrlRule*> findMatchingRules(const SwrlAtom& goal);

    // Query fact bindings: find all bindings from storage that satisfy the atom
    Bindings queryFactBindings(const SwrlAtom& atom, const Binding& initial);

    // Unification helper for two arguments
    bool unifyArgs(const String& goalArg, const String& headArg, Binding& binding);

    // Check if an argument is a variable
    static bool isVariable(const String& arg);
};

} // namespace ontology
