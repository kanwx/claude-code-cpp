#include <ontology/Inference.hpp>
#include <algorithm>
#include <sstream>
#include <regex>
#include <chrono>

namespace ontology {

// ============================================================================
// SymbolicReasoner 实现 - 符号推理 (左脑)
// ============================================================================

SymbolicReasoner::SymbolicReasoner(StoragePtr storage) : storage_(storage) {
}

void SymbolicReasoner::addAxiom(const Axiom& axiom) {
    axioms_[axiom.id] = axiom;
}

void SymbolicReasoner::removeAxiom(const String& axiomId) {
    axioms_.erase(axiomId);
}

// ============================================================================
// 类型推理
// ============================================================================

std::vector<String> SymbolicReasoner::getTypes(const String& individualId) {
    std::vector<String> types;

    auto ind = storage_->getIndividual(individualId);
    if (!ind) return types;

    // 直接类型
    types.push_back(ind->classId);

    // 推理超类
    auto superClasses = getSuperClasses(ind->classId);
    for (const auto& super : superClasses) {
        if (std::find(types.begin(), types.end(), super) == types.end()) {
            types.push_back(super);
        }
    }

    return types;
}

std::vector<String> SymbolicReasoner::getSuperClasses(const String& classId) {
    std::vector<String> supers;

    std::function<void(const String&)> collect = [&](const String& cid) {
        auto cls = storage_->getClass(cid);
        if (cls) {
            for (const auto& super : cls->superClasses) {
                if (std::find(supers.begin(), supers.end(), super) == supers.end()) {
                    supers.push_back(super);
                    collect(super);
                }
            }
        }
    };

    collect(classId);
    return supers;
}

std::vector<String> SymbolicReasoner::getSubClasses(const String& classId) {
    return storage_->getAllSubClasses(classId);
}

bool SymbolicReasoner::isInstanceOf(const String& individualId, const String& classId) {
    auto types = getTypes(individualId);
    return std::find(types.begin(), types.end(), classId) != types.end();
}

bool SymbolicReasoner::isSubClassOf(const String& subClass, const String& superClass) {
    auto supers = getSuperClasses(subClass);
    return std::find(supers.begin(), supers.end(), superClass) != supers.end();
}

// ============================================================================
// 关系推理
// ============================================================================

std::vector<Triple> SymbolicReasoner::getRelated(const String& subject, const String& relation) {
    TripleStore::TriplePattern pattern;
    pattern.subject = subject;
    pattern.subjectIsVar = subject.empty();
    pattern.predicate = relation;
    pattern.predicateIsVar = relation.empty();
    pattern.objectIsVar = true;  // object is always a variable in "get related"
    return storage_->queryTriples(pattern);
}

std::vector<String> SymbolicReasoner::getRelatedObjects(const String& subject, const String& relation) {
    std::vector<String> objects;
    auto triples = getRelated(subject, relation);
    for (const auto& t : triples) {
        objects.push_back(t.object);
    }
    return objects;
}

std::vector<String> SymbolicReasoner::getRelatedSubjects(const String& object, const String& relation) {
    TripleStore::TriplePattern pattern;
    pattern.subjectIsVar = true;  // subject is always a variable in "get related subjects"
    pattern.predicate = relation;
    pattern.predicateIsVar = relation.empty();
    pattern.object = object;
    pattern.objectIsVar = object.empty();

    auto triples = storage_->queryTriples(pattern);
    std::vector<String> subjects;
    for (const auto& t : triples) {
        subjects.push_back(t.subject);
    }
    return subjects;
}

std::vector<String> SymbolicReasoner::inferTransitive(const String& subject, const String& relation) {
    std::vector<String> result;
    std::unordered_set<String> visited;
    auto startTime = std::chrono::steady_clock::now();

    std::function<void(const String&)> collect = [&](const String& s) {
        if (visited.count(s)) return;
        visited.insert(s);

        auto objects = getRelatedObjects(s, relation);
        for (const auto& obj : objects) {
            if (std::find(result.begin(), result.end(), obj) == result.end()) {
                result.push_back(obj);

                // Record trace step
                if (explainabilityEngine_ && explainabilityEngine_->currentTrace()) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - startTime).count();
                    InferenceStep step;
                    step.source = "symbolic";
                    step.type = "transitive_inference";
                    step.ruleId = relation;
                    step.outputConfidence = 0.9f;
                    step.durationMs = elapsed;
                    step.detail = "Transitive: " + s + " -> " + obj + " via " + relation;
                    step.inputFactIds.push_back(s + ":" + relation);
                    Triple inferredTriple;
                    inferredTriple.subject = s;
                    inferredTriple.predicate = relation;
                    inferredTriple.object = obj;
                    inferredTriple.confidence = 0.9f;
                    explainabilityEngine_->currentTrace()->addStep(step, {inferredTriple});
                }

                collect(obj);
            }
        }
    };

    collect(subject);
    return result;
}

std::vector<String> SymbolicReasoner::inferSymmetric(const String& subject, const String& relation) {
    std::vector<String> result;
    auto startTime = std::chrono::steady_clock::now();

    // 正向
    auto forward = getRelatedObjects(subject, relation);
    result.insert(result.end(), forward.begin(), forward.end());

    // 反向 (对称关系)
    auto backward = getRelatedSubjects(subject, relation);
    for (const auto& b : backward) {
        if (std::find(result.begin(), result.end(), b) == result.end()) {
            result.push_back(b);
        }
    }

    // Record trace step
    if (explainabilityEngine_ && explainabilityEngine_->currentTrace() && !result.empty()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        InferenceStep step;
        step.source = "symbolic";
        step.type = "symmetric_inference";
        step.ruleId = relation;
        step.outputConfidence = 1.0f;
        step.durationMs = elapsed;
        step.detail = "Symmetric: " + subject + " via " + relation + " (forward=" +
            std::to_string(forward.size()) + ", backward=" + std::to_string(backward.size()) + ")";
        step.inputFactIds.push_back(subject + ":" + relation);
        std::vector<Triple> outputTriples;
        for (const auto& obj : result) {
            Triple t; t.subject = subject; t.predicate = relation; t.object = obj; t.confidence = 1.0f;
            outputTriples.push_back(t);
        }
        explainabilityEngine_->currentTrace()->addStep(step, outputTriples);
    }

    return result;
}

std::vector<String> SymbolicReasoner::inferInverse(const String& subject, const String& relation) {
    auto startTime = std::chrono::steady_clock::now();

    // First look for declared inverse property
    auto rel = storage_->getRelation(relation);
    if (rel && !rel->inverseProperty.empty()) {
        auto result = getRelatedObjects(subject, rel->inverseProperty);
        if (explainabilityEngine_ && explainabilityEngine_->currentTrace() && !result.empty()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            InferenceStep step;
            step.source = "symbolic";
            step.type = "inverse_inference";
            step.ruleId = relation;
            step.outputConfidence = 1.0f;
            step.durationMs = elapsed;
            step.detail = "Inverse via declared property: " + rel->inverseProperty;
            step.inputFactIds.push_back(subject + ":" + relation);
            std::vector<Triple> outputTriples;
            for (const auto& obj : result) {
                Triple t; t.subject = subject; t.predicate = rel->inverseProperty; t.object = obj; t.confidence = 1.0f;
                outputTriples.push_back(t);
            }
            explainabilityEngine_->currentTrace()->addStep(step, outputTriples);
        }
        return result;
    }

    // Check axioms for owl:inverseOf
    for (const auto& [id, axiom] : axioms_) {
        if (axiom.type == Axiom::Type::Inverse &&
            axiom.premise == relation &&
            axiom.conclusion.find("inverseOf") != String::npos) {
            auto objects = getRelatedObjects(subject, axiom.conclusion);
            if (!objects.empty()) return objects;
        }
    }

    // Fallback: convention-based _inv
    String inverseRel = relation + "_inv";
    auto result = getRelatedObjects(subject, inverseRel);
    if (!result.empty()) return result;

    // Also check: if X is object of (Y, relation), then Y is related via inverse
    auto asObject = getRelatedSubjects(subject, relation);
    if (explainabilityEngine_ && explainabilityEngine_->currentTrace() && !asObject.empty()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        InferenceStep step;
        step.source = "symbolic";
        step.type = "inverse_inference";
        step.ruleId = relation;
        step.outputConfidence = 0.8f;
        step.durationMs = elapsed;
        step.detail = "Inverse via fallback (subject-as-object)";
        std::vector<Triple> outputTriples;
        for (const auto& obj : asObject) {
            Triple t; t.subject = subject; t.predicate = relation + "_inv"; t.object = obj; t.confidence = 0.8f;
            outputTriples.push_back(t);
        }
        explainabilityEngine_->currentTrace()->addStep(step, outputTriples);
    }
    return asObject;
}

// ============================================================================
// 规则推理
// ============================================================================

std::vector<Triple> SymbolicReasoner::applyRules(const String& subject, int maxDepth) {
    std::vector<Triple> inferred;
    std::unordered_set<String> applied;
    auto startTime = std::chrono::steady_clock::now();

    std::function<void(const String&, int)> apply = [&](const String& s, int depth) {
        if (depth > maxDepth) return;

        for (const auto& [id, axiom] : axioms_) {
            if (axiom.type != Axiom::Type::CustomSWRL) continue;

            String key = s + ":" + id;
            if (applied.count(key)) continue;
            applied.insert(key);

            // 解析前提并检查是否满足
            if (checkPremise(s, axiom.premise)) {
                // 生成结论
                auto newTriples = generateConclusion(s, axiom.conclusion);
                for (const auto& t : newTriples) {
                    inferred.push_back(t);

                    // Record trace step
                    if (explainabilityEngine_ && explainabilityEngine_->currentTrace()) {
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - startTime).count();
                        InferenceStep step;
                        step.source = "symbolic";
                        step.type = "rule_application";
                        step.ruleId = id;
                        step.outputConfidence = t.confidence;
                        step.durationMs = elapsed;
                        step.detail = "Rule " + id + " applied to " + s;
                        String factId = s + ":" + axiom.premise;
                        step.inputFactIds.push_back(factId);
                        explainabilityEngine_->currentTrace()->addStep(step, {t});
                    }

                    // 递归应用规则
                    apply(t.object, depth + 1);
                }
            }
        }
    };

    apply(subject, 0);
    return inferred;
}

bool SymbolicReasoner::checkPremise(const String& subject, const String& premise) {
    // 解析前提条件
    // 格式: "relation(X, Y)" 或 "isA(X, Class)"

    std::regex relPattern(R"((\w+)\((\w+),\s*(\w+)\))");
    std::regex isAPattern(R"(isA\((\w+),\s*(\w+)\))");

    std::smatch match;

    if (std::regex_match(premise, match, isAPattern)) {
        String var = match[1].str();
        String className = match[2].str();

        if (var == "X") {
            return isInstanceOf(subject, className);
        }
    } else if (std::regex_match(premise, match, relPattern)) {
        String relation = match[1].str();
        String var1 = match[2].str();
        String var2 = match[3].str();

        if (var1 == "X") {
            auto objects = getRelatedObjects(subject, relation);
            return !objects.empty();
        }
    }

    return false;
}

std::vector<Triple> SymbolicReasoner::generateConclusion(const String& subject, const String& conclusion) {
    std::vector<Triple> triples;

    // 解析结论
    // 格式: "relation(X, Y)" 表示从 subject 通过 relation 可以到达 Y

    std::regex relPattern(R"((\w+)\((\w+),\s*(\w+)\))");
    std::smatch match;

    if (std::regex_match(conclusion, match, relPattern)) {
        String relation = match[1].str();
        String var1 = match[2].str();
        String var2 = match[3].str();

        // 简化处理：生成推理结论
        Triple t;
        t.subject = subject;
        t.predicate = relation;
        t.object = var2; // Y 变量
        t.confidence = 0.9f;
        t.source = "inference:rule";
        triples.push_back(t);
    }

    return triples;
}

// ============================================================================
// 一致性检查
// ============================================================================

std::vector<Conflict> SymbolicReasoner::checkConsistency() {
    std::vector<Conflict> conflicts;

    auto allIndividuals = storage_->getAllIndividuals();

    for (const auto& ind : allIndividuals) {
        // 检查不相交类
        auto types = getTypes(ind.id);
        for (size_t i = 0; i < types.size(); ++i) {
            for (size_t j = i + 1; j < types.size(); ++j) {
                if (areDisjoint(types[i], types[j])) {
                    Conflict c;
                    c.description = "Individual " + ind.name + " belongs to disjoint classes: " + types[i] + " and " + types[j];
                    c.entities = {ind.id, types[i], types[j]};
                    c.severity = Conflict::Severity::Error;
                    conflicts.push_back(c);
                }
            }
        }

        // 检查函数属性约束
        auto relations = getRelated(ind.id, "");
        for (const auto& t : relations) {
            if (isFunctional(t.predicate)) {
                auto objects = getRelatedObjects(ind.id, t.predicate);
                if (objects.size() > 1) {
                    Conflict c;
                    c.description = "Functional property " + t.predicate + " has multiple values for " + ind.name;
                    c.entities = {ind.id, t.predicate};
                    c.severity = Conflict::Severity::Warning;
                    conflicts.push_back(c);
                }
            }
        }
    }

    return conflicts;
}

bool SymbolicReasoner::areDisjoint(const String& class1, const String& class2) {
    auto cls1 = storage_->getClass(class1);
    if (cls1) {
        for (const auto& disjoint : cls1->disjointClasses) {
            if (disjoint == class2) return true;
        }
    }

    auto cls2 = storage_->getClass(class2);
    if (cls2) {
        for (const auto& disjoint : cls2->disjointClasses) {
            if (disjoint == class1) return true;
        }
    }

    return false;
}

bool SymbolicReasoner::isFunctional(const String& relationId) {
    auto rel = storage_->getRelation(relationId);
    if (rel) return rel->isFunctional;

    // Check axioms for FunctionalProperty declaration
    for (const auto& [id, axiom] : axioms_) {
        if (axiom.type == Axiom::Type::Functional &&
            axiom.premise == relationId &&
            axiom.conclusion.find("Functional") != String::npos) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// 通用推理接口
// ============================================================================

InferenceResult SymbolicReasoner::infer(const String& query) {
    InferenceResult result;
    result.query = query;

    // Extract entities mentioned in the query
    String mainEntity;
    auto allIndividuals = storage_->getAllIndividuals();
    for (const auto& ind : allIndividuals) {
        if (!ind.name.empty() && query.find(ind.name) != String::npos) {
            mainEntity = ind.id;
            break;
        }
        if (query.find(ind.id) != String::npos) {
            mainEntity = ind.id;
            break;
        }
    }

    if (query.find("有哪些") != String::npos || query.find("哪些") != String::npos) {
        auto facts = inferInstances(query);
        for (const auto& f : facts) {
            result.facts.push_back(f);
        }
    } else if (query.find("可以") != String::npos || query.find("能够") != String::npos) {
        auto inferred = inferCapabilities(query);
        for (const auto& t : inferred) {
            Triple triple;
            triple.subject = mainEntity.empty() ? "unknown" : mainEntity;
            triple.predicate = "inferred";
            triple.object = t;
            triple.confidence = 0.8f;
            result.facts.push_back(triple);
        }
    } else if (!mainEntity.empty()) {
        // General entity query: return all related triples
        auto related = getRelated(mainEntity, "");
        for (const auto& t : related) {
            result.facts.push_back(t);
        }
        auto ruleResults = applyRules(mainEntity, maxDepth_);
        for (const auto& t : ruleResults) {
            result.facts.push_back(t);
        }
    }

    result.explanation = generateExplanation(result);
    return result;
}

std::vector<Triple> SymbolicReasoner::inferInstances(const String& query) {
    std::vector<Triple> results;

    // 尝试识别类名
    auto allClasses = storage_->getAllClasses();
    for (const auto& cls : allClasses) {
        if (query.find(cls.name) != String::npos) {
            auto individuals = storage_->getIndividualsByClass(cls.id);
            for (const auto& ind : individuals) {
                Triple t;
                t.subject = cls.name;
                t.predicate = "hasInstance";
                t.object = ind.name;
                t.confidence = 1.0f;
                t.source = "direct";
                results.push_back(t);
            }
        }
    }

    return results;
}

std::vector<String> SymbolicReasoner::inferCapabilities(const String& query) {
    std::vector<String> results;

    // 提取主体
    auto allIndividuals = storage_->getAllIndividuals();
    for (const auto& ind : allIndividuals) {
        if (query.find(ind.name) != String::npos) {
            // 查找相关能力
            auto inferred = applyRules(ind.id, 3);
            for (const auto& t : inferred) {
                results.push_back(t.object);
            }
        }
    }

    return results;
}

String SymbolicReasoner::generateExplanation(const InferenceResult& result) {
    std::ostringstream oss;

    oss << "符号推理过程 (左脑):\n";

    for (const auto& fact : result.facts) {
        oss << "  ✓ " << fact.subject << " " << fact.predicate << " " << fact.object;
        if (fact.confidence < 1.0f) {
            oss << " (置信度: " << fact.confidence << ")";
        }
        oss << "\n";
    }

    if (result.facts.empty()) {
        oss << "  (无推理结果)\n";
    }

    return oss.str();
}

} // namespace ontology
