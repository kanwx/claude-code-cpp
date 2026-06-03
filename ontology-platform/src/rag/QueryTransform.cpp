#include <ontology/QueryTransform.hpp>
#include <ontology/mcp/CognitiveMcpServer.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>
#include <cmath>

namespace ontology {

QueryTransformEngine::QueryTransformEngine(
    std::shared_ptr<TextEmbedder> embedder,
    StoragePtr hybridStorage,
    LlmCollaboration* llm
)
    : embedder_(embedder)
    , hybridStorage_(hybridStorage)
    , llm_(llm)
{
}

QueryTransformEngine::TransformedQuery
QueryTransformEngine::transform(const String& query) {
    TransformedQuery tq;
    tq.originalQuery = query;
    tq.originalEmbedding = embedder_->embed(query);

    // Step 1: HyDE — 生成假设文档
    if (config_.enableHyDE) {
        tq.hypotheticalDocument = generateHypotheticalDocument(query);
        if (!tq.hypotheticalDocument.empty()) {
            tq.hydeEmbedding = embedder_->embed(tq.hypotheticalDocument);
        }
    }

    // Step 2: 查询扩展
    if (config_.enableExpansion) {
        tq.expandedQueries = expandQuery(query);
        // 计算嵌入
        for (auto& eq : tq.expandedQueries) {
            eq.embedding = embedder_->embed(eq.text);
        }
    }

    // Step 3: 查询分解
    if (config_.enableDecomposition && isCompoundQuery(query)) {
        tq.subQueries = decomposeQuery(query);
        // 计算嵌入
        for (auto& sq : tq.subQueries) {
            sq.embedding = embedder_->embed(sq.text);
        }
    }

    // Step 4: 收集所有检索用文本
    std::unordered_set<String> seen;
    tq.allQueryTexts.push_back(query);
    seen.insert(query);

    if (!tq.hypotheticalDocument.empty() && seen.find(tq.hypotheticalDocument) == seen.end()) {
        tq.allQueryTexts.push_back(tq.hypotheticalDocument);
        seen.insert(tq.hypotheticalDocument);
    }

    for (const auto& eq : tq.expandedQueries) {
        if (seen.find(eq.text) == seen.end()) {
            tq.allQueryTexts.push_back(eq.text);
            seen.insert(eq.text);
        }
    }

    for (const auto& sq : tq.subQueries) {
        if (seen.find(sq.text) == seen.end()) {
            tq.allQueryTexts.push_back(sq.text);
            seen.insert(sq.text);
        }
    }

    return tq;
}

std::vector<std::pair<std::vector<float>, float>>
QueryTransformEngine::getAllRetrievalEmbeddings(const TransformedQuery& tq) {
    std::vector<std::pair<std::vector<float>, float>> embeddings;

    // Original query embedding (weight 1.0)
    if (!tq.originalEmbedding.empty()) {
        embeddings.push_back({tq.originalEmbedding, 1.0f});
    }

    // HyDE embedding (weight 0.7 — less certain than original)
    if (!tq.hydeEmbedding.empty()) {
        embeddings.push_back({tq.hydeEmbedding, 0.7f});
    }

    // Expanded query embeddings
    for (const auto& eq : tq.expandedQueries) {
        if (!eq.embedding.empty()) {
            embeddings.push_back({eq.embedding, eq.weight * 0.5f});
        }
    }

    // Sub-query embeddings (each weighted equally)
    float subWeight = tq.subQueries.empty() ? 0.0f : 0.8f / tq.subQueries.size();
    for (const auto& sq : tq.subQueries) {
        if (!sq.embedding.empty()) {
            embeddings.push_back({sq.embedding, subWeight});
        }
    }

    return embeddings;
}

std::vector<std::pair<String, float>>
QueryTransformEngine::getAllRetrievalTexts(const TransformedQuery& tq) {
    std::vector<std::pair<String, float>> texts;

    texts.push_back({tq.originalQuery, 1.0f});

    if (!tq.hypotheticalDocument.empty()) {
        texts.push_back({tq.hypotheticalDocument, 0.7f});
    }

    for (const auto& eq : tq.expandedQueries) {
        texts.push_back({eq.text, eq.weight * 0.5f});
    }

    for (const auto& sq : tq.subQueries) {
        texts.push_back({sq.text, 0.6f});
    }

    return texts;
}

// ============================================================================
// HyDE
// ============================================================================

String QueryTransformEngine::generateHypotheticalDocument(const String& query) {
    if (llm_) {
        auto result = generateHypotheticalDocumentLLM(query);
        if (!result.empty()) return result;
    }
    return generateHypotheticalDocumentTemplate(query);
}

String QueryTransformEngine::generateHypotheticalDocumentLLM(const String& query) {
    if (!llm_) return "";

    try {
        String prompt = "请写一段详细的回答来解答以下问题，即使你不确定也要尽量提供相关信息：\n\n" + query;
        auto response = llm_->callLlm("你是一个知识助手，请提供详细、准确的回答。", prompt);
        if (response.length() > static_cast<size_t>(config_.hydeMaxTokens)) {
            response = response.substr(0, config_.hydeMaxTokens);
        }
        return response;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("RAG JSON error: {}", e.what());
        return "";
    } catch (const std::exception& e) {
        spdlog::error("RAG error: {}", e.what());
        return "";
    }
}

String QueryTransformEngine::generateHypotheticalDocumentTemplate(const String& query) {
    // Template-based HyDE: construct a hypothetical answer document
    // This works without LLM by using domain templates
    std::ostringstream oss;

    // Extract key terms from query
    auto terms = splitChinese(query);

    oss << "关于";
    for (size_t i = 0; i < terms.size() && i < 5; i++) {
        if (i > 0) oss << "、";
        oss << terms[i];
    }
    oss << "的相关信息如下：\n\n";

    // Generate template content based on query patterns
    if (query.find("是什么") != String::npos || query.find("什么是") != String::npos ||
        query.find("什么是") != String::npos || query.find("what is") != String::npos) {
        oss << "定义：";
        for (size_t i = 0; i < terms.size() && i < 3; i++) {
            oss << terms[i];
            if (i + 1 < terms.size() && i + 1 < 3) oss << "是";
        }
        oss << "是一种重要的概念，具有以下特征：首先，它属于";
        if (!terms.empty()) oss << terms[0];
        oss << "领域；其次，它与";
        if (terms.size() > 1) oss << terms[1];
        else oss << "相关实体";
        oss << "有密切关联；最后，它在实际应用中发挥着关键作用。\n";
    } else if (query.find("如何") != String::npos || query.find("怎么") != String::npos ||
               query.find("怎样") != String::npos || query.find("how") != String::npos) {
        oss << "方法与步骤：";
        for (size_t i = 0; i < terms.size() && i < 4; i++) {
            oss << terms[i] << " ";
        }
        oss << "的实施流程包括：第一步，确定目标与范围；第二步，分析现有资源；"
            << "第三步，制定详细计划；第四步，执行并跟踪进度；第五步，评估效果并改进。\n";
    } else if (query.find("为什么") != String::npos || query.find("原因") != String::npos ||
               query.find("why") != String::npos) {
        oss << "原因分析：";
        for (size_t i = 0; i < terms.size() && i < 3; i++) {
            oss << terms[i] << " ";
        }
        oss << "的原因主要有以下几点：一是内在机制驱动；二是外部环境影响；"
            << "三是历史发展脉络；四是与其他因素的交互作用。\n";
    } else {
        // Generic template
        oss << "相关描述：";
        for (size_t i = 0; i < terms.size() && i < 5; i++) {
            oss << terms[i] << " ";
        }
        oss << "是一个核心概念，具有多方面的特征和关联。"
            << "在实际场景中，它与其他实体存在多层次的关系网络，"
            << "包括层次结构关系、功能依赖关系和语义关联关系。\n";
    }

    return oss.str();
}

// ============================================================================
// Query Expansion
// ============================================================================

std::vector<QueryTransformEngine::TransformedQuery::ExpandedQuery>
QueryTransformEngine::expandQuery(const String& query) {
    std::vector<TransformedQuery::ExpandedQuery> expanded;

    if (config_.useSynonyms) {
        auto synExp = expandBySynonyms(query);
        expanded.insert(expanded.end(), synExp.begin(), synExp.end());
    }

    if (config_.useOntologyExpansion && hybridStorage_) {
        auto ontoExp = expandByOntology(query);
        expanded.insert(expanded.end(), ontoExp.begin(), ontoExp.end());
    }

    auto templateExp = expandByTemplates(query);
    expanded.insert(expanded.end(), templateExp.begin(), templateExp.end());

    // Deduplicate
    std::unordered_set<String> seen;
    seen.insert(query);
    expanded.erase(std::remove_if(expanded.begin(), expanded.end(),
        [&seen](const auto& e) {
            if (seen.count(e.text) || e.text == "") return true;
            seen.insert(e.text);
            return false;
        }), expanded.end());

    // Limit
    if (static_cast<int>(expanded.size()) > config_.maxExpandedQueries) {
        expanded.resize(config_.maxExpandedQueries);
    }

    return expanded;
}

std::vector<QueryTransformEngine::TransformedQuery::ExpandedQuery>
QueryTransformEngine::expandBySynonyms(const String& query) {
    std::vector<TransformedQuery::ExpandedQuery> results;

    // Chinese synonym patterns (domain-independent common synonyms)
    static const std::vector<std::pair<String, String>> synonyms = {
        {"管理", "经营 治理 运营"}, {"组织", "机构 团体 单位"},
        {"人员", "员工 工作人员 职员"}, {"部门", "科室 处室 机构"},
        {"项目", "工程 计划 任务"}, {"流程", "过程 步骤 程序"},
        {"系统", "平台 体系 架构"}, {"数据", "信息 资料 记录"},
        {"技术", "工艺 方法 手段"}, {"问题", "议题 难题 挑战"},
        {"方案", "计划 策略 对策"}, {"目标", "目的 指标 方向"},
        {"风险", "隐患 危险 威胁"}, {"资源", "资产 要素 条件"},
        {"优化", "改进 提升 完善"}, {"分析", "研究 评估 解析"},
        {"实施", "执行 落实 推行"}, {"监控", "监测 监督 观察"},
        {"合作", "协作 配合 联合"}, {"创新", "革新 突破 变革"},
    };

    for (const auto& [key, values] : synonyms) {
        if (query.find(key) != String::npos) {
            // Split synonym values
            std::istringstream iss(values);
            String word;
            while (iss >> word) {
                String expanded = query;
                size_t pos = expanded.find(key);
                if (pos != String::npos) {
                    expanded.replace(pos, key.length(), word);
                    TransformedQuery::ExpandedQuery eq;
                    eq.text = expanded;
                    eq.method = "synonym";
                    eq.weight = 0.6f;
                    results.push_back(eq);
                }
            }
        }
    }

    // English synonyms
    static const std::vector<std::pair<String, String>> enSynonyms = {
        {"manage", "administer operate run"}, {"organization", "institution agency group"},
        {"employee", "worker staff personnel"}, {"department", "division unit section"},
        {"process", "procedure workflow method"}, {"system", "platform framework infrastructure"},
        {"analyze", "examine evaluate assess"}, {"implement", "execute deploy realize"},
    };

    for (const auto& [key, values] : enSynonyms) {
        // Case-insensitive search
        String lowerQuery = query;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
            [](unsigned char c) { return std::tolower(c); });

        if (lowerQuery.find(key) != String::npos) {
            std::istringstream iss(values);
            String word;
            while (iss >> word) {
                String expanded = query;
                size_t pos = lowerQuery.find(key);
                if (pos != String::npos) {
                    expanded.replace(pos, key.length(), word);
                    TransformedQuery::ExpandedQuery eq;
                    eq.text = expanded;
                    eq.method = "synonym";
                    eq.weight = 0.5f;
                    results.push_back(eq);
                }
            }
        }
    }

    return results;
}

std::vector<QueryTransformEngine::TransformedQuery::ExpandedQuery>
QueryTransformEngine::expandByOntology(const String& query) {
    std::vector<TransformedQuery::ExpandedQuery> results;

    if (!hybridStorage_) return results;

    // Find entities mentioned in query and expand using ontology hierarchy
    auto individuals = hybridStorage_->getAllIndividuals();
    auto classes = hybridStorage_->getAllClasses();

    for (const auto& ind : individuals) {
        if (!ind.name.empty() && query.find(ind.name) != String::npos) {
            // Expand to include class members
            if (!ind.classId.empty()) {
                auto classMembers = hybridStorage_->getIndividualsByClass(ind.classId);
                for (const auto& member : classMembers) {
                    if (member.id != ind.id && !member.name.empty()) {
                        String expanded = query;
                        // Add the member name as additional context
                        expanded += " " + member.name;

                        TransformedQuery::ExpandedQuery eq;
                        eq.text = expanded;
                        eq.method = "ontology";
                        eq.weight = 0.4f;
                        results.push_back(eq);
                    }
                }
            }

            // Expand using relation triples
            auto triples = hybridStorage_->findBySubject(ind.id);
            for (const auto& t : triples) {
                String expanded = query + " " + t.predicate + " " + t.object;
                TransformedQuery::ExpandedQuery eq;
                eq.text = expanded;
                eq.method = "ontology";
                eq.weight = 0.3f * t.confidence;
                results.push_back(eq);
            }
        }
    }

    // Expand class names to include subclasses
    for (const auto& cls : classes) {
        if (!cls.name.empty() && query.find(cls.name) != String::npos) {
            // Add parent class names for broader context
            auto superIds = hybridStorage_->getSuperClasses(cls.id);
            for (const auto& superClass : superIds) {
                auto parent = hybridStorage_->getClass(superClass);
                if (parent && !parent->name.empty()) {
                    String expanded = query + " " + parent->name;
                    TransformedQuery::ExpandedQuery eq;
                    eq.text = expanded;
                    eq.method = "ontology";
                    eq.weight = 0.3f;
                    results.push_back(eq);
                }
            }
        }
    }

    return results;
}

std::vector<QueryTransformEngine::TransformedQuery::ExpandedQuery>
QueryTransformEngine::expandByTemplates(const String& query) {
    std::vector<TransformedQuery::ExpandedQuery> results;

    // Template-based expansions for common query patterns
    auto terms = splitChinese(query);

    // Template 1: Add domain context
    if (terms.size() >= 2) {
        TransformedQuery::ExpandedQuery eq;
        eq.text = terms[0] + "的" + terms[1] + "相关概念和应用";
        eq.method = "template";
        eq.weight = 0.4f;
        results.push_back(eq);
    }

    // Template 2: Reverse relation query
    if (terms.size() >= 2) {
        TransformedQuery::ExpandedQuery eq;
        eq.text = terms[1] + "对应的" + terms[0];
        eq.method = "template";
        eq.weight = 0.3f;
        results.push_back(eq);
    }

    return results;
}

// ============================================================================
// Query Decomposition
// ============================================================================

std::vector<QueryTransformEngine::TransformedQuery::SubQuery>
QueryTransformEngine::decomposeQuery(const String& query) {
    auto results = decomposeByConjunctions(query);

    if (results.empty()) {
        results = decomposeByClauses(query);
    }

    // Assign order
    for (size_t i = 0; i < results.size(); i++) {
        results[i].order = static_cast<int>(i);
    }

    // Limit
    if (static_cast<int>(results.size()) > config_.maxSubQueries) {
        results.resize(config_.maxSubQueries);
    }

    return results;
}

std::vector<QueryTransformEngine::TransformedQuery::SubQuery>
QueryTransformEngine::decomposeByConjunctions(const String& query) {
    std::vector<TransformedQuery::SubQuery> results;

    // Chinese conjunctions
    static const std::vector<String> conjunctions = {
        "并且", "而且", "以及", "同时", "另外", "还有",
        "和", "与", "及", "或", "还是",
        "并且还", "而且还", "，以及", "，同时",
    };

    for (const auto& conj : conjunctions) {
        size_t pos = query.find(conj);
        if (pos != String::npos) {
            String part1 = query.substr(0, pos);
            String part2 = query.substr(pos + conj.length());

            // Trim
            auto trim = [](String s) -> String {
                size_t a = s.find_first_not_of(" \t\n\r");
                size_t b = s.find_last_not_of(" \t\n\r");
                return (a == String::npos) ? "" : s.substr(a, b - a + 1);
            };
            part1 = trim(part1);
            part2 = trim(part2);

            if (part1.length() >= 2 && part2.length() >= 2) {
                TransformedQuery::SubQuery sq1;
                sq1.text = part1;
                sq1.isCompound = false;
                results.push_back(sq1);

                TransformedQuery::SubQuery sq2;
                sq2.text = part2;
                sq2.isCompound = false;
                results.push_back(sq2);

                return results;  // Return first decomposition
            }
        }
    }

    // English conjunctions
    static const std::vector<String> enConjunctions = {" and ", " or ", " but ", " as well as ", " along with "};
    for (const auto& conj : enConjunctions) {
        size_t pos = query.find(conj);
        if (pos != String::npos) {
            String part1 = query.substr(0, pos);
            String part2 = query.substr(pos + conj.length());

            if (part1.length() >= config_.minSubQueryLength &&
                part2.length() >= config_.minSubQueryLength) {
                TransformedQuery::SubQuery sq1;
                sq1.text = part1;
                results.push_back(sq1);

                TransformedQuery::SubQuery sq2;
                sq2.text = part2;
                results.push_back(sq2);

                return results;
            }
        }
    }

    return results;
}

std::vector<QueryTransformEngine::TransformedQuery::SubQuery>
QueryTransformEngine::decomposeByClauses(const String& query) {
    std::vector<TransformedQuery::SubQuery> results;

    // Split on Chinese punctuation that suggests sub-questions
    static const std::vector<std::pair<String, bool>> markers = {
        {"吗？", true}, {"呢？", true}, {"？", true},
        {"是否", false}, {"能不能", false}, {"有没有", false},
    };

    // Try to split on question marks or "是否/能不能" patterns
    for (const auto& [marker, isQuestionEnd] : markers) {
        size_t pos = query.find(marker);
        if (pos != String::npos && pos > 0) {
            String part1 = query.substr(0, pos + marker.length());
            String part2 = query.substr(pos + marker.length());

            auto trim = [](String s) -> String {
                size_t a = s.find_first_not_of(" \t\n\r");
                size_t b = s.find_last_not_of(" \t\n\r");
                return (a == String::npos) ? "" : s.substr(a, b - a + 1);
            };
            part1 = trim(part1);
            part2 = trim(part2);

            if (part1.length() >= 2) {
                TransformedQuery::SubQuery sq1;
                sq1.text = part1;
                results.push_back(sq1);
            }
            if (part2.length() >= 2) {
                TransformedQuery::SubQuery sq2;
                sq2.text = part2;
                results.push_back(sq2);
            }

            if (!results.empty()) return results;
        }
    }

    return results;
}

// ============================================================================
// Helpers
// ============================================================================

std::vector<String> QueryTransformEngine::splitChinese(const String& text) const {
    std::vector<String> terms;
    size_t i = 0;
    String currentWord;

    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        if (c < 0x80) {
            if (std::isalnum(c) || c == '_') {
                currentWord += static_cast<char>(std::tolower(c));
            } else {
                if (currentWord.length() >= 2) terms.push_back(currentWord);
                currentWord.clear();
            }
            i++;
        } else if (c >= 0xE0 && c < 0xF0) {
            if (currentWord.length() >= 2) terms.push_back(currentWord);
            currentWord.clear();
            if (i + 3 <= text.size()) {
                terms.push_back(text.substr(i, 3));
                i += 3;
            } else {
                i++;
            }
        } else {
            if (currentWord.length() >= 2) terms.push_back(currentWord);
            currentWord.clear();
            i += (c >= 0xC0 && c < 0xE0) ? 2 : 1;
        }
    }
    if (currentWord.length() >= 2) terms.push_back(currentWord);

    return terms;
}

String QueryTransformEngine::extractKeywords(const String& query) const {
    auto terms = splitChinese(query);
    // Filter out common stop words
    static const std::unordered_set<String> stopWords = {
        "的", "了", "在", "是", "我", "有", "和", "就", "不", "人",
        "都", "一", "一个", "上", "也", "很", "到", "说", "要", "去",
        "你", "会", "着", "没有", "看", "好", "自己", "这",
        "the", "a", "an", "is", "are", "was", "were", "be", "been",
        "have", "has", "had", "do", "does", "did", "will", "would",
        "can", "could", "may", "might", "shall", "should", "it", "in",
        "on", "at", "to", "for", "of", "with", "by", "from", "and",
        "or", "but", "not", "no", "all", "any", "each", "every",
    };

    std::ostringstream oss;
    for (const auto& term : terms) {
        if (stopWords.find(term) == stopWords.end()) {
            if (!oss.str().empty()) oss << " ";
            oss << term;
        }
    }
    return oss.str();
}

bool QueryTransformEngine::isCompoundQuery(const String& query) const {
    // Check for conjunctions
    static const std::vector<String> indicators = {
        "并且", "而且", "以及", "同时", "另外", "还有",
        "和", "与", "及", "或", "还是",
        "和", "与", "及",
        " and ", " or ", " but ",
    };

    for (const auto& ind : indicators) {
        if (query.find(ind) != String::npos) return true;
    }

    // Check for multiple question patterns
    int questionCount = 0;
    size_t pos = 0;
    while ((pos = query.find("？", pos)) != String::npos) {
        questionCount++;
        pos++;
    }
    pos = 0;
    while ((pos = query.find("?", pos)) != String::npos) {
        questionCount++;
        pos++;
    }

    return questionCount >= 2;
}

} // namespace ontology
