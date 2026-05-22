#include <ontology/sparql/SparqlExecutor.hpp>
#include <ontology/sparql/SparqlParser.hpp>
#include <ontology/Temporal.hpp>
#include <ontology/Storage.hpp>
#include <chrono>
#include <unordered_set>
#include <queue>
#include <sstream>
#include <algorithm>

namespace ontology::sparql {

// ============================================================================
// Construction and factory methods
// ============================================================================

SparqlExecutor::SparqlExecutor(TripleSource source)
    : source_(std::move(source)) {}

SparqlExecutor::TripleSource SparqlExecutor::fromTripleStore(ontology::TripleStore& store) {
    return [&store](const String& s, const String& p, const String& o) -> std::vector<ontology::Triple> {
        if (!s.empty() && !p.empty() && !o.empty()) {
            auto t = store.find(s, p, o);
            if (t) return {*t};
            return {};
        }
        if (!s.empty() && !p.empty()) {
            return store.findBySP(s, p);
        }
        if (!p.empty() && !o.empty()) {
            return store.findByPO(p, o);
        }
        if (!s.empty()) {
            return store.findBySubject(s);
        }
        if (!p.empty()) {
            return store.findByPredicate(p);
        }
        // All empty — return all triples
        return store.all();
    };
}

SparqlExecutor::TripleSource SparqlExecutor::fromHybridStorage(ontology::HybridStorage& storage) {
    return [&storage](const String& s, const String& p, const String& o) -> std::vector<ontology::Triple> {
        if (!s.empty() && !p.empty() && !o.empty()) {
            auto t = storage.findTriple(s, p, o);
            if (t) return {*t};
            return {};
        }
        if (!s.empty() && !p.empty()) {
            return storage.findBySP(s, p);
        }
        if (!p.empty() && !o.empty()) {
            return storage.findByPO(p, o);
        }
        if (!s.empty()) {
            return storage.findBySubject(s);
        }
        if (!p.empty()) {
            return storage.findByPredicate(p);
        }
        return storage.getAllTriples();
    };
}

void SparqlExecutor::setEnableInference(bool enable) {
    enableInference_ = enable;
}

void SparqlExecutor::setMaxInferenceDepth(int depth) {
    maxInferenceDepth_ = depth;
}

// ============================================================================
// Top-level execute
// ============================================================================

QueryResult SparqlExecutor::execute(const Query& query) {
    auto start = std::chrono::high_resolution_clock::now();

    QueryResult result;
    result.queryType = queryType(query);

    try {
        std::visit([&](const auto& q) {
            using T = std::decay_t<decltype(q)>;
            if constexpr (std::is_same_v<T, SelectQuery>) {
                result = executeSelect(q);
            } else if constexpr (std::is_same_v<T, AskQuery>) {
                result = executeAsk(q);
            } else if constexpr (std::is_same_v<T, ConstructQuery>) {
                result = executeConstruct(q);
            } else if constexpr (std::is_same_v<T, DescribeQuery>) {
                result = executeDescribe(q);
            }
        }, query);
    } catch (const std::exception& e) {
        result.errorMessage = e.what();
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.executionTime = std::chrono::duration<float>(end - start).count();
    return result;
}

QueryResult SparqlExecutor::execute(const String& sparqlString) {
    SparqlParser parser;
    auto query = parser.parse(sparqlString);
    if (!query) {
        QueryResult result;
        result.errorMessage = "Failed to parse query";
        if (!parser.lastError().empty()) {
            result.errorMessage += ": " + parser.lastError();
        }
        return result;
    }
    return execute(*query);
}

// ============================================================================
// SELECT execution
// ============================================================================

QueryResult SparqlExecutor::executeSelect(const SelectQuery& query) {
    QueryResult result;
    result.queryType = SparqlQueryType::SELECT;

    // Collect all variables
    result.variables = allVariables(Query{query});

    // Pattern matching
    result.rows = matchGroupPattern(query.where);

    // Temporal filtering
    result.rows = applyTemporalFilter(query.where, result.rows);

    // Filters
    if (!query.where.filters.empty()) {
        result.rows = applyFilters(result.rows, query.where.filters);
    }

    // Optional groups
    for (const auto& opt : query.where.optionalGroups) {
        result.rows = matchOptional(opt, result.rows);
    }

    // Union groups (handled inside matchGroupPattern, but if present at top
    // level they are already merged)

    // GROUP BY / HAVING
    if (!query.groupBy.empty()) {
        result.rows = applyGroupBy(result.rows, query.groupBy, query.having);
        // Update variables to include group-by and aggregate variables
        result.variables = query.groupBy;
        for (const auto& var : query.variables) {
            if (std::find(query.groupBy.begin(), query.groupBy.end(), var.name) == query.groupBy.end()) {
                result.variables.push_back(var.name);
            }
        }
    }

    // ORDER BY
    if (!query.orderBy.empty()) {
        applyOrderBy(result.rows, query.orderBy);
    }

    // OFFSET
    if (query.offset > 0 && query.offset < (int)result.rows.size()) {
        result.rows.erase(result.rows.begin(), result.rows.begin() + query.offset);
    }

    // LIMIT
    if (query.limit > 0 && (int)result.rows.size() > query.limit) {
        result.rows.resize(query.limit);
    }

    result.resultCount = result.rows.size();
    return result;
}

// ============================================================================
// ASK execution
// ============================================================================

QueryResult SparqlExecutor::executeAsk(const AskQuery& query) {
    QueryResult result;
    result.queryType = SparqlQueryType::ASK;

    auto bindings = matchGroupPattern(query.where);
    bindings = applyTemporalFilter(query.where, bindings);
    result.askResult = !bindings.empty();

    result.resultCount = bindings.size();
    return result;
}

// ============================================================================
// CONSTRUCT execution
// ============================================================================

QueryResult SparqlExecutor::executeConstruct(const ConstructQuery& query) {
    QueryResult result;
    result.queryType = SparqlQueryType::CONSTRUCT;

    auto bindings = matchGroupPattern(query.where);
    bindings = applyTemporalFilter(query.where, bindings);

    // Apply filters from the where pattern
    if (!query.where.filters.empty()) {
        bindings = applyFilters(bindings, query.where.filters);
    }

    // Construct triples from the template
    for (const auto& binding : bindings) {
        for (const auto& templatePattern : query.constructTemplate) {
            TriplePattern tp;
            tp.subject = templatePattern.subject;
            tp.predicate = templatePattern.predicate;
            tp.object = templatePattern.object;

            // Replace variables in the template with bound values
            if (tp.subject.isVariable()) {
                auto it = binding.find(tp.subject.value);
                if (it != binding.end()) {
                    tp.subject = TriplePattern::iri(it->second.is_string()
                        ? it->second.get<String>() : it->second.dump());
                }
            }
            if (tp.predicate.isVariable()) {
                auto it = binding.find(tp.predicate.value);
                if (it != binding.end()) {
                    tp.predicate = TriplePattern::iri(it->second.is_string()
                        ? it->second.get<String>() : it->second.dump());
                }
            }
            if (tp.object.isVariable()) {
                auto it = binding.find(tp.object.value);
                if (it != binding.end()) {
                    // Preserve literal vs IRI distinction
                    if (it->second.is_string()) {
                        tp.object = TriplePattern::literal(it->second.get<String>());
                    } else {
                        tp.object = TriplePattern::literal(it->second.dump());
                    }
                }
            }

            // Only add if all positions are non-empty and non-variable
            if (!tp.subject.isVariable() && !tp.predicate.isVariable() && !tp.object.isVariable()) {
                result.constructTriples.push_back(tp);
            }
        }
    }

    result.resultCount = result.constructTriples.size();
    return result;
}

// ============================================================================
// DESCRIBE execution
// ============================================================================

QueryResult SparqlExecutor::executeDescribe(const DescribeQuery& query) {
    QueryResult result;
    result.queryType = SparqlQueryType::DESCRIBE;

    // Collect resources to describe from variables and explicit IRIs
    std::unordered_set<String> resources;

    // First, match the where pattern to get variable bindings
    auto bindings = matchGroupPattern(query.where);
    bindings = applyTemporalFilter(query.where, bindings);

    if (!query.where.filters.empty()) {
        bindings = applyFilters(bindings, query.where.filters);
    }

    // Collect resource IRIs from bindings for the described variables
    for (const auto& var : query.variables) {
        for (const auto& binding : bindings) {
            auto it = binding.find(var.name);
            if (it != binding.end() && it->second.is_string()) {
                resources.insert(it->second.get<String>());
            }
        }
    }

    // Also collect non-variable resources from the where pattern triples
    for (const auto& tp : query.where.triples) {
        if (!tp.subject.isVariable()) resources.insert(tp.subject.value);
        if (!tp.predicate.isVariable()) resources.insert(tp.predicate.value);
        if (!tp.object.isVariable() && tp.object.isIRI()) resources.insert(tp.object.value);
    }

    // Describe each resource: find all triples where it appears as subject or object
    for (const auto& resource : resources) {
        auto asSubject = source_(resource, "", "");
        for (const auto& t : asSubject) {
            TriplePattern tp;
            tp.subject = TriplePattern::iri(t.subject);
            tp.predicate = TriplePattern::iri(t.predicate);
            if (t.isLiteral) {
                tp.object = TriplePattern::literal(t.object);
            } else {
                tp.object = TriplePattern::iri(t.object);
            }
            result.constructTriples.push_back(tp);
        }

        // Also find triples where resource is object (reverse relations)
        // This requires scanning — use source_ with empty s and p
        // For efficiency, we skip reverse scan if there are many resources
    }

    // Deduplicate
    std::unordered_set<String> seen;
    std::vector<TriplePattern> unique;
    for (const auto& tp : result.constructTriples) {
        String key = tp.subject.value + "|" + tp.predicate.value + "|" + tp.object.value;
        if (seen.insert(key).second) {
            unique.push_back(tp);
        }
    }
    result.constructTriples = std::move(unique);

    result.resultCount = result.constructTriples.size();
    return result;
}

// ============================================================================
// Core pattern matching
// ============================================================================

std::vector<SparqlExecutor::Binding> SparqlExecutor::matchGroupPattern(
    const GroupPattern& pattern)
{
    std::vector<Binding> results;

    // UNION handling: if union groups exist, match each and merge
    if (!pattern.unionGroups.empty()) {
        results = matchUnion(pattern.unionGroups);
    } else {
        // Match triples (join)
        results = joinPatterns(pattern.triples);
    }

    // Apply filters
    if (!pattern.filters.empty()) {
        results = applyFilters(results, pattern.filters);
    }

    // Apply optional groups
    for (const auto& opt : pattern.optionalGroups) {
        results = matchOptional(opt, results);
    }

    // Temporal filtering is done separately via applyTemporalFilter

    return results;
}

std::vector<SparqlExecutor::Binding> SparqlExecutor::matchTriplePattern(
    const TriplePattern& tp,
    const Binding& initial)
{
    std::vector<Binding> results;

    // Resolve variables from initial bindings
    String subject, predicate, object;

    if (tp.subject.isVariable()) {
        auto it = initial.find(tp.subject.value);
        if (it != initial.end()) subject = it->second.is_string()
            ? it->second.get<String>() : it->second.dump();
    } else {
        subject = tp.subject.value;
    }

    if (tp.predicate.isVariable()) {
        auto it = initial.find(tp.predicate.value);
        if (it != initial.end()) predicate = it->second.is_string()
            ? it->second.get<String>() : it->second.dump();
    } else {
        predicate = tp.predicate.value;
    }

    if (tp.object.isVariable()) {
        auto it = initial.find(tp.object.value);
        if (it != initial.end()) object = it->second.is_string()
            ? it->second.get<String>() : it->second.dump();
    } else {
        object = tp.object.value;
    }

    // Query the TripleSource
    auto triples = source_(subject, predicate, object);

    // Generate bindings
    for (const auto& triple : triples) {
        Binding binding;

        if (tp.subject.isVariable() && subject.empty()) {
            binding[tp.subject.value] = triple.subject;
        }
        if (tp.predicate.isVariable() && predicate.empty()) {
            binding[tp.predicate.value] = triple.predicate;
        }
        if (tp.object.isVariable() && object.empty()) {
            binding[tp.object.value] = triple.object;
        }

        if (!binding.empty()) {
            results.push_back(binding);
        }
    }

    return results;
}

std::vector<SparqlExecutor::Binding> SparqlExecutor::joinPatterns(
    const std::vector<TriplePattern>& patterns,
    const Binding& initial)
{
    std::vector<Binding> solutions;
    solutions.push_back(initial);

    for (const auto& pattern : patterns) {
        std::vector<Binding> newSolutions;

        for (const auto& binding : solutions) {
            auto matches = matchTriplePattern(pattern, binding);
            for (auto& match : matches) {
                // Merge bindings
                auto merged = binding;
                for (const auto& [var, val] : match) {
                    merged[var] = val;
                }
                newSolutions.push_back(merged);
            }
        }

        solutions = std::move(newSolutions);
        if (solutions.empty()) break;
    }

    return solutions;
}

std::vector<SparqlExecutor::Binding> SparqlExecutor::matchOptional(
    const GroupPattern& optional,
    const std::vector<Binding>& bindings)
{
    std::vector<Binding> result;

    for (const auto& binding : bindings) {
        // Try to match the optional pattern with current bindings
        auto optBindings = matchGroupPattern(optional);

        // Filter optBindings that are compatible with current binding
        std::vector<Binding> compatible;
        for (const auto& optBind : optBindings) {
            bool compat = true;
            for (const auto& [var, val] : binding) {
                auto it = optBind.find(var);
                if (it != optBind.end() && it->second != val) {
                    compat = false;
                    break;
                }
            }
            if (compat) {
                compatible.push_back(optBind);
            }
        }

        if (!compatible.empty()) {
            // Optional matched: extend current binding
            for (const auto& optBind : compatible) {
                auto merged = binding;
                for (const auto& [var, val] : optBind) {
                    if (merged.find(var) == merged.end()) {
                        merged[var] = val;
                    }
                }
                result.push_back(merged);
            }
        } else {
            // Optional did not match: keep original binding (unbound optional vars)
            result.push_back(binding);
        }
    }

    return result;
}

std::vector<SparqlExecutor::Binding> SparqlExecutor::matchUnion(
    const std::vector<GroupPattern>& unions)
{
    std::vector<Binding> allSolutions;
    std::unordered_set<String> seen;

    for (const auto& group : unions) {
        auto solutions = matchGroupPattern(group);
        for (const auto& sol : solutions) {
            // Deduplicate by serialized binding
            String key;
            for (const auto& [k, v] : sol) {
                key += k + "=" + v.dump() + ";";
            }
            if (seen.insert(key).second) {
                allSolutions.push_back(sol);
            }
        }
    }

    return allSolutions;
}

// ============================================================================
// Filter evaluation
// ============================================================================

std::vector<SparqlExecutor::Binding> SparqlExecutor::applyFilters(
    const std::vector<Binding>& bindings,
    const std::vector<std::shared_ptr<FilterExpression>>& filters)
{
    std::vector<Binding> filtered;

    for (const auto& binding : bindings) {
        bool passes = true;
        for (const auto& filter : filters) {
            auto result = filter->evaluate(binding);
            if (!result || !result->get<bool>()) {
                passes = false;
                break;
            }
        }
        if (passes) {
            filtered.push_back(binding);
        }
    }

    return filtered;
}

// ============================================================================
// Aggregation
// ============================================================================

std::vector<SparqlExecutor::Binding> SparqlExecutor::applyGroupBy(
    const std::vector<Binding>& bindings,
    const std::vector<String>& groupBy,
    const std::shared_ptr<FilterExpression>& having)
{
    // Group
    std::unordered_map<String, std::vector<Binding>> groups;

    for (const auto& solution : bindings) {
        std::ostringstream key;
        for (const auto& var : groupBy) {
            auto it = solution.find(var);
            if (it != solution.end()) {
                key << it->second.dump() << "|";
            } else {
                key << "null|";
            }
        }
        groups[key.str()].push_back(solution);
    }

    // Compute aggregates per group
    std::vector<Binding> result;

    for (const auto& [key, group] : groups) {
        if (group.empty()) continue;

        Binding groupedSolution;

        // Add group-by variable values
        for (const auto& var : groupBy) {
            auto it = group[0].find(var);
            if (it != group[0].end()) {
                groupedSolution[var] = it->second;
            }
        }

        // COUNT
        groupedSolution["__count__"] = Json(static_cast<int>(group.size()));

        // SUM, AVG, MIN, MAX for numeric variables
        std::unordered_map<String, double> sums, mins, maxs;
        std::unordered_map<String, bool> numeric;

        for (const auto& sol : group) {
            for (const auto& [var, val] : sol) {
                if (val.is_number()) {
                    numeric[var] = true;
                    double v = val.get<double>();
                    sums[var] += v;
                    if (mins.find(var) == mins.end() || v < mins[var]) mins[var] = v;
                    if (maxs.find(var) == maxs.end() || v > maxs[var]) maxs[var] = v;
                }
            }
        }

        for (const auto& [var, isNum] : numeric) {
            if (isNum) {
                groupedSolution["__sum_" + var + "__"] = Json(sums[var]);
                groupedSolution["__avg_" + var + "__"] = Json(sums[var] / group.size());
                groupedSolution["__min_" + var + "__"] = Json(mins[var]);
                groupedSolution["__max_" + var + "__"] = Json(maxs[var]);
            }
        }

        // HAVING
        if (having) {
            auto pass = having->evaluate(groupedSolution);
            if (!pass || !pass->get<bool>()) {
                continue;
            }
        }

        result.push_back(groupedSolution);
    }

    return result;
}

// ============================================================================
// Sorting and pagination
// ============================================================================

void SparqlExecutor::applyOrderBy(
    std::vector<Binding>& bindings,
    const std::vector<std::pair<String, bool>>& orderBy)
{
    std::sort(bindings.begin(), bindings.end(), [&](const auto& a, const auto& b) {
        for (const auto& [var, ascending] : orderBy) {
            auto itA = a.find(var);
            auto itB = b.find(var);

            if (itA == a.end() && itB == b.end()) continue;
            if (itA == a.end()) return ascending;
            if (itB == b.end()) return !ascending;

            int cmp = 0;
            if (itA->second.is_string() && itB->second.is_string()) {
                cmp = itA->second.template get<String>().compare(itB->second.template get<String>());
            } else if (itA->second.is_number() && itB->second.is_number()) {
                double va = itA->second.template get<double>();
                double vb = itB->second.template get<double>();
                cmp = (va < vb) ? -1 : (va > vb) ? 1 : 0;
            }
            if (cmp != 0) {
                return ascending ? cmp < 0 : cmp > 0;
            }
        }
        return false;
    });
}

void SparqlExecutor::applyLimitOffset(
    std::vector<Binding>& bindings,
    int limit,
    int offset)
{
    if (offset > 0 && offset < (int)bindings.size()) {
        bindings.erase(bindings.begin(), bindings.begin() + offset);
    }
    if (limit > 0 && (int)bindings.size() > limit) {
        bindings.resize(limit);
    }
}

// ============================================================================
// Temporal filtering
// ============================================================================

std::vector<SparqlExecutor::Binding> SparqlExecutor::applyTemporalFilter(
    const GroupPattern& pattern,
    const std::vector<Binding>& bindings)
{
    if (pattern.validAtTimestamp.empty() && pattern.validBetweenFrom.empty()) {
        return bindings;
    }

    std::vector<Binding> filtered;

    for (const auto& binding : bindings) {
        bool valid = false;

        // Reconstruct triple from binding to check temporal validity
        for (const auto& tp : pattern.triples) {
            String subj, pred, obj;

            if (tp.subject.isVariable()) {
                auto it = binding.find(tp.subject.value);
                subj = it != binding.end() && it->second.is_string()
                    ? it->second.get<String>() : "";
            } else {
                subj = tp.subject.value;
            }

            if (tp.predicate.isVariable()) {
                auto it = binding.find(tp.predicate.value);
                pred = it != binding.end() && it->second.is_string()
                    ? it->second.get<String>() : "";
            } else {
                pred = tp.predicate.value;
            }

            if (tp.object.isVariable()) {
                auto it = binding.find(tp.object.value);
                obj = it != binding.end() && it->second.is_string()
                    ? it->second.get<String>() : "";
            } else {
                obj = tp.object.value;
            }

            if (!pattern.validAtTimestamp.empty()) {
                // VALID_AT: check if triple is valid at the given timestamp
                if (!subj.empty() && !pred.empty() && !obj.empty()) {
                    auto triples = source_(subj, pred, obj);
                    for (const auto& t : triples) {
                        if (ontology::isValidAt(t.validFrom, t.validTo, pattern.validAtTimestamp)) {
                            valid = true;
                            break;
                        }
                    }
                } else if (!subj.empty() && !pred.empty()) {
                    auto triples = source_(subj, pred, "");
                    for (const auto& t : triples) {
                        if (ontology::isValidAt(t.validFrom, t.validTo, pattern.validAtTimestamp)) {
                            valid = true;
                            break;
                        }
                    }
                } else if (!pred.empty() && !obj.empty()) {
                    auto triples = source_("", pred, obj);
                    for (const auto& t : triples) {
                        if (ontology::isValidAt(t.validFrom, t.validTo, pattern.validAtTimestamp)) {
                            valid = true;
                            break;
                        }
                    }
                } else {
                    valid = true; // Broad pattern: accept
                }
            } else if (!pattern.validBetweenFrom.empty() && !pattern.validBetweenTo.empty()) {
                // VALID_BETWEEN: check if triple overlaps [from, to]
                int64_t fromMs = ontology::isoToEpochMs(pattern.validBetweenFrom);
                int64_t toMs = ontology::isoToEpochMs(pattern.validBetweenTo);

                auto overlaps = [&](const Triple& t) -> bool {
                    if (t.validFrom.empty() && t.validTo.empty()) return true;
                    int64_t tFrom = t.validFrom.empty() ? 0 : ontology::isoToEpochMs(t.validFrom);
                    int64_t tTo = t.validTo.empty() ? INT64_MAX : ontology::isoToEpochMs(t.validTo);
                    return tFrom <= toMs && tTo >= fromMs;
                };

                if (!subj.empty() && !pred.empty() && !obj.empty()) {
                    auto triples = source_(subj, pred, obj);
                    for (const auto& t : triples) {
                        if (overlaps(t)) { valid = true; break; }
                    }
                } else if (!subj.empty() && !pred.empty()) {
                    auto triples = source_(subj, pred, "");
                    for (const auto& t : triples) {
                        if (overlaps(t)) { valid = true; break; }
                    }
                } else if (!pred.empty() && !obj.empty()) {
                    auto triples = source_("", pred, obj);
                    for (const auto& t : triples) {
                        if (overlaps(t)) { valid = true; break; }
                    }
                } else {
                    valid = true;
                }
            }

            if (valid) break;
        }

        if (valid) {
            filtered.push_back(binding);
        }
    }

    return filtered;
}

// ============================================================================
// Property path evaluation
// ============================================================================

std::vector<String> SparqlExecutor::evaluatePropertyPath(
    const String& start,
    const String& path,
    int maxDepth)
{
    std::vector<String> results;
    std::unordered_set<String> visited;

    // Sequence path: pred1/pred2
    if (path.find('/') != String::npos) {
        std::vector<String> segments;
        std::istringstream iss(path);
        String segment;
        while (std::getline(iss, segment, '/')) {
            segments.push_back(segment);
        }

        std::vector<String> current = {start};
        for (const auto& seg : segments) {
            std::vector<String> next;
            for (const auto& node : current) {
                auto reachable = evaluatePropertyPath(node, seg, maxDepth);
                next.insert(next.end(), reachable.begin(), reachable.end());
            }
            current = std::move(next);
        }
        return current;
    }

    // Alternative path: pred1|pred2
    if (path.find('|') != String::npos) {
        std::vector<String> alternatives;
        std::istringstream iss(path);
        String alt;
        while (std::getline(iss, alt, '|')) {
            alternatives.push_back(alt);
        }

        for (const auto& a : alternatives) {
            auto reachable = evaluatePropertyPath(start, a, maxDepth);
            for (const auto& r : reachable) {
                if (visited.insert(r).second) {
                    results.push_back(r);
                }
            }
        }
        return results;
    }

    // Transitive closure
    bool transitive = false;
    bool includeSelf = false;
    String pred = path;

    if (path.size() >= 1 && path.back() == '+') {
        transitive = true;
        pred = path.substr(0, path.size() - 1);
    } else if (path.size() >= 1 && path.back() == '*') {
        transitive = true;
        includeSelf = true;
        pred = path.substr(0, path.size() - 1);
    }

    // Inverse path
    bool inverse = false;
    if (!pred.empty() && pred[0] == '^') {
        inverse = true;
        pred = pred.substr(1);
    }

    if (!transitive) {
        // Simple path
        if (inverse) {
            auto triples = source_("", pred, start);
            for (const auto& t : triples) {
                results.push_back(t.subject);
            }
        } else {
            auto triples = source_(start, pred, "");
            for (const auto& t : triples) {
                results.push_back(t.object);
            }
        }
    } else {
        // Transitive closure (BFS)
        std::queue<std::pair<String, int>> queue;
        queue.push({start, 0});
        visited.insert(start);

        if (includeSelf) {
            results.push_back(start);
        }

        while (!queue.empty()) {
            auto [node, depth] = queue.front();
            queue.pop();

            if (depth >= maxDepth) continue;

            std::vector<Triple> nextTriples;
            if (inverse) {
                nextTriples = source_("", pred, node);
            } else {
                nextTriples = source_(node, pred, "");
            }

            for (const auto& t : nextTriples) {
                String next = inverse ? t.subject : t.object;
                if (visited.insert(next).second) {
                    results.push_back(next);
                    queue.push({next, depth + 1});
                }
            }
        }
    }

    return results;
}

} // namespace ontology::sparql
