#include <ontology/AutoModel.hpp>
#include <ontology/Storage.hpp>
#include <sstream>
#include <random>
#include <cmath>
#include <curl/curl.h>

namespace ontology {

// ============================================================================
// LLMInterface 实现
// ============================================================================

static std::once_flag curlInitFlag;

LLMInterface::LLMInterface(const AutoModelConfig& config) : config_(config) {
    std::call_once(curlInitFlag, []() { curl_global_init(CURL_GLOBAL_ALL); });
}

LLMInterface::~LLMInterface() {
    // curl_global_cleanup must be called once at program exit, not per instance
}

void LLMInterface::setApiKey(const String& key) {
    config_.llmApiKey = key;
}

void LLMInterface::setEndpoint(const String& endpoint) {
    config_.llmEndpoint = endpoint;
}

void LLMInterface::setModel(const String& model) {
    config_.llmModel = model;
}

String LLMInterface::chat(const String& prompt, const String& systemPrompt) {
    Json body;
    body["model"] = config_.llmModel;
    body["max_tokens"] = config_.maxTokens;
    body["temperature"] = config_.temperature;

    Json messages = Json::array();
    if (!systemPrompt.empty()) {
        Json sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = systemPrompt;
        messages.push_back(sysMsg);
    }

    Json userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = prompt;
    messages.push_back(userMsg);

    body["messages"] = messages;

    String url = config_.llmEndpoint + "/chat/completions";
    String response = httpPost(url, body.dump());

    try {
        Json result = Json::parse(response);
        return result["choices"][0]["message"]["content"].get<String>();
    } catch (...) {
        return "";
    }
}

Json LLMInterface::chatJson(const String& prompt, const String& systemPrompt) {
    String response = chat(prompt, systemPrompt);

    // 尝试提取 JSON
    size_t start = response.find('{');
    size_t end = response.rfind('}');

    if (start != String::npos && end != String::npos) {
        String jsonStr = response.substr(start, end - start + 1);
        try {
            return Json::parse(jsonStr);
        } catch (...) {}
    }

    return Json::object();
}

String LLMInterface::httpPost(const String& url, const String& body) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    String authHeader = "Authorization: Bearer " + config_.llmApiKey;
    headers = curl_slist_append(headers, authHeader.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    String response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
        +[](void* contents, size_t size, size_t nmemb, String* s) -> size_t {
            s->append((char*)contents, size * nmemb);
            return size * nmemb;
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}

String LLMInterface::complete(const String& prompt) {
    Json body;
    body["model"] = config_.llmModel;
    body["prompt"] = prompt;
    body["max_tokens"] = config_.maxTokens;
    body["temperature"] = config_.temperature;

    String url = config_.llmEndpoint + "/completions";
    String response = httpPost(url, body.dump());

    try {
        Json result = Json::parse(response);
        return result["choices"][0]["text"].get<String>();
    } catch (...) {
        return "";
    }
}

std::vector<String> LLMInterface::batchChat(const std::vector<String>& prompts) {
    std::vector<String> results;
    results.reserve(prompts.size());
    for (const auto& prompt : prompts) {
        results.push_back(chat(prompt));
    }
    return results;
}

String LLMInterface::makeRequest(const String& prompt, const String& systemPrompt) {
    return chat(prompt, systemPrompt);
}

// ============================================================================
// EntityExtractor 实现
// ============================================================================

EntityExtractor::EntityExtractor(std::shared_ptr<LLMInterface> llm, StoragePtr storage)
    : llm_(llm), storage_(storage) {

    // 默认抽取模板
    promptTemplate_ = R"(
从以下文本中抽取实体和关系，以JSON格式返回：

文本:
{{TEXT}}

已知实体类型:
{{ENTITY_TYPES}}

请返回格式:
{
  "entities": [
    {"text": "实体文本", "id": "实体ID", "class": "类型", "confidence": 0.95}
  ],
  "relations": [
    {"subject": "主体ID", "predicate": "关系", "object": "客体ID", "confidence": 0.9}
  ]
}

只返回JSON，不要其他内容。
)";

    // 默认实体类型
    entityTypes_ = {
        {"Person", "人员，如员工、经理、客户等"},
        {"Organization", "组织，如公司、部门、团队等"},
        {"Location", "地点，如城市、地址、区域等"},
        {"Product", "产品，如商品、服务、项目等"},
        {"Event", "事件，如会议、活动、事故等"},
        {"Concept", "概念，如技术、方法、理论等"}
    };
}

void EntityExtractor::addEntityType(const String& type, const String& description) {
    entityTypes_[type] = description;
}

String EntityExtractor::buildPrompt(const String& text) {
    String prompt = promptTemplate_;

    // 替换文本占位符
    size_t pos = prompt.find("{{TEXT}}");
    if (pos != String::npos) {
        prompt.replace(pos, 8, text);
    }

    // 构建实体类型描述
    std::ostringstream types;
    for (const auto& [type, desc] : entityTypes_) {
        types << "- " << type << ": " << desc << "\n";
    }

    pos = prompt.find("{{ENTITY_TYPES}}");
    if (pos != String::npos) {
        prompt.replace(pos, 16, types.str());
    }

    return prompt;
}

ExtractionResult EntityExtractor::extract(const String& text) {
    ExtractionResult result;
    result.source = text;

    String prompt = buildPrompt(text);
    Json response = llm_->chatJson(prompt);

    // 解析实体
    if (response.contains("entities")) {
        for (const auto& e : response["entities"]) {
            ExtractionResult::EntityMention mention;
            mention.text = e.value("text", "");
            mention.entityId = e.value("id", mention.text);
            mention.suggestedClass = e.value("class", "Entity");
            mention.confidence = e.value("confidence", 0.8f);

            // 在原文中定位
            size_t pos = text.find(mention.text);
            if (pos != String::npos) {
                mention.startPos = pos;
                mention.endPos = pos + mention.text.length();
            }

            result.entities.push_back(mention);
        }
    }

    // 解析关系
    if (response.contains("relations")) {
        for (const auto& r : response["relations"]) {
            ExtractionResult::RelationMention mention;
            mention.subject = r.value("subject", "");
            mention.predicate = r.value("predicate", "");
            mention.object = r.value("object", "");
            mention.confidence = r.value("confidence", 0.8f);
            result.relations.push_back(mention);
        }
    }

    return result;
}

std::vector<ExtractionResult> EntityExtractor::extractBatch(const std::vector<String>& texts) {
    std::vector<ExtractionResult> results;
    results.reserve(texts.size());
    for (const auto& text : texts) {
        results.push_back(extract(text));
    }
    return results;
}

void EntityExtractor::setPromptTemplate(const String& template_) {
    promptTemplate_ = template_;
}

ExtractionResult EntityExtractor::parseResult(const String& llmResponse, const String& source) {
    ExtractionResult result;
    result.source = source;

    Json response;
    try {
        size_t start = llmResponse.find('{');
        size_t end = llmResponse.rfind('}');
        if (start != String::npos && end != String::npos) {
            response = Json::parse(llmResponse.substr(start, end - start + 1));
        }
    } catch (...) {
        return result;
    }

    if (response.contains("entities")) {
        for (const auto& e : response["entities"]) {
            ExtractionResult::EntityMention mention;
            mention.text = e.value("text", "");
            mention.entityId = e.value("id", mention.text);
            mention.suggestedClass = e.value("class", "Entity");
            mention.confidence = e.value("confidence", 0.8f);
            size_t pos = source.find(mention.text);
            if (pos != String::npos) {
                mention.startPos = pos;
                mention.endPos = pos + mention.text.length();
            }
            result.entities.push_back(mention);
        }
    }

    if (response.contains("relations")) {
        for (const auto& r : response["relations"]) {
            ExtractionResult::RelationMention mention;
            mention.subject = r.value("subject", "");
            mention.predicate = r.value("predicate", "");
            mention.object = r.value("object", "");
            mention.confidence = r.value("confidence", 0.8f);
            result.relations.push_back(mention);
        }
    }

    return result;
}

// ============================================================================
// RelationExtractor 实现
// ============================================================================

RelationExtractor::RelationExtractor(std::shared_ptr<LLMInterface> llm, StoragePtr storage)
    : llm_(llm), storage_(storage) {
}

ExtractionResult RelationExtractor::extractOpen(const String& text) {
    ExtractionResult result;
    result.source = text;

    String prompt = R"(
从以下文本中进行开放关系抽取：

文本:
)" + text + R"(

请识别所有实体之间的关系，返回JSON格式:
{
  "relations": [
    {
      "subject": "主体",
      "predicate": "关系",
      "object": "客体",
      "confidence": 0.9,
      "evidence": "原文中的证据片段"
    }
  ]
}

只返回JSON。
)";

    Json response = llm_->chatJson(prompt);

    if (response.contains("relations")) {
        for (const auto& r : response["relations"]) {
            ExtractionResult::RelationMention mention;
            mention.subject = r.value("subject", "");
            mention.predicate = r.value("predicate", "");
            mention.object = r.value("object", "");
            mention.confidence = r.value("confidence", 0.8f);
            mention.evidence = r.value("evidence", "");
            result.relations.push_back(mention);
        }
    }

    return result;
}

ExtractionResult RelationExtractor::extract(const String& text, const std::vector<String>& knownEntities) {
    ExtractionResult result;
    result.source = text;

    String entityList;
    for (const auto& e : knownEntities) {
        entityList += "- " + e + "\n";
    }

    String prompt = buildPrompt(text, knownEntities);
    Json response = llm_->chatJson(prompt);

    if (response.contains("relations")) {
        for (const auto& r : response["relations"]) {
            ExtractionResult::RelationMention mention;
            mention.subject = r.value("subject", "");
            mention.predicate = r.value("predicate", "");
            mention.object = r.value("object", "");
            mention.confidence = r.value("confidence", 0.8f);
            mention.evidence = r.value("evidence", "");
            result.relations.push_back(mention);
        }
    }

    return result;
}

std::vector<String> RelationExtractor::resolveCoreference(const std::vector<String>& mentions) {
    if (mentions.empty()) return mentions;

    std::ostringstream prompt;
    prompt << R"(
对以下实体提及进行共指消解，找出哪些提及指向同一实体：

提及列表:
)";
    for (size_t i = 0; i < mentions.size(); ++i) {
        prompt << (i + 1) << ". " << mentions[i] << "\n";
    }
    prompt << R"(
返回JSON格式:
{
  "groups": [
    {"representative": "标准名称", "mentions": ["提及1", "提及2"]}
  ]
}
只返回JSON。
)";

    Json response = llm_->chatJson(prompt.str());

    std::vector<String> resolved;
    if (response.contains("groups")) {
        for (const auto& group : response["groups"]) {
            String rep = group.value("representative", "");
            if (!rep.empty()) {
                resolved.push_back(rep);
            }
        }
    }

    return resolved.empty() ? mentions : resolved;
}

String RelationExtractor::buildPrompt(const String& text, const std::vector<String>& entities) {
    std::ostringstream prompt;
    prompt << R"(
从以下文本中抽取实体间关系，仅考虑已知实体。

已知实体:
)";
    for (const auto& e : entities) {
        prompt << "- " << e << "\n";
    }
    prompt << "\n文本:\n" << text << R"(

返回JSON格式:
{
  "relations": [
    {"subject": "主体", "predicate": "关系", "object": "客体", "confidence": 0.9, "evidence": "证据"}
  ]
}
只返回JSON。
)";
    return prompt.str();
}

// ============================================================================
// RuleGenerator 实现
// ============================================================================

RuleGenerator::RuleGenerator(std::shared_ptr<LLMInterface> llm, StoragePtr storage)
    : llm_(llm), storage_(storage) {
}

SwrlRule RuleGenerator::generateFromNL(const String& description) {
    String prompt = R"(
将以下自然语言规则转换为SWRL格式：

规则描述: )" + description + R"(

SWRL语法说明:
- 变量用 ?x, ?y, ?z 表示
- 类原子: Class(?x)
- 关系原子: property(?x, ?y)
- 规则格式: 前提 -> 结论

返回JSON格式:
{
  "id": "规则ID",
  "name": "规则名称",
  "body": [
    {"type": 1, "propertyId": "关系名", "argument1": "?x", "argument2": "?y"}
  ],
  "head": [
    {"type": 1, "propertyId": "关系名", "argument1": "?x", "argument2": "?z"}
  ],
  "confidence": 0.9
}

type: 0=ClassAtom, 1=ObjectPropertyAtom, 2=DataPropertyAtom
只返回JSON。
)";

    Json response = llm_->chatJson(prompt);

    return SwrlRule::fromJson(response);
}

std::vector<SwrlRule> RuleGenerator::discoverRules(int minSupport, float minConfidence) {
    std::vector<SwrlRule> rules;

    // 获取所有三元组
    auto triples = storage_->getAllTriples();
    if (triples.size() < (size_t)minSupport) {
        return rules;
    }

    // 统计关系模式
    std::unordered_map<String, int> relationCounts;
    std::unordered_map<String, std::vector<Triple>> relationGroups;

    for (const auto& t : triples) {
        relationCounts[t.predicate]++;
        relationGroups[t.predicate].push_back(t);
    }

    // 寻找传递性模式
    for (const auto& [pred, count] : relationCounts) {
        if (count < minSupport) continue;

        const auto& group = relationGroups[pred];

        // 检查传递性: (a, pred, b) 和 (b, pred, c) 是否存在 (a, pred, c)
        int transitiveCount = 0;
        int possibleTransitive = 0;

        for (const auto& t1 : group) {
            for (const auto& t2 : group) {
                if (t1.object == t2.subject) {
                    possibleTransitive++;
                    // 检查是否存在 t1.subject -> t2.object
                    bool found = false;
                    for (const auto& t3 : group) {
                        if (t3.subject == t1.subject && t3.object == t2.object) {
                            found = true;
                            break;
                        }
                    }
                    if (found) transitiveCount++;
                }
            }
        }

        if (possibleTransitive > 0) {
            float confidence = (float)transitiveCount / possibleTransitive;
            if (confidence >= minConfidence) {
                // 发现传递性规则
                SwrlRule rule;
                rule.id = "auto_transitivity_" + pred;
                rule.name = pred + "的传递性 (自动发现)";
                rule.confidence = confidence;

                SwrlAtom atom1, atom2, atom3;
                atom1.type = SwrlAtomType::ObjectPropertyAtom;
                atom1.propertyId = pred;
                atom1.argument1 = "?x";
                atom1.argument2 = "?y";

                atom2.type = SwrlAtomType::ObjectPropertyAtom;
                atom2.propertyId = pred;
                atom2.argument1 = "?y";
                atom2.argument2 = "?z";

                atom3.type = SwrlAtomType::ObjectPropertyAtom;
                atom3.propertyId = pred;
                atom3.argument1 = "?x";
                atom3.argument2 = "?z";

                rule.body = {atom1, atom2};
                rule.head = {atom3};

                rules.push_back(rule);
            }
        }
    }

    return rules;
}

std::vector<SwrlRule> RuleGenerator::induceFromExamples(
    const std::vector<Triple>& positiveExamples,
    const std::vector<Triple>& negativeExamples
) {
    if (positiveExamples.empty()) return {};

    // Use FOIL algorithm to induce rules from examples
    SwrlRule rule = foilAlgorithm(positiveExamples, negativeExamples);
    if (rule.body.empty()) return {};

    return {rule};
}

bool RuleGenerator::validateRule(const SwrlRule& rule) {
    // Test rule against existing triples
    auto allTriples = storage_->getAllTriples();
    int supported = 0;
    int contradicted = 0;

    // Check if any existing triples match the rule body
    for (const auto& t : allTriples) {
        for (const auto& atom : rule.body) {
            if (atom.type == SwrlAtomType::ObjectPropertyAtom &&
                atom.propertyId == t.predicate) {
                supported++;
            }
        }
    }

    // Rule is valid if at least one triple supports it
    return supported > 0 && contradicted == 0;
}

SwrlRule RuleGenerator::foilAlgorithm(
    const std::vector<Triple>& positive,
    const std::vector<Triple>& negative
) {
    SwrlRule rule;
    rule.id = "induced_rule";
    rule.name = "Induced Rule";
    rule.confidence = 0.0f;

    if (positive.empty()) return rule;

    // Determine head predicate from positive examples (most common)
    std::unordered_map<String, int> predCounts;
    for (const auto& t : positive) {
        predCounts[t.predicate]++;
    }

    String headPred;
    int bestCount = 0;
    for (const auto& [pred, count] : predCounts) {
        if (count > bestCount) {
            bestCount = count;
            headPred = pred;
        }
    }

    // Create head atom
    SwrlAtom headAtom;
    headAtom.type = SwrlAtomType::ObjectPropertyAtom;
    headAtom.propertyId = headPred;
    headAtom.argument1 = "?x";
    headAtom.argument2 = "?y";
    rule.head = {headAtom};

    // Collect candidate body predicates (all predicates except the head)
    std::vector<String> candidatePreds;
    auto* ts = storage_ ? storage_->getTripleStore() : nullptr;
    if (ts) {
        auto allPreds = ts->getAllPredicates();
        for (const auto& p : allPreds) {
            if (p != headPred) candidatePreds.push_back(p);
        }
    }
    for (const auto& t : positive) {
        if (t.predicate != headPred) {
            if (std::find(candidatePreds.begin(), candidatePreds.end(), t.predicate) == candidatePreds.end()) {
                candidatePreds.push_back(t.predicate);
            }
        }
    }
    for (const auto& t : negative) {
        if (t.predicate != headPred) {
            if (std::find(candidatePreds.begin(), candidatePreds.end(), t.predicate) == candidatePreds.end()) {
                candidatePreds.push_back(t.predicate);
            }
        }
    }

    // FOIL specialization loop
    auto countBindings = [&](const std::vector<SwrlAtom>& body,
                            const std::vector<Triple>& examples) -> int {
        if (!ts || body.empty()) return static_cast<int>(examples.size());
        int count = 0;
        for (const auto& ex : examples) {
            String xVal = ex.subject;
            String yVal = ex.object;
            bool allMatch = true;
            for (const auto& atom : body) {
                if (atom.argument1 == "?x") {
                    auto results = ts->findBySP(xVal, atom.propertyId);
                    if (results.empty()) { allMatch = false; break; }
                    if (atom.argument2 == "?y") {
                        bool found = false;
                        for (const auto& r : results) {
                            if (r.object == yVal) { found = true; break; }
                        }
                        if (!found) { allMatch = false; break; }
                    }
                } else if (atom.argument1 == "?y") {
                    auto results = ts->findBySP(yVal, atom.propertyId);
                    if (results.empty()) { allMatch = false; break; }
                }
            }
            if (allMatch) count++;
        }
        return count;
    };

    std::vector<SwrlAtom> body;
    int p0 = static_cast<int>(positive.size());
    int n0 = static_cast<int>(negative.size());
    float bestGain = -1e30f;
    std::vector<SwrlAtom> bestBody;

    // Try adding each candidate predicate as a body atom
    for (const auto& candPred : candidatePreds) {
        for (const char* arg2 : {"?z", "?y"}) {
            std::vector<SwrlAtom> testBody = body;
            SwrlAtom atom;
            atom.type = SwrlAtomType::ObjectPropertyAtom;
            atom.propertyId = candPred;
            atom.argument1 = "?x";
            atom.argument2 = arg2;
            testBody.push_back(atom);

            int p1 = countBindings(testBody, positive);
            int n1 = countBindings(testBody, negative);

            if (p1 == 0) continue;

            float p0f = static_cast<float>(p0);
            float n0f = static_cast<float>(n0);
            float p1f = static_cast<float>(p1);
            float n1f = static_cast<float>(n1);

            float oldEnt = (p0f + n0f > 0) ? std::log2(p0f / (p0f + n0f)) : 0.0f;
            float newEnt = (p1f + n1f > 0) ? std::log2(p1f / (p1f + n1f)) : 0.0f;
            float gain = p1f * (newEnt - oldEnt);

            if (gain > bestGain) {
                bestGain = gain;
                bestBody = testBody;
            }
        }
    }

    rule.body = bestBody;

    // Calculate confidence
    int pFinal = bestBody.empty() ? p0 : countBindings(bestBody, positive);
    int nFinal = bestBody.empty() ? n0 : countBindings(bestBody, negative);
    float posCov = static_cast<float>(pFinal) / static_cast<float>(positive.size());
    float negCov = negative.empty() ? 0.0f
        : static_cast<float>(nFinal) / static_cast<float>(negative.size());

    rule.confidence = posCov / (posCov + negCov + 0.001f);

    return rule;
}

// ============================================================================
// OntologyLearner 实现
// ============================================================================

OntologyLearner::OntologyLearner(std::shared_ptr<LLMInterface> llm, StoragePtr storage)
    : llm_(llm), storage_(storage) {
}

std::vector<OntologySuggestion::RelationSuggestion> OntologyLearner::discoverRelations(int minSupport) {
    std::vector<OntologySuggestion::RelationSuggestion> suggestions;

    auto triples = storage_->getAllTriples();

    // 统计共同出现
    std::unordered_map<String, std::unordered_map<String, int>> cooccurrence;

    for (const auto& t1 : triples) {
        for (const auto& t2 : triples) {
            if (t1.subject == t2.subject && t1.predicate != t2.predicate) {
                cooccurrence[t1.predicate][t2.predicate]++;
            }
        }
    }

    // 寻找强关联的关系对
    for (const auto& [pred1, counts] : cooccurrence) {
        for (const auto& [pred2, count] : counts) {
            if (count >= minSupport) {
                // 建议复合规则
                OntologySuggestion::RelationSuggestion suggestion;
                suggestion.predicate = pred1 + "_and_" + pred2;
                suggestion.confidence = (float)count / triples.size();
                suggestion.source = "cooccurrence_analysis";
                suggestions.push_back(suggestion);
            }
        }
    }

    return suggestions;
}

void OntologyLearner::incrementalLearn(const Triple& newTriple) {
    // 更新支持度统计
    relationSupport_[newTriple.predicate]++;

    // 更新共现统计
    auto related = storage_->queryTriples(
        TripleStore::TriplePattern{newTriple.subject, "", "", false, true, true});

    for (const auto& t : related) {
        if (t.predicate != newTriple.predicate) {
            cooccurrence_[newTriple.predicate][t.predicate]++;
        }
    }

    // 检查是否可以归纳新规则
    if (relationSupport_[newTriple.predicate] >= 10) {
        tryInduction(newTriple.predicate);
    }
}

bool OntologyLearner::tryInduction(const String& pattern) {
    // 简化的归纳学习
    // Check if the pattern appears frequently enough to suggest a rule
    auto it = relationSupport_.find(pattern);
    if (it == relationSupport_.end() || it->second < config_.minSamplesForInduction) {
        return false;
    }

    // Check co-occurrence for potential rule body
    auto coit = cooccurrence_.find(pattern);
    if (coit == cooccurrence_.end()) return false;

    for (const auto& [otherPred, count] : coit->second) {
        float confidence = static_cast<float>(count) / it->second;
        if (confidence >= config_.inductionThreshold) {
            return true; // Pattern strong enough for induction
        }
    }

    return false;
}

OntologySuggestion OntologyLearner::learn(const std::vector<String>& documents) {
    OntologySuggestion suggestion;

    // Learn from each document
    for (const auto& doc : documents) {
        String prompt = R"(
从以下文本中学习本体结构，识别类层次、属性和实例：

文本:
)" + doc + R"(

返回JSON格式:
{
  "hierarchy": [
    {"parent": "父类", "child": "子类", "confidence": 0.9, "reason": "原因"}
  ],
  "properties": [
    {"domain": "域类", "property": "属性名", "range": "值类型", "confidence": 0.8, "isObjectProperty": true}
  ],
  "instances": [
    {"individualId": "实例ID", "classId": "类ID", "attributes": {"key": "value"}, "confidence": 0.9}
  ]
}
只返回JSON。
)";
        Json response = llm_->chatJson(prompt);

        if (response.contains("hierarchy")) {
            for (const auto& h : response["hierarchy"]) {
                OntologySuggestion::HierarchySuggestion hs;
                hs.parent = h.value("parent", "");
                hs.child = h.value("child", "");
                hs.confidence = h.value("confidence", 0.8f);
                hs.reason = h.value("reason", "");
                suggestion.hierarchy.push_back(hs);
            }
        }

        if (response.contains("properties")) {
            for (const auto& p : response["properties"]) {
                OntologySuggestion::PropertySuggestion ps;
                ps.domain = p.value("domain", "");
                ps.property = p.value("property", "");
                ps.range = p.value("range", "");
                ps.confidence = p.value("confidence", 0.8f);
                ps.isObjectProperty = p.value("isObjectProperty", true);
                suggestion.properties.push_back(ps);
            }
        }

        if (response.contains("instances")) {
            for (const auto& i : response["instances"]) {
                OntologySuggestion::InstanceSuggestion is;
                is.individualId = i.value("individualId", "");
                is.classId = i.value("classId", "");
                is.confidence = i.value("confidence", 0.8f);
                if (i.contains("attributes")) {
                    for (auto& [k, v] : i["attributes"].items()) {
                        is.attributes.push_back({k, v.is_string() ? v.get<String>() : v.dump()});
                    }
                }
                suggestion.instances.push_back(is);
            }
        }
    }

    // Also add relation suggestions from statistical analysis
    suggestion.relations = discoverRelations();

    return suggestion;
}

std::vector<OntologySuggestion::HierarchySuggestion> OntologyLearner::learnHierarchy(
    const std::vector<Class>& existingClasses
) {
    std::vector<OntologySuggestion::HierarchySuggestion> suggestions;

    if (existingClasses.size() < 2) return suggestions;

    std::ostringstream classList;
    for (const auto& cls : existingClasses) {
        classList << "- " << cls.name << " (" << cls.id << ")";
        if (!cls.description.empty()) classList << ": " << cls.description;
        classList << "\n";
    }

    String prompt = R"(
分析以下类之间的层次关系，找出潜在的继承关系：

类列表:
)" + classList.str() + R"(

返回JSON格式:
{
  "hierarchy": [
    {"parent": "父类ID", "child": "子类ID", "confidence": 0.9, "reason": "原因"}
  ]
}
只返回JSON。
)";

    Json response = llm_->chatJson(prompt);

    if (response.contains("hierarchy")) {
        for (const auto& h : response["hierarchy"]) {
            OntologySuggestion::HierarchySuggestion hs;
            hs.parent = h.value("parent", "");
            hs.child = h.value("child", "");
            hs.confidence = h.value("confidence", 0.8f);
            hs.reason = h.value("reason", "");
            suggestions.push_back(hs);
        }
    }

    return suggestions;
}

std::vector<OntologySuggestion::PropertySuggestion> OntologyLearner::learnProperties(
    const String& classId
) {
    std::vector<OntologySuggestion::PropertySuggestion> suggestions;

    auto cls = storage_->getClass(classId);
    if (!cls) return suggestions;

    // Get existing instances of this class
    auto instances = storage_->getIndividualsByClass(classId);

    String prompt = R"(
分析类 ")" + cls->name + R"(" 的实例数据，建议该类应该有哪些属性：

类描述: )" + cls->description + R"(
实例数量: )" + std::to_string(instances.size()) + R"(

返回JSON格式:
{
  "properties": [
    {"domain": "域类", "property": "属性名", "range": "值类型", "confidence": 0.8, "isObjectProperty": false}
  ]
}
只返回JSON。
)";

    Json response = llm_->chatJson(prompt);

    if (response.contains("properties")) {
        for (const auto& p : response["properties"]) {
            OntologySuggestion::PropertySuggestion ps;
            ps.domain = p.value("domain", classId);
            ps.property = p.value("property", "");
            ps.range = p.value("range", "");
            ps.confidence = p.value("confidence", 0.8f);
            ps.isObjectProperty = p.value("isObjectProperty", false);
            suggestions.push_back(ps);
        }
    }

    return suggestions;
}

// ============================================================================
// AutoModelEngine 实现
// ============================================================================

AutoModelEngine::AutoModelEngine(StoragePtr storage, const AutoModelConfig& config)
    : storage_(storage), config_(config) {

    llm_ = std::make_shared<LLMInterface>(config);
    entityExtractor_ = std::make_shared<EntityExtractor>(llm_, storage);
    relationExtractor_ = std::make_shared<RelationExtractor>(llm_, storage);
    ontologyLearner_ = std::make_shared<OntologyLearner>(llm_, storage);
    ruleGenerator_ = std::make_shared<RuleGenerator>(llm_, storage);
    neuralReasoner_ = std::make_shared<NeuralReasoner>(storage, 128);
}

AutoModelEngine::~AutoModelEngine() {
}

bool AutoModelEngine::initialize(const String& apiKey, const String& endpoint) {
    llm_->setApiKey(apiKey);
    if (!endpoint.empty()) {
        llm_->setEndpoint(endpoint);
    }
    llmInitialized_ = true;
    return true;
}

// ===== 文本驱动的建模 =====

ExtractionResult AutoModelEngine::buildFromText(const String& text) {
    if (!llmInitialized_) {
        return ExtractionResult();
    }

    // 实体抽取
    auto entityResult = entityExtractor_->extract(text);

    // 关系抽取
    auto relationResult = relationExtractor_->extractOpen(text);

    // 合并结果
    ExtractionResult result;
    result.entities = entityResult.entities;
    result.relations = relationResult.relations;
    result.source = text;

    // 将抽取结果添加到存储
    for (const auto& e : result.entities) {
        Individual ind;
        ind.id = e.entityId;
        ind.name = e.text;
        ind.classId = e.suggestedClass;
        storage_->addIndividual(ind);
    }

    for (const auto& r : result.relations) {
        if (r.confidence >= config_.confidenceThreshold) {
            Triple t;
            t.subject = r.subject;
            t.predicate = r.predicate;
            t.object = r.object;
            t.confidence = r.confidence;
            storage_->addTriple(t);

            // 增量学习
            if (config_.enableIncrementalLearning) {
                ontologyLearner_->incrementalLearn(t);
            }
        }
    }

    return result;
}

std::vector<ExtractionResult> AutoModelEngine::buildFromDocuments(const std::vector<String>& documents) {
    return entityExtractor_->extractBatch(documents);
}

ExtractionResult AutoModelEngine::buildFromStructured(const Json& data) {
    ExtractionResult result;

    // 从结构化数据构建本体
    // 支持 JSON 格式的实体和关系定义

    if (data.contains("entities")) {
        for (const auto& e : data["entities"]) {
            ExtractionResult::EntityMention mention;
            mention.text = e.value("name", "");
            mention.entityId = e.value("id", mention.text);
            mention.suggestedClass = e.value("type", "Entity");
            mention.confidence = 1.0f;
            result.entities.push_back(mention);

            // 添加到存储
            Individual ind;
            ind.id = mention.entityId;
            ind.name = mention.text;
            ind.classId = mention.suggestedClass;
            if (e.contains("attributes")) {
                for (auto& [key, val] : e["attributes"].items()) {
                    ind.properties[key] = val;
                }
            }
            storage_->addIndividual(ind);
        }
    }

    if (data.contains("relations")) {
        for (const auto& r : data["relations"]) {
            ExtractionResult::RelationMention mention;
            mention.subject = r.value("subject", "");
            mention.predicate = r.value("predicate", "");
            mention.object = r.value("object", "");
            mention.confidence = r.value("confidence", 1.0f);
            result.relations.push_back(mention);

            if (mention.confidence >= config_.confidenceThreshold) {
                Triple t;
                t.subject = mention.subject;
                t.predicate = mention.predicate;
                t.object = mention.object;
                t.confidence = mention.confidence;
                storage_->addTriple(t);
            }
        }
    }

    return result;
}

void AutoModelEngine::addKnowledge(const Triple& triple, bool retrain) {
    storage_->addTriple(triple);

    if (config_.enableIncrementalLearning) {
        ontologyLearner_->incrementalLearn(triple);
    }

    if (retrain) {
        // 重新训练嵌入
        trainEmbeddings(10, 0.01f);
    }
}

void AutoModelEngine::addClass(const Class& cls, bool suggestHierarchy) {
    storage_->addClass(cls);

    if (suggestHierarchy && llmInitialized_) {
        // 使用 LLM 建议类层次
        String prompt = R"(
给定一个新的类: )" + cls.name + R"( (描述: )" + cls.description + R"()
和现有类列表:
)" + getClassList() + R"(

请建议这个类应该继承哪些父类，以及哪些现有类应该继承它。
返回 JSON 格式:
{
  "superClasses": ["父类1", "父类2"],
  "subClasses": ["子类1"]
}
)";

        Json response = llm_->chatJson(prompt);

        if (response.contains("superClasses")) {
            for (const auto& super : response["superClasses"]) {
                if (super.is_string()) {
                    Class updated = cls;
                    updated.superClasses.push_back(super.get<String>());
                    storage_->updateClass(updated);
                }
            }
        }
    }
}

String AutoModelEngine::getClassList() {
    auto classes = storage_->getAllClasses();
    std::ostringstream oss;
    for (const auto& cls : classes) {
        oss << "- " << cls.name << " (" << cls.id << ")\n";
    }
    return oss.str();
}

int AutoModelEngine::importAndLearn(const std::vector<Triple>& triples) {
    int imported = 0;

    for (const auto& t : triples) {
        auto existing = storage_->queryTriples(
            TripleStore::TriplePattern{t.subject, t.predicate, t.object});
        if (existing.empty()) {
            storage_->addTriple(t);
            imported++;

            if (config_.enableIncrementalLearning) {
                ontologyLearner_->incrementalLearn(t);
            }
        }
    }

    // Discover and add new rules
    if (imported > 0) {
        auto newRules = ruleGenerator_->discoverRules(5, 0.7f);
        auto* ts = storage_->getTripleStore();
        for (const auto& rule : newRules) {
            if (ruleGenerator_->validateRule(rule)) {
                ts->add({rule.id, "http://www.w3.org/1999/02/22-rdf-syntax-ns#type",
                         "http://www.w3.org/2003/11/swrl#Imp"});
                ts->add({rule.id, "http://www.w3.org/2000/01/rdf-schema#label", rule.name});
            }
        }
    }

    return imported;
}

std::vector<Individual> AutoModelEngine::discoverEntities(int limit) {
    std::vector<Individual> discovered;

    if (!llmInitialized_) return discovered;

    // 使用嵌入相似度发现潜在实体
    auto individuals = storage_->getAllIndividuals();

    if (embeddingsTrained_) {
        // 寻找高相似度但未连接的实体对
        for (size_t i = 0; i < individuals.size() && (int)discovered.size() < limit; ++i) {
            for (size_t j = i + 1; j < individuals.size() && (int)discovered.size() < limit; ++j) {
                auto emb1 = neuralReasoner_->getEmbedding(individuals[i].id);
                auto emb2 = neuralReasoner_->getEmbedding(individuals[j].id);

                if (!emb1.empty() && !emb2.empty()) {
                    float sim = 0.0f, norm1 = 0.0f, norm2 = 0.0f;
                    for (size_t k = 0; k < emb1.size(); ++k) {
                        sim += emb1[k] * emb2[k];
                        norm1 += emb1[k] * emb1[k];
                        norm2 += emb2[k] * emb2[k];
                    }
                    sim /= (std::sqrt(norm1) * std::sqrt(norm2));

                    // 高相似度但没有直接关系
                    if (sim > 0.85f) {
                        auto relations = storage_->queryTriples(
                            TripleStore::TriplePattern{individuals[i].id, "", individuals[j].id, false, true, false});
                        if (relations.empty()) {
                            discovered.push_back(individuals[j]);
                        }
                    }
                }
            }
        }
    }

    return discovered;
}

std::vector<std::pair<String, float>> AutoModelEngine::discoverRelations(int limit) {
    std::vector<std::pair<String, float>> result;
    auto suggestions = ontologyLearner_->discoverRelations(limit);
    for (const auto& s : suggestions) {
        result.push_back({s.predicate, s.confidence});
    }
    return result;
}

std::vector<std::pair<String, String>> AutoModelEngine::discoverHierarchy() {
    std::vector<std::pair<String, String>> hierarchy;

    if (!llmInitialized_) return hierarchy;

    auto classes = storage_->getAllClasses();

    // 使用 LLM 分析类层次
    for (const auto& cls : classes) {
        String prompt = R"(
分析类 ")" + cls.name + R"(" 的层次位置。

现有类:
)" + getClassList() + R"(

请建议这个类的父类和子类。返回 JSON:
{
  "superClasses": ["父类ID列表"],
  "subClasses": ["子类ID列表"]
}
)";

        Json response = llm_->chatJson(prompt);

        if (response.contains("superClasses")) {
            for (const auto& super : response["superClasses"]) {
                if (super.is_string()) {
                    hierarchy.push_back({cls.id, super.get<String>()});
                }
            }
        }
    }

    return hierarchy;
}

std::vector<String> AutoModelEngine::optimize() {
    std::vector<String> optimizations;

    // 1. 检测冗余关系
    auto triples = storage_->getAllTriples();
    std::unordered_map<String, int> predicateCount;
    for (const auto& t : triples) {
        predicateCount[t.predicate]++;
    }

    // 2. 检测潜在传递性
    for (const auto& [pred, count] : predicateCount) {
        if (count > 10) {
            // 检查传递性模式
            auto related = storage_->queryTriples(TripleStore::TriplePattern{"", pred, "", true, false, true});
            int transitivePairs = 0;

            for (const auto& t1 : related) {
                for (const auto& t2 : related) {
                    if (t1.object == t2.subject) {
                        auto check = storage_->queryTriples(
                            TripleStore::TriplePattern{t1.subject, pred, t2.object});
                        if (!check.empty()) transitivePairs++;
                    }
                }
            }

            if (transitivePairs > 5) {
                optimizations.push_back("建议: " + pred + " 可能是传递性关系，考虑添加传递性规则");
            }
        }
    }

    // 3. 检测孤立实体
    auto individuals = storage_->getAllIndividuals();
    for (const auto& ind : individuals) {
        auto related = storage_->queryTriples(TripleStore::TriplePattern{ind.id, "", "", false, true, true});
        auto asObject = storage_->queryTriples(TripleStore::TriplePattern{"", "", ind.id, true, true, false});

        if (related.empty() && asObject.empty()) {
            optimizations.push_back("警告: 实体 " + ind.name + " 是孤立的，没有关系");
        }
    }

    return optimizations;
}

void AutoModelEngine::resolveConflict(const String& conflictId, bool dryRun) {
    if (!llmInitialized_) return;

    String prompt = R"(
Detected conflict: )" + conflictId + R"(

Analyze this conflict and suggest resolution actions.
Use the following format for each action:
REMOVE_TRIPLE(subject predicate object)
ADD_TRIPLE(subject predicate object)
MODIFY_CLASS(className property value)

List each action on a separate line.
)";

    String suggestion = llm_->chat(prompt);
    auto actions = parseConflictActions(suggestion);

    if (dryRun) return;

    auto* ts = storage_->getTripleStore();
    for (const auto& action : actions) {
        switch (action.type) {
            case ConflictAction::RemoveTriple:
                ts->remove({action.subject, action.predicate, action.object});
                break;
            case ConflictAction::AddTriple:
                ts->add({action.subject, action.predicate, action.object});
                break;
            case ConflictAction::ModifyClass:
                ts->remove({action.subject, "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", ""});
                ts->add({action.subject, "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", action.object});
                break;
        }
    }
}

std::vector<ConflictAction> AutoModelEngine::parseConflictActions(const String& llmResponse) {
    std::vector<ConflictAction> actions;
    std::istringstream stream(llmResponse);
    String line;

    while (std::getline(stream, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end = line.find_last_not_of(" \t\r\n");
        if (start == String::npos) continue;
        line = line.substr(start, end - start + 1);

        if (line.find("REMOVE_TRIPLE(") == 0) {
            size_t parenStart = line.find('(');
            size_t parenEnd = line.find(')');
            if (parenStart != String::npos && parenEnd != String::npos) {
                String args = line.substr(parenStart + 1, parenEnd - parenStart - 1);
                std::istringstream argStream(args);
                String s, p, o;
                argStream >> s >> p >> o;
                actions.push_back({ConflictAction::RemoveTriple, s, p, o, line});
            }
        }
        else if (line.find("ADD_TRIPLE(") == 0) {
            size_t parenStart = line.find('(');
            size_t parenEnd = line.find(')');
            if (parenStart != String::npos && parenEnd != String::npos) {
                String args = line.substr(parenStart + 1, parenEnd - parenStart - 1);
                std::istringstream argStream(args);
                String s, p, o;
                argStream >> s >> p >> o;
                actions.push_back({ConflictAction::AddTriple, s, p, o, line});
            }
        }
        else if (line.find("MODIFY_CLASS(") == 0) {
            size_t parenStart = line.find('(');
            size_t parenEnd = line.find(')');
            if (parenStart != String::npos && parenEnd != String::npos) {
                String args = line.substr(parenStart + 1, parenEnd - parenStart - 1);
                std::istringstream argStream(args);
                String cls, prop, val;
                argStream >> cls >> prop >> val;
                actions.push_back({ConflictAction::ModifyClass, cls, prop, val, line});
            }
        }
    }

    return actions;
}

void AutoModelEngine::mergeOntologies(const std::vector<Triple>& externalTriples,
                                      const String& sourceId,
                                      const String& sourceName) {
    // 实体对齐
    std::vector<String> localEntities, externalEntities;
    auto localIndividuals = storage_->getAllIndividuals();

    for (const auto& ind : localIndividuals) {
        localEntities.push_back(ind.id);
    }

    std::unordered_set<String> externalEntitySet;
    for (const auto& t : externalTriples) {
        externalEntitySet.insert(t.subject);
        externalEntitySet.insert(t.object);
    }
    for (const auto& e : externalEntitySet) {
        externalEntities.push_back(e);
    }

    auto alignments = alignEntities(localEntities, externalEntities);

    // 根据对齐结果合并
    for (const auto& t : externalTriples) {
        Triple merged = t;

        // 检查主体是否需要对齐
        for (const auto& al : alignments) {
            if (merged.subject == al.entity2) merged.subject = al.entity1;
            if (merged.object == al.entity2) merged.object = al.entity1;
        }

        // 添加来源追踪
        Provenance prov;
        prov.sourceId = sourceId;
        prov.sourceName = sourceName;
        prov.confidence = t.confidence;
        provenanceIndex_[tripleHash(merged)] = prov;

        // 添加合并后的三元组
        storage_->addTriple(merged);
    }
}

String AutoModelEngine::generateDocumentation() {
    std::ostringstream doc;

    doc << "# 本体文档\n\n";
    doc << "生成时间: " << __DATE__ << " " << __TIME__ << "\n\n";

    // 类文档
    doc << "## 类\n\n";
    auto classes = storage_->getAllClasses();
    for (const auto& cls : classes) {
        doc << "### " << cls.name << " (`" << cls.id << "`)\n\n";
        if (!cls.description.empty()) {
            doc << cls.description << "\n\n";
        }
        if (!cls.superClasses.empty()) {
            doc << "**父类**: ";
            for (size_t i = 0; i < cls.superClasses.size(); ++i) {
                if (i > 0) doc << ", ";
                doc << cls.superClasses[i];
            }
            doc << "\n\n";
        }

        // 列出该类的实例
        auto instances = storage_->getIndividualsByClass(cls.id);
        if (!instances.empty()) {
            doc << "**实例**: ";
            for (size_t i = 0; i < instances.size() && i < 10; ++i) {
                if (i > 0) doc << ", ";
                doc << instances[i].name;
            }
            if (instances.size() > 10) doc << " ... (共 " << instances.size() << " 个)";
            doc << "\n\n";
        }
    }

    // 关系文档
    doc << "## 关系\n\n";
    auto triples = storage_->getAllTriples();
    std::unordered_set<String> predicates;
    for (const auto& t : triples) {
        predicates.insert(t.predicate);
    }

    for (const auto& pred : predicates) {
        doc << "### " << pred << "\n\n";

        // 统计使用次数
        auto related = storage_->queryTriples(TripleStore::TriplePattern{"", pred, "", true, false, true});
        doc << "**使用次数**: " << related.size() << "\n\n";

        // 示例
        doc << "**示例**:\n";
        int examples = 0;
        for (const auto& t : related) {
            if (examples++ >= 3) break;
            doc << "- " << t.subject << " → " << t.object << "\n";
        }
        doc << "\n";
    }

    return doc.str();
}

std::vector<std::pair<String, String>> AutoModelEngine::exportTrainingData() {
    std::vector<std::pair<String, String>> data;

    auto triples = storage_->getAllTriples();

    for (const auto& t : triples) {
        // 生成正例
        String input = t.subject + " " + t.predicate + " ?";
        String output = t.object;
        data.push_back({input, output});

        // 生成反例（如果可能）
    }

    return data;
}

// ===== 交互式建模 =====

String AutoModelEngine::answerQuestion(const String& question) {
    // 获取当前本体状态
    auto stats = storage_->getAllTriples();

    String prompt = R"(
你是一个本体建模助手。用户会问关于当前本体的问题，请根据上下文回答。

当前本体包含 )" + std::to_string(stats.size()) + R"( 个三元组。

用户问题: )" + question + R"(

请用中文回答。
)";

    return llm_->chat(prompt);
}

OntologySuggestion AutoModelEngine::getSuggestions() {
    OntologySuggestion suggestions;

    // 发现潜在关系
    suggestions.relations = ontologyLearner_->discoverRelations();

    return suggestions;
}

String AutoModelEngine::explainInference(const Triple& result) {
    std::ostringstream explanation;

    explanation << "推理结果解释:\n";
    explanation << "  " << result.subject << " " << result.predicate << " " << result.object;
    if (result.confidence < 1.0f) {
        explanation << " (置信度: " << result.confidence << ")";
    }
    explanation << "\n\n";

    // Find supporting evidence
    auto related = storage_->queryTriples(
        TripleStore::TriplePattern{result.subject, "", "", false, true, true});
    explanation << "相关事实:\n";
    for (const auto& t : related) {
        if (t.object != result.object || t.predicate != result.predicate) {
            explanation << "  - " << t.subject << " " << t.predicate << " " << t.object << "\n";
        }
    }

    // Find path from subject to object
    auto paths = storage_->findPath(result.subject, result.object, "", 4);
    if (!paths.empty()) {
        explanation << "\n推理路径:\n";
        for (const auto& path : paths) {
            if (path.size() <= 4) {
                explanation << "  ";
                for (size_t i = 0; i < path.size(); ++i) {
                    explanation << path[i];
                    if (i + 1 < path.size()) explanation << " -> ";
                }
                explanation << "\n";
            }
        }
    }

    // LLM explanation if available
    if (llmInitialized_) {
        String prompt = R"(
解释以下推理结果的逻辑依据：
)" + result.subject + " " + result.predicate + " " + result.object + R"(
置信度: )" + std::to_string(result.confidence) + R"(

请用简洁的中文解释这个推理是如何得出的。
)";
        String llmExplanation = llm_->chat(prompt);
        if (!llmExplanation.empty()) {
            explanation << "\n" << llmExplanation;
        }
    }

    return explanation.str();
}

// ===== 自动发现 =====

std::vector<SwrlRule> AutoModelEngine::discoverRules(int minSupport) {
    return ruleGenerator_->discoverRules(minSupport, config_.inductionThreshold);
}

// ===== 模型优化 =====

void AutoModelEngine::trainEmbeddings(int epochs, float lr) {
    neuralReasoner_->trainEmbeddings(epochs, lr);
    embeddingsTrained_ = true;
}

std::vector<OntologyConflict> AutoModelEngine::detectConflicts() {
    std::vector<OntologyConflict> conflicts;
    auto* ts = storage_->getTripleStore();
    if (!ts) return conflicts;

    // ---- 1. Disjoint class assertion conflicts ----
    std::vector<std::pair<String, String>> disjointPairs;
    auto disjointTriples = ts->findByPredicate(
        "http://www.w3.org/2002/07/owl#disjointWith");
    for (const auto& dt : disjointTriples) {
        disjointPairs.push_back({dt.subject, dt.object});
    }

    std::unordered_map<String, std::vector<String>> individualTypes;
    auto typeTriples = ts->findByPredicate(
        "http://www.w3.org/1999/02/22-rdf-syntax-ns#type");
    for (const auto& tt : typeTriples) {
        individualTypes[tt.subject].push_back(tt.object);
    }

    for (const auto& [individual, types] : individualTypes) {
        for (size_t i = 0; i < types.size(); ++i) {
            for (size_t j = i + 1; j < types.size(); ++j) {
                for (const auto& [c1, c2] : disjointPairs) {
                    if ((types[i] == c1 && types[j] == c2) ||
                        (types[i] == c2 && types[j] == c1)) {
                        OntologyConflict c;
                        c.type = OntologyConflict::DisjointClassAssertion;
                        c.description = individual + " is typed as both " +
                            types[i] + " and " + types[j] + " which are disjoint";
                        c.severity = 1.0f;
                        for (const auto& tt : typeTriples) {
                            if (tt.subject == individual &&
                                (tt.object == types[i] || tt.object == types[j])) {
                                c.conflictingTriples.push_back(tt);
                            }
                        }
                        conflicts.push_back(c);
                    }
                }
            }
        }
    }

    // ---- 2. Functional property violations ----
    auto funcPropTriples = ts->findByPO(
        "http://www.w3.org/1999/02/22-rdf-syntax-ns#type",
        "http://www.w3.org/2002/07/owl#FunctionalProperty");

    for (const auto& fp : funcPropTriples) {
        String prop = fp.subject;
        std::unordered_map<String, std::vector<Triple>> subjectValues;
        auto propTriples = ts->findByPredicate(prop);
        for (const auto& pt : propTriples) {
            subjectValues[pt.subject].push_back(pt);
        }
        for (const auto& [subj, vals] : subjectValues) {
            if (vals.size() > 1) {
                OntologyConflict c;
                c.type = OntologyConflict::FunctionalPropertyViolation;
                c.description = subj + " has " + std::to_string(vals.size()) +
                    " values for functional property " + prop;
                c.conflictingTriples = vals;
                c.severity = 0.9f;
                conflicts.push_back(c);
            }
        }
    }

    return conflicts;
}

// ===== 知识融合 =====

std::vector<AlignmentResult> AutoModelEngine::alignEntities(
    const std::vector<String>& entities1,
    const std::vector<String>& entities2) {

    std::vector<AlignmentResult> alignments;

    for (const auto& e1 : entities1) {
        for (const auto& e2 : entities2) {
            AlignmentResult ar;
            ar.entity1 = e1;
            ar.entity2 = e2;

            // 1. Embedding similarity
            ar.embeddingScore = 0.0f;
            if (embeddingsTrained_ && neuralReasoner_) {
                auto emb1 = neuralReasoner_->getEmbedding(e1);
                auto emb2 = neuralReasoner_->getEmbedding(e2);
                if (!emb1.empty() && !emb2.empty() && emb1.size() == emb2.size()) {
                    float dot = 0, norm1 = 0, norm2 = 0;
                    for (size_t i = 0; i < emb1.size(); ++i) {
                        dot += emb1[i] * emb2[i];
                        norm1 += emb1[i] * emb1[i];
                        norm2 += emb2[i] * emb2[i];
                    }
                    if (norm1 > 0 && norm2 > 0) {
                        ar.embeddingScore = dot / (std::sqrt(norm1) * std::sqrt(norm2));
                    }
                }
            }

            // 2. Structural similarity (Jaccard of shared properties)
            ar.structuralScore = jaccardCoefficient(e1, e2);

            // 3. Label similarity (normalized Levenshtein)
            int dist = levenshteinDistance(e1, e2);
            int maxLen = std::max(static_cast<int>(e1.size()), static_cast<int>(e2.size()));
            ar.labelScore = maxLen > 0 ? 1.0f - static_cast<float>(dist) / maxLen : 0.0f;

            // Combined score
            ar.combinedScore =
                alignWeightEmbedding_ * ar.embeddingScore +
                alignWeightStructural_ * ar.structuralScore +
                alignWeightLabel_ * ar.labelScore;

            if (ar.combinedScore >= 0.3f) {
                alignments.push_back(ar);
            }
        }
    }

    std::sort(alignments.begin(), alignments.end(),
        [](const AlignmentResult& a, const AlignmentResult& b) {
            return a.combinedScore > b.combinedScore;
        });

    return alignments;
}

// ===== 导出与解释 =====

String AutoModelEngine::describeOntology() {
    auto triples = storage_->getAllTriples();
    auto classes = storage_->getAllClasses();
    auto individuals = storage_->getAllIndividuals();

    std::ostringstream oss;
    oss << "## 本体描述\n\n";
    oss << "### 统计信息\n";
    oss << "- 类数量: " << classes.size() << "\n";
    oss << "- 实体数量: " << individuals.size() << "\n";
    oss << "- 三元组数量: " << triples.size() << "\n\n";

    oss << "### 类层次\n";
    for (const auto& cls : classes) {
        oss << "- " << cls.name << " (" << cls.id << ")\n";
    }

    oss << "\n### 关系类型\n";
    std::unordered_set<String> predicates;
    for (const auto& t : triples) {
        predicates.insert(t.predicate);
    }
    for (const auto& pred : predicates) {
        oss << "- " << pred << "\n";
    }

    return oss.str();
}

// ============================================================================
// 便捷函数
// ============================================================================

ExtractionResult quickBuild(StoragePtr storage, const String& text, const String& apiKey) {
    AutoModelConfig config;
    config.llmApiKey = apiKey;

    AutoModelEngine engine(storage, config);
    if (!apiKey.empty()) {
        engine.initialize(apiKey);
    }

    return engine.buildFromText(text);
}

std::vector<Triple> naturalLanguageQuery(StoragePtr storage, const String& query, const String& apiKey) {
    AutoModelConfig config;
    config.llmApiKey = apiKey;

    AutoModelEngine engine(storage, config);
    if (!apiKey.empty()) {
        engine.initialize(apiKey);
    }

    // Use the engine to interpret the natural language query
    String answer = engine.answerQuestion(query);

    // Extract triples from the answer context
    auto allTriples = storage->getAllTriples();

    // Simple keyword matching to find relevant triples
    std::vector<Triple> results;
    for (const auto& t : allTriples) {
        if (query.find(t.subject) != String::npos ||
            query.find(t.predicate) != String::npos ||
            query.find(t.object) != String::npos) {
            results.push_back(t);
        }
    }

    return results;
}

// ============================================================================
// Helper methods
// ============================================================================

size_t AutoModelEngine::tripleHash(const Triple& t) {
    std::hash<String> hasher;
    size_t h = hasher(t.subject);
    h ^= hasher(t.predicate) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= hasher(t.object) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

int AutoModelEngine::levenshteinDistance(const String& s1, const String& s2) {
    int m = static_cast<int>(s1.size());
    int n = static_cast<int>(s2.size());
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1));

    for (int i = 0; i <= m; ++i) dp[i][0] = i;
    for (int j = 0; j <= n; ++j) dp[0][j] = j;

    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            dp[i][j] = std::min({dp[i-1][j] + 1, dp[i][j-1] + 1, dp[i-1][j-1] + cost});
        }
    }
    return dp[m][n];
}

float AutoModelEngine::jaccardCoefficient(const String& entity1, const String& entity2) const {
    auto* ts = storage_->getTripleStore();
    if (!ts) return 0.0f;

    auto triples1 = ts->findBySubject(entity1);
    auto triples2 = ts->findBySubject(entity2);

    std::unordered_set<String> props1, props2;
    for (const auto& t : triples1) props1.insert(t.predicate);
    for (const auto& t : triples2) props2.insert(t.predicate);

    if (props1.empty() && props2.empty()) return 0.0f;

    int intersection = 0;
    for (const auto& p : props1) {
        if (props2.count(p)) intersection++;
    }
    int unionSize = static_cast<int>(props1.size() + props2.size()) - intersection;
    return unionSize > 0 ? static_cast<float>(intersection) / unionSize : 0.0f;
}

} // namespace ontology
