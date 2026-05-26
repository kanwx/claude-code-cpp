#include <ontology/Inference.hpp>
#include <ontology/DempsterShafer.hpp>
#include <ontology/DlReasoner.hpp>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace ontology {

// ============================================================================
// HybridReasoner 实现 - 混合推理 (左右脑协同)
// ============================================================================

HybridReasoner::HybridReasoner(
    std::shared_ptr<SymbolicReasoner> symbolic,
    std::shared_ptr<NeuralReasoner> neural
) : symbolic_(symbolic), neural_(neural) {
}

HybridReasoner::HybridReasoner(
    std::shared_ptr<SymbolicReasoner> symbolic,
    std::shared_ptr<NeuralReasoner> neural,
    const Config& config
) : symbolic_(symbolic), neural_(neural), config_(config) {
}

void HybridReasoner::setStorage(StoragePtr storage) {
    hybridStorage_ = storage;
}

// ============================================================================
// 主推理接口
// ============================================================================

HybridReasoner::HybridResult HybridReasoner::infer(const String& individualId, const String& context) {
    HybridResult result;
    auto totalStart = std::chrono::steady_clock::now();

    // Start trace if explainability engine is available
    bool tracing = explainabilityEngine_ && explainabilityEngine_->isEnabled();
    if (tracing) {
        explainabilityEngine_->beginTrace(individualId);
        // Propagate engine to sub-reasoners
        if (symbolic_) symbolic_->setExplainabilityEngine(explainabilityEngine_);
        if (neural_) neural_->setExplainabilityEngine(explainabilityEngine_);
    }

    // 符号推理
    auto symStart = std::chrono::steady_clock::now();
    if (config_.enableSymbolic && symbolic_) {
        // 获取类型推理结果
        auto types = symbolic_->getTypes(individualId);
        for (const auto& type : types) {
            Triple t;
            t.subject = individualId;
            t.predicate = "rdf:type";
            t.object = type;
            t.confidence = 1.0f;
            result.symbolicFacts.push_back(t);
        }

        // Also apply rules
        auto ruleFacts = symbolic_->applyRules(individualId, config_.maxInferenceDepth);
        result.symbolicFacts.insert(result.symbolicFacts.end(), ruleFacts.begin(), ruleFacts.end());
    }
    auto symElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - symStart).count();

    // Record symbolic phase trace
    if (tracing && !result.symbolicFacts.empty()) {
        InferenceStep step;
        step.source = "symbolic";
        step.type = "type_and_rule_inference";
        step.outputConfidence = 1.0f;
        step.durationMs = symElapsed;
        step.detail = "Symbolic phase: " + std::to_string(result.symbolicFacts.size()) + " facts";
        explainabilityEngine_->currentTrace()->addStep(step, result.symbolicFacts);
    }

    // 神经推理 (如果启用)
    auto neuStart = std::chrono::steady_clock::now();
    if (config_.enableNeural && neural_) {
        auto similar = neural_->findSimilar(individualId, 10);
        for (const auto& [id, score] : similar) {
            result.neuralPredictions.push_back({id, score});
        }
    }
    auto neuElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - neuStart).count();

    // Record neural phase trace
    if (tracing && !result.neuralPredictions.empty()) {
        InferenceStep step;
        step.source = "neural";
        step.type = "similarity_search";
        step.embeddingModel = neural_ ? "embedding" : "";
        step.outputConfidence = result.neuralPredictions.empty() ? 0.0f : result.neuralPredictions[0].second;
        step.durationMs = neuElapsed;
        step.detail = "Neural phase: " + std::to_string(result.neuralPredictions.size()) + " predictions";
        std::vector<Triple> neuTriples;
        for (const auto& [id, score] : result.neuralPredictions) {
            Triple t; t.subject = individualId; t.predicate = "similarTo"; t.object = id; t.confidence = score;
            neuTriples.push_back(t);
        }
        explainabilityEngine_->currentTrace()->addStep(step, neuTriples);
    }

    // 融合结果 (Dempster-Shafer 证据理论)
    auto fusionStart = std::chrono::steady_clock::now();
    std::vector<NeuralPredictionData> preds;
    for (const auto& [id, score] : result.neuralPredictions) {
        preds.push_back({individualId, "similarTo", id, score});
    }

    // 使用 D-S 融合器
    DempsterShaferFuser::Config dsConfig;
    dsConfig.symbolicDiscount = config_.symbolWeight;
    dsConfig.neuralDiscount = config_.neuralWeight;
    dsConfig.conflictThreshold = 0.7f;
    DempsterShaferFuser fuser(dsConfig);

    result.dsFusionDetails = fuser.fuseResults(result.symbolicFacts, preds);
    result.combined = fuser.extractFacts(result.dsFusionDetails);
    auto fusionElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - fusionStart).count();

    // Record D-S fusion trace
    if (tracing && !result.dsFusionDetails.empty()) {
        InferenceStep step;
        step.source = "hybrid";
        step.type = "ds_fusion";
        step.outputConfidence = result.combined.empty() ? 0.0f : result.combined[0].confidence;
        step.durationMs = fusionElapsed;
        float maxConflict = 0.0f;
        for (const auto& dsr : result.dsFusionDetails) {
            if (dsr.conflict > maxConflict) maxConflict = dsr.conflict;
        }
        step.embeddingScore = maxConflict;
        step.detail = "D-S fusion: " + std::to_string(result.dsFusionDetails.size()) +
            " results, max conflict=" + std::to_string(maxConflict);
        explainabilityEngine_->currentTrace()->addStep(step, result.combined);
    }

    // 生成解释
    std::ostringstream oss;
    oss << "推理结果包含 " << result.symbolicFacts.size() << " 个符号事实"
        << " 和 " << result.neuralPredictions.size() << " 个神经预测";
    result.explanation = oss.str();

    // Finish trace
    if (tracing) {
        auto trace = explainabilityEngine_->finishTrace();
        result.traceId = trace.traceId;
    }

    return result;
}

String HybridReasoner::answer(const String& question, const Ontology& ontology) {
    // 简单问答实现
    std::ostringstream oss;

    if (config_.enableSymbolic && symbolic_) {
        oss << "符号推理: ";
        // 搜索相关实体
        for (const auto& [id, ind] : ontology.individuals) {
            if (question.find(ind.name) != String::npos) {
                oss << "找到实体 " << ind.name << " 属于类 " << ind.classId;
            }
        }
    }

    if (config_.enableNeural && neural_) {
        oss << "\n神经推理: ";
        auto neurals = neural_->infer(question);
        oss << "找到 " << neurals.facts.size() << " 个神经推理结果";
    }

    return oss.str();
}

std::vector<std::pair<Individual, float>> HybridReasoner::findSimilar(
    const Individual& target,
    int topK
) {
    std::vector<std::pair<Individual, float>> results;

    if (neural_) {
        auto similar = neural_->findSimilar(target.id, topK);
        for (const auto& [id, score] : similar) {
            if (hybridStorage_) {
                auto ind = hybridStorage_->getIndividual(id);
                if (ind) {
                    results.push_back({*ind, score});
                }
            } else {
                Individual ind;
                ind.id = id;
                ind.name = id;
                results.push_back({ind, score});
            }
        }
    }

    // Add symbolic matches (same class)
    if (hybridStorage_ && !target.classId.empty()) {
        auto classMembers = hybridStorage_->getIndividualsByClass(target.classId);
        for (const auto& ind : classMembers) {
            if (ind.id != target.id) {
                bool alreadyAdded = false;
                for (const auto& [existing, _] : results) {
                    if (existing.id == ind.id) { alreadyAdded = true; break; }
                }
                if (!alreadyAdded) {
                    results.push_back({ind, 0.6f});
                }
            }
        }
    }

    std::sort(results.begin(), results.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (static_cast<int>(results.size()) > topK) {
        results.resize(topK);
    }

    return results;
}

std::vector<Triple> HybridReasoner::knowledgeCompletion(const Triple& partial) {
    std::vector<Triple> results;

    if (config_.enableNeural && neural_) {
        auto predictions = neural_->predictLinks(partial.subject, partial.predicate, 10);

        for (const auto& [obj, score] : predictions) {
            Triple t;
            t.subject = partial.subject;
            t.predicate = partial.predicate;
            t.object = obj;
            t.confidence = score;
            results.push_back(t);
        }
    }

    return results;
}

std::vector<HybridReasoner::Contradiction> HybridReasoner::detectContradictions() {
    std::vector<Contradiction> contradictions;

    // DL reasoner: TBox-level consistency checking
    if (dlReasoner_ && hybridStorage_) {
        TripleStore* ts = hybridStorage_->getTripleStore();
        if (ts) {
            dlReasoner_->loadFromTripleStore(ts);
            if (!dlReasoner_->isConsistent()) {
                Contradiction c;
                c.reason = "DL reasoner: TBox/ABox inconsistency detected (tableaux clash)";
                c.confidence = 1.0f;
                contradictions.push_back(c);
            }
        }
    }

    // Symbolic reasoner: ABox-level conflict detection
    if (config_.enableSymbolic && symbolic_) {
        auto violations = symbolic_->checkConsistency();
        for (const auto& v : violations) {
            Contradiction c;
            c.reason = v.description;
            c.confidence = 1.0f;
            contradictions.push_back(c);
        }
    }

    return contradictions;
}

std::vector<Triple> HybridReasoner::mergeResults(
    const std::vector<Triple>& symbolic,
    const std::vector<NeuralPrediction>& neural
) {
    // 使用 Dempster-Shafer 证据理论融合
    DempsterShaferFuser::Config dsConfig;
    dsConfig.symbolicDiscount = config_.symbolWeight;
    dsConfig.neuralDiscount = config_.neuralWeight;
    dsConfig.conflictThreshold = 0.7f;

    // 转换 NeuralPrediction → NeuralPredictionData
    std::vector<NeuralPredictionData> preds;
    for (const auto& n : neural) {
        preds.push_back({n.subject, n.relation, n.object, n.score});
    }

    DempsterShaferFuser fuser(dsConfig);
    auto fusionResults = fuser.fuseResults(symbolic, preds);

    return fuser.extractFacts(fusionResults);
}

} // namespace ontology
