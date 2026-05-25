# Inference Refinement Design — Phase 2 Completion

**Date:** 2026-05-25
**Branch:** feat/phase2-inference-refinement

## Overview

Complete the three remaining gap areas in the inference layer: ClassExpression TBox-aware reasoning, SPARQL named graph update operations, and AutoModel logic fixes.

---

## 1. ClassExpression TBox-Aware Reasoning

### Problem

`ClassExpressionEvaluator` methods (`isSubsumedBy`, `isEquivalent`, `isEmpty`, `isUniversal`) only handle basic type checks. They cannot reason over complex expressions using TBox axioms (`rdfs:subClassOf`, `owl:disjointWith`, `owl:equivalentClass`). `FunctionalSyntaxParser` splits by whitespace and cannot handle nested brackets.

### Design

**ClassExpressionEvaluator** gains a `TripleStore*` constructor parameter. All TBox-aware methods query the store for axioms.

#### isSubsumedBy(A, B)

Recursive structural decomposition with TBox lookup:

| Expression A | Condition for A ⊑ B |
|---|---|
| Named class C | C == B, or ∃ C rdfs:subClassOf B (transitive closure), or ∃ C owl:equivalentClass B |
| Intersection(X,Y) | X ⊑ B ∧ Y ⊑ B |
| Union(X,Y) | X ⊑ B ∨ Y ⊑ B |
| Complement(X) | X is disjoint with B |
| Some(R,C) | B is Some(R,D) where C ⊑ D |
| All(R,C) | B is All(R,D) where D ⊑ C (contravariant) |
| MinCard(n,R) | B is MinCard(m,R) where m ≤ n |
| HasValue(R,v) | B is HasValue(R,v) |

#### isEquivalent(A, B)

A ≡ B iff A ⊑ B ∧ B ⊑ A. Plus direct `owl:equivalentClass` axiom check.

#### isEmpty(A)

| Expression A | Condition for A = ∅ |
|---|---|
| Named class C | C == owl:Nothing |
| Intersection(X, Complement(Y)) | X ⊑ Y |
| Intersection(X, Y) | X and Y are disjoint (owl:disjointWith) |
| Complement(X) | X is universal |
| All(R,C) | C is empty |

#### isUniversal(A)

A = ⊤ iff A == owl:Thing, or A = Complement(X) where X is empty.

#### FunctionalSyntaxParser

Replace whitespace-split with recursive descent parser:
- Tokenize: handle `(`, `)`, keywords (ObjectSomeValuesFrom, ObjectIntersectionOf, etc.), full IRIs `<...>`, and quoted strings.
- Parse: each token keyword dispatches to a parsing function that consumes `(`, sub-expressions, `)`.
- Build ClassExpression tree with proper nesting.

### Files Changed

- `include/ontology/ClassExpression.hpp` — add TripleStore* to ClassExpressionEvaluator constructor
- `src/owl/ExpressionParser.cpp` — rewrite isSubsumedBy/isEquivalent/isEmpty/isUniversal, rewrite FunctionalSyntaxParser::parse()
- `src/owl/ClassExpression.cpp` — minor adjustments if isEquivalent signature changes

---

## 2. SPARQL Named Graph Operations

### Problem

6 named graph update operations in `SparqlEndpoint::executeUpdate()` are no-ops: Drop, Create, Load, Copy, Move, Add.

### Design

#### Storage Model

Add `std::map<std::string, TripleStore> namedGraphs_` to `SparqlEndpoint`. The default graph is the main TripleStore passed at construction. Named graphs are separate TripleStore instances keyed by IRI.

Accessor methods:
- `getNamedGraph(const string& iri)` → `TripleStore*` (nullptr if missing)
- `listNamedGraphs()` → `vector<string>` of IRIs
- `hasNamedGraph(const string& iri)` → `bool`

#### Operation Implementations

| Op | Behavior |
|---|---|
| **CREATE** `<iri>` | `namedGraphs_[iri]` = empty TripleStore. No-op if already exists (unless SILENT). |
| **DROP** `<iri>` | `namedGraphs_.erase(iri)`. SILENT = no error if missing. DROP DEFAULT = clear main store. |
| **LOAD** `<remote_iri>` INTO `<graph_iri>` | Record source IRI. Fetch via HTTP GET + OntologyIO::parseRdfXml/Turtle. Insert triples into named graph. On parse failure, SILENT skips. |
| **COPY** `<src>` TO `<dest>` | Deep-copy src TripleStore into dest (replacing dest entirely). |
| **MOVE** `<src>` TO `<dest>` | COPY then DROP src. |
| **ADD** `<src>` TO `<dest>` | Merge: iterate src triples, insert into dest. Additive, not replacing. |

#### GRAPH Clause in INSERT/DELETE

When a `GRAPH <iri>` clause appears in INSERT DATA / DELETE DATA / DELETE WHERE, the triples are applied to the named graph instead of the default store. Parse the `graphIri` field already present in `SparqlUpdate` and dispatch accordingly.

### Files Changed

- `include/ontology/sparql/SparqlEndpoint.hpp` — add `namedGraphs_` map, accessors, graph-aware execute helpers
- `src/sparql/SparqlEndpoint.cpp` — implement 6 operations, extend INSERT/DELETE for GRAPH clause

---

## 3. AutoModel Fixes

### Problem

Three methods have incomplete logic:

1. `RuleGenerator::foilAlgorithm()` produces identity rules instead of real FOIL-induced rules
2. `AutoModelEngine::resolveConflict()` calls LLM but doesn't execute the suggested resolution
3. `AutoModelEngine::importAndLearn()` iterates discovered rules but has empty loop body

### Design

#### foilAlgorithm() Fix

Implement proper FOIL specialization loop:

1. Start with empty body (most general rule covering all positive examples).
2. At each step, find the atom that maximizes foil_gain:
   - `p1` = positive bindings satisfying current body + candidate atom
   - `n1` = negative bindings satisfying current body + candidate atom
   - `p0`, `n0` = counts before adding candidate
   - `gain = p1 * (log2(p1/(p1+n1)) - log2(p0/(p0+n0)))`
3. Add the best-gain atom to the body.
4. Repeat until all positives covered or gain < threshold.
5. Use `TripleStore::query()` for binding enumeration.

#### resolveConflict() Fix

1. After LLM call, parse response for action directives: `REMOVE_TRIPLE(s,p,o)`, `ADD_TRIPLE(s,p,o)`, `MODIFY_CLASS(name,property,value)`.
2. Execute parsed actions against the engine's TripleStore.
3. Add `bool dryRun = false` parameter. When true, return parsed actions without executing.
4. Log each action taken.

#### importAndLearn() Fix

1. In the loop over `tryInduction()` results, call `SwrlEngine::addRule()` for each discovered rule.
2. Log rule additions.
3. Also add discovered entities/relations to the ontology.

### Files Changed

- `src/inference/AutoModel.cpp` — fix all three methods
- `include/ontology/AutoModel.hpp` — add `dryRun` parameter to resolveConflict, possibly add action parsing helpers

---

## Implementation Order

1. **ClassExpression** — foundational; other components depend on proper class reasoning
2. **SPARQL named graphs** — self-contained storage + operation work
3. **AutoModel fixes** — depends on ClassExpression improvements for conflict detection

## Testing

Each area gets dedicated unit tests:

- **ClassExpression**: test TBox-aware subsumption, equivalence, emptiness, universality with sample ontologies containing subClassOf and disjointWith axioms. Test FunctionalSyntaxParser with nested expressions.
- **SPARQL named graphs**: test CREATE/DROP/LOAD/COPY/MOVE/ADD operations. Test GRAPH clause in INSERT/DELETE. Test SILENT flag.
- **AutoModel**: test foilAlgorithm produces non-trivial rules on sample data. Test resolveConflict executes changes. Test importAndLearn adds discovered rules.
