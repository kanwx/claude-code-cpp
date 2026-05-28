#include <ontology/RagPipeline.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>

namespace ontology {

// ============================================================================
// TextSplitter
// ============================================================================

std::vector<String> TextSplitter::splitBySeparator(const String& text) const {
    std::vector<String> segments;
    size_t start = 0;

    while (start < text.size()) {
        size_t pos = text.find(config_.separator, start);
        if (pos == String::npos) {
            segments.push_back(text.substr(start));
            break;
        }
        segments.push_back(text.substr(start, pos - start));
        start = pos + config_.separator.size();
    }

    return segments;
}

std::vector<RagChunk> TextSplitter::split(
    const String& text,
    const String& documentId,
    const String& knowledgeBaseId
) const {
    std::vector<RagChunk> chunks;

    if (text.empty()) return chunks;

    auto segments = splitBySeparator(text);

    // Merge short segments
    std::vector<String> merged;
    String current;
    for (const auto& seg : segments) {
        if (current.empty()) {
            current = seg;
        } else if (static_cast<int>(current.size()) + static_cast<int>(seg.size()) < config_.chunkSize) {
            current += config_.separator + seg;
        } else {
            merged.push_back(current);
            current = seg;
        }
    }
    if (!current.empty()) {
        merged.push_back(current);
    }

    // Create chunks with overlap
    int chunkIndex = 0;
    int charPos = 0;

    for (size_t i = 0; i < merged.size(); i++) {
        String chunkText = merged[i];

        // If single segment exceeds chunk size, hard-split it
        if (static_cast<int>(chunkText.size()) > config_.chunkSize * 2) {
            int offset = 0;
            while (offset < static_cast<int>(chunkText.size())) {
                int len = std::min(config_.chunkSize, static_cast<int>(chunkText.size()) - offset);
                RagChunk chunk;
                chunk.documentId = documentId;
                chunk.knowledgeBaseId = knowledgeBaseId;
                chunk.text = chunkText.substr(offset, len);
                chunk.chunkIndex = chunkIndex++;
                chunk.startPos = charPos + offset;
                chunk.endPos = chunk.startPos + len;
                chunks.push_back(chunk);
                offset += len - config_.chunkOverlap;
                if (offset < 0) offset = 0;
            }
            charPos += chunkText.size();
            continue;
        }

        RagChunk chunk;
        chunk.documentId = documentId;
        chunk.knowledgeBaseId = knowledgeBaseId;
        chunk.text = chunkText;
        chunk.chunkIndex = chunkIndex++;
        chunk.startPos = charPos;
        chunk.endPos = charPos + static_cast<int>(chunkText.size());
        chunks.push_back(chunk);

        charPos += static_cast<int>(chunkText.size()) + static_cast<int>(config_.separator.size());
    }

    return chunks;
}

// ============================================================================
// RagPipeline
// ============================================================================

RagPipeline::RagPipeline(
    std::shared_ptr<TextEmbedder> embedder,
    std::shared_ptr<RagStorage> ragStorage,
    StoragePtr hybridStorage,
    LlmCollaboration* llm
)
    : embedder_(embedder)
    , ragStorage_(ragStorage)
    , hybridStorage_(hybridStorage)
    , llm_(llm)
    , splitter_(config_.splitter)
{
}

RagPipeline::IngestionResult RagPipeline::ingest(
    const String& knowledgeBaseId,
    const String& title,
    const String& content,
    const String& source,
    const std::vector<String>& tags,
    const Json& metadata
) {
    IngestionResult result;

    // 1. Add document metadata
    RagDocument doc;
    doc.title = title;
    doc.content = content;
    doc.knowledgeBaseId = knowledgeBaseId;
    doc.source = source;
    doc.tags = tags;
    doc.metadata = metadata;

    String docId = ragStorage_->addDocument(doc);
    result.documentId = docId;

    // 2. Split into chunks
    auto chunks = splitter_.split(content, docId, knowledgeBaseId);
    result.chunkCount = static_cast<int>(chunks.size());

    if (chunks.empty()) {
        result.warnings.push_back("No chunks produced from content");
        return result;
    }

    // 3. Compute embeddings in batch
    std::vector<String> texts;
    texts.reserve(chunks.size());
    for (const auto& chunk : chunks) {
        texts.push_back(chunk.text);
    }

    auto embeddings = embedder_->embedBatch(texts);

    // 4. Store chunks with embeddings
    for (size_t i = 0; i < chunks.size(); i++) {
        if (i < embeddings.size()) {
            chunks[i].embedding = embeddings[i];
        }
        ragStorage_->addChunk(chunks[i]);
    }

    // 5. Extract entities and relations
    if (config_.extractEntities) {
        if (llm_) {
            // LLM-based extraction (preferred)
            try {
                auto extraction = llm_->extractFromText(content, "");

                for (const auto& [entityName, entityType] : extraction.entities) {
                    Individual ind;
                    ind.id = entityName;
                    ind.name = entityName;
                    ind.classId = entityType;

                    if (hybridStorage_) {
                        hybridStorage_->addIndividual(ind);
                        if (!chunks.empty() && embedder_) {
                            auto entityEmb = embedder_->embed(entityName);
                            hybridStorage_->storeIndividual(ind, entityEmb);
                        }
                    }
                    result.entityCount++;
                }

                if (config_.storeExtractedTriples && hybridStorage_) {
                    for (const auto& t : extraction.relations) {
                        if (t.confidence >= config_.extractionConfidenceThreshold) {
                            hybridStorage_->addTriple(t);
                            result.tripleCount++;
                        }
                    }
                }
            } catch (const nlohmann::json::exception& e) {
                spdlog::error("RAG JSON error: {}", e.what());
                result.warnings.push_back("LLM entity extraction failed, using rule-based fallback");
                auto rbExtraction = extractEntitiesRuleBased(content);
                for (const auto& [entityName, entityType] : rbExtraction.entities) {
                    Individual ind;
                    ind.id = entityName;
                    ind.name = entityName;
                    ind.classId = entityType;
                    if (hybridStorage_) {
                        hybridStorage_->addIndividual(ind);
                        if (embedder_) {
                            auto entityEmb = embedder_->embed(entityName);
                            hybridStorage_->storeIndividual(ind, entityEmb);
                        }
                    }
                    result.entityCount++;
                }
                if (config_.storeExtractedTriples && hybridStorage_) {
                    for (const auto& t : rbExtraction.relations) {
                        hybridStorage_->addTriple(t);
                        result.tripleCount++;
                    }
                }
            }
        } else {
            // Rule-based extraction (no LLM available)
            auto rbExtraction = extractEntitiesRuleBased(content);
            for (const auto& [entityName, entityType] : rbExtraction.entities) {
                Individual ind;
                ind.id = entityName;
                ind.name = entityName;
                ind.classId = entityType;
                if (hybridStorage_) {
                    hybridStorage_->addIndividual(ind);
                    if (embedder_) {
                        auto entityEmb = embedder_->embed(entityName);
                        hybridStorage_->storeIndividual(ind, entityEmb);
                    }
                }
                result.entityCount++;
            }
            if (config_.storeExtractedTriples && hybridStorage_) {
                for (const auto& t : rbExtraction.relations) {
                    hybridStorage_->addTriple(t);
                    result.tripleCount++;
                }
            }
        }
    }

    return result;
}

RagPipeline::IngestionResult RagPipeline::ingestFile(
    const String& filePath,
    const String& knowledgeBaseId,
    const String& title,
    const String& source,
    const std::vector<String>& tags,
    const Json& metadata
) {
    if (!preprocessor_) {
        IngestionResult result;
        result.warnings.push_back("No DocumentPreprocessor configured for file ingestion");
        return result;
    }

    auto extractResult = preprocessor_->extract(filePath);
    if (extractResult.text.empty() && extractResult.imageBase64.empty()) {
        IngestionResult result;
        result.warnings.push_back("No text or images extracted from file: " + filePath);
        return result;
    }

    // Process images via image2text
    String fullText = extractResult.text;
    for (size_t i = 0; i < extractResult.imageBase64.size(); i++) {
        String mediaType = i < extractResult.imageMediaTypes.size()
            ? extractResult.imageMediaTypes[i] : "image/png";
        String imgDesc = preprocessor_->imageToText(extractResult.imageBase64[i], mediaType);
        if (!imgDesc.empty()) {
            fullText += "\n\n[图片描述] " + imgDesc;
        }
    }

    String docTitle = title.empty() ? filePath : title;
    String docSource = source.empty() ? filePath : source;

    // Merge extraction metadata
    Json mergedMeta = metadata;
    mergedMeta["detectedType"] = extractResult.detectedType;
    mergedMeta["detectedLanguage"] = extractResult.detectedLanguage;
    mergedMeta["pageCount"] = extractResult.pageCount;
    mergedMeta["imageCount"] = extractResult.imageCount;

    return ingest(knowledgeBaseId, docTitle, fullText, docSource, tags, mergedMeta);
}

RagPipeline::IngestionResult RagPipeline::ingestBuffer(
    const String& data,
    const String& mimeType,
    const String& fileName,
    const String& knowledgeBaseId,
    const String& title,
    const String& source,
    const std::vector<String>& tags,
    const Json& metadata
) {
    if (!preprocessor_) {
        IngestionResult result;
        result.warnings.push_back("No DocumentPreprocessor configured for buffer ingestion");
        return result;
    }

    auto extractResult = preprocessor_->extractFromBuffer(data, mimeType, fileName);
    if (extractResult.text.empty() && extractResult.imageBase64.empty()) {
        IngestionResult result;
        result.warnings.push_back("No text or images extracted from buffer");
        return result;
    }

    // Process images via image2text
    String fullText = extractResult.text;
    for (size_t i = 0; i < extractResult.imageBase64.size(); i++) {
        String mediaType = i < extractResult.imageMediaTypes.size()
            ? extractResult.imageMediaTypes[i] : "image/png";
        String imgDesc = preprocessor_->imageToText(extractResult.imageBase64[i], mediaType);
        if (!imgDesc.empty()) {
            fullText += "\n\n[图片描述] " + imgDesc;
        }
    }

    String docTitle = title.empty() ? fileName : title;
    String docSource = source.empty() ? fileName : source;

    Json mergedMeta = metadata;
    mergedMeta["detectedType"] = extractResult.detectedType;
    mergedMeta["detectedLanguage"] = extractResult.detectedLanguage;
    mergedMeta["pageCount"] = extractResult.pageCount;
    mergedMeta["imageCount"] = extractResult.imageCount;

    return ingest(knowledgeBaseId, docTitle, fullText, docSource, tags, mergedMeta);
}

bool RagPipeline::deleteDocument(const String& documentId) {
    return ragStorage_->deleteDocument(documentId);
}

RagPipeline::RuleBasedExtraction RagPipeline::extractEntitiesRuleBased(const String& text) const {
    RuleBasedExtraction result;
    if (text.empty()) return result;

    // Known entity patterns
    struct Pattern {
        String regex;
        String type;
    };

    // Chinese named entity patterns
    static const std::vector<Pattern> patterns = {
        // Person names: 2-4 character Chinese names with common surnames
        {"(?:张|王|李|赵|刘|陈|杨|黄|周|吴|徐|孙|马|朱|胡|郭|林|何|高|罗|郑|梁|谢|宋|唐|韩|邓|冯|曹|彭|曾|肖|田|董|袁|潘|于|蒋|蔡|余|杜|叶|程|魏|苏|吕|丁|任|沈|姚|卢|姜|崔|钟|谭|陆|汪|范|廖|石|金|贾|夏|丁|魏|薛|阎|段|雷|侯|龙|邵|万|钱|严|覃|武|戴|莫|孔|向|汤)[\\u4e00-\\u9fff]{1,3}", "Person"},
        // Organizations: ends with org keywords
        {"[\\u4e00-\\u9fff]{2,10}(?:公司|集团|研究院|研究所|大学|学院|医院|银行|部门|委员会|局|厅|处|院|所|中心)", "Organization"},
        // Locations: ends with location keywords
        {"[\\u4e00-\\u9fff]{1,8}(?:省|市|区|县|镇|村|路|街|号|楼|室|国|洲|岛|山|河|湖|海|江)", "Location"},
        // Date/time
        {"\\d{4}年\\d{1,2}月\\d{1,2}日", "Date"},
        {"\\d{4}-\\d{2}-\\d{2}", "Date"},
        // Numbers with units
        {"\\d+(?:\\.\\d+)?(?:万元|亿元|元|美元|欧元|公斤|吨|米|千米|平方公里|人|个|次|条|项|件|台|套)", "Quantity"},
        // English proper nouns (capitalized words)
        {"[A-Z][a-z]+(?:\\s+[A-Z][a-z]+)*", "NamedEntity"},
    };

    // Apply regex patterns (simplified: use substring matching for Chinese)
    std::unordered_set<String> seen;

    // Person name detection using common surname prefix
    static const String surnames = "张王李赵刘陈杨黄周吴徐孙马朱胡郭林何高罗郑梁谢宋唐韩邓冯曹彭曾肖田董袁潘于蒋蔡余杜叶程魏苏吕丁任沈姚卢姜崔钟谭陆汪范廖石金贾夏魏薛阎段雷侯龙邵万钱严覃武戴莫孔向汤";
    for (size_t i = 0; i < text.size(); i++) {
        // Check if current character is a surname
        String ch = text.substr(i, 3);  // UTF-8 char
        bool isSurname = false;
        for (size_t s = 0; s < surnames.size(); s += 3) {
            if (s + 2 < surnames.size() && text.compare(i, 3, surnames, s, 3) == 0) {
                isSurname = true;
                break;
            }
        }
        if (isSurname && i + 6 <= text.size()) {
            // Try 2-char, 3-char, 4-char names
            for (int len : {6, 9, 12}) {  // 2, 3, 4 Chinese chars (3 bytes each)
                if (i + len <= text.size()) {
                    String name = text.substr(i, len);
                    // Verify all characters are CJK
                    bool allCJK = true;
                    for (size_t j = 3; j < name.size(); j += 3) {
                        unsigned char c = name[j];
                        if (c < 0xE4 || c > 0xE9) { allCJK = false; break; }
                    }
                    if (allCJK && seen.find(name) == seen.end()) {
                        result.entities.push_back({name, "Person"});
                        seen.insert(name);
                    }
                }
            }
        }
    }

    // Organization/location detection by suffix keywords
    static const std::vector<std::pair<String, String>> suffixTypes = {
        {"公司", "Organization"}, {"集团", "Organization"}, {"研究院", "Organization"},
        {"研究所", "Organization"}, {"大学", "Organization"}, {"学院", "Organization"},
        {"医院", "Organization"}, {"银行", "Organization"}, {"部门", "Organization"},
        {"委员会", "Organization"}, {"局", "Organization"}, {"厅", "Organization"},
        {"省", "Location"}, {"市", "Location"}, {"区", "Location"},
        {"县", "Location"}, {"镇", "Location"}, {"国", "Location"},
    };

    for (const auto& [suffix, type] : suffixTypes) {
        size_t pos = 0;
        while ((pos = text.find(suffix, pos)) != String::npos) {
            // Find the start of this entity (up to 10 chars before suffix)
            size_t start = pos > 30 ? pos - 30 : 0;
            // Skip whitespace/newlines
            while (start < pos && (text[start] == ' ' || text[start] == '\n' || text[start] == '\r')) {
                start++;
            }
            String entity = text.substr(start, pos - start + suffix.size());
            // Trim leading whitespace
            size_t firstNonSpace = entity.find_first_not_of(" \t\n\r");
            if (firstNonSpace != String::npos && firstNonSpace > 0) {
                entity = entity.substr(firstNonSpace);
            }
            if (entity.size() >= 4 && entity.size() <= 40 && seen.find(entity) == seen.end()) {
                result.entities.push_back({entity, type});
                seen.insert(entity);
            }
            pos += suffix.size();
        }
    }

    // Relation extraction using pattern matching
    // Patterns: "A是B的C", "A属于B", "A包含B", "A位于B", "A管理B"
    static const std::vector<std::pair<String, String>> relationPatterns = {
        {"是", "is"}, {"属于", "belongsTo"}, {"包含", "contains"},
        {"位于", "locatedIn"}, {"管理", "manages"}, {"审批", "canApprove"},
        {"下属", "hasSubordinate"}, {"上级", "hasSupervisor"},
        {"生产", "produces"}, {"依赖", "dependsOn"},
    };

    for (const auto& [keyword, predicate] : relationPatterns) {
        size_t pos = 0;
        while ((pos = text.find(keyword, pos)) != String::npos) {
            // Extract subject (text before keyword)
            size_t subjStart = pos > 20 ? pos - 20 : 0;
            while (subjStart < pos && (text[subjStart] == ' ' || text[subjStart] == '\n')) subjStart++;
            size_t subjEnd = pos;
            // Remove trailing particles
            while (subjEnd > subjStart + 3) {
                String last2 = text.substr(subjEnd - 3, 3);
                if (last2 == "的" || last2 == "了" || last2 == "在") subjEnd -= 3;
                else break;
            }
            String subject = text.substr(subjStart, subjEnd - subjStart);

            // Extract object (text after keyword)
            size_t objStart = pos + keyword.size();
            size_t objEnd = objStart + 20;
            if (objEnd > text.size()) objEnd = text.size();
            // Cut at next punctuation or whitespace
            for (size_t i = objStart; i < objEnd; i++) {
                if (text[i] == '\n' ||
                    text[i] == ',' || text[i] == '.' || text[i] == ';' ||
                    (i + 2 < text.size() && (
                        text.substr(i, 3) == "\xef\xbc\x8c" ||  // ，
                        text.substr(i, 3) == "\xe3\x80\x82" ||  // 。
                        text.substr(i, 3) == "\xe3\x80\x81" ||  // 、
                        text.substr(i, 3) == "\xef\xbc\x9b" ||  // ；
                        text.substr(i, 3) == "\xef\xbc\x9a"     // ：
                    ))) {
                    objEnd = i;
                    break;
                }
            }
            String object = text.substr(objStart, objEnd - objStart);

            // Trim and validate
            auto trim = [](String s) -> String {
                size_t a = s.find_first_not_of(" \t\n\r");
                size_t b = s.find_last_not_of(" \t\n\r");
                return (a == String::npos) ? "" : s.substr(a, b - a + 1);
            };
            subject = trim(subject);
            object = trim(object);

            if (!subject.empty() && !object.empty() &&
                subject.size() >= 2 && object.size() >= 2 &&
                subject.size() <= 30 && object.size() <= 30) {
                Triple t;
                t.subject = subject;
                t.predicate = predicate;
                t.object = object;
                t.confidence = 0.6f;
                t.source = "rule_based_extraction";
                result.relations.push_back(t);
            }
            pos = objEnd;
        }
    }

    // Limit results
    if (result.entities.size() > 50) result.entities.resize(50);
    if (result.relations.size() > 30) result.relations.resize(30);

    return result;
}

} // namespace ontology
