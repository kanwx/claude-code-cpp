#pragma once

#include "../Core.hpp"
#include "Forward.hpp"
#include <optional>
#include <vector>

namespace ontology {

// ============================================================================
// 向量数据库接口
// ============================================================================

class VectorDatabase {
public:
    virtual ~VectorDatabase() = default;

    // 连接
    virtual bool connect() = 0;
    virtual void disconnect() = 0;

    // 集合操作
    virtual bool createCollection(const String& name, int dimension, const String& metric = "cosine") = 0;
    virtual bool dropCollection(const String& name) = 0;
    virtual bool hasCollection(const String& name) = 0;

    // 向量操作
    virtual bool insert(const String& collection, const String& id, const std::vector<float>& vector, const Json& metadata = {}) = 0;
    virtual bool update(const String& collection, const String& id, const std::vector<float>& vector, const Json& metadata = {}) = 0;
    virtual bool remove(const String& collection, const String& id) = 0;

    // 搜索
    struct SearchResult {
        String id;
        float score;
        Json metadata;
        std::vector<float> vector;
    };

    virtual std::vector<SearchResult> search(
        const String& collection,
        const std::vector<float>& query,
        int topK = 10,
        const Json& filter = {}
    ) = 0;

    // 批量操作
    virtual bool batchInsert(const String& collection, const std::vector<std::pair<String, std::vector<float>>>& vectors) = 0;

    // 状态
    virtual bool isConnected() const = 0;
};

} // namespace ontology
