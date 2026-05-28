#pragma once

#include "../Core.hpp"
#include "Forward.hpp"
#include <optional>
#include <vector>

namespace ontology {

// ============================================================================
// 图数据库接口
// ============================================================================

class GraphDatabase {
public:
    virtual ~GraphDatabase() = default;

    // 连接
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // 节点操作
    virtual bool createNode(const String& id, const String& label, const Json& properties) = 0;
    virtual bool updateNode(const String& id, const Json& properties) = 0;
    virtual bool deleteNode(const String& id) = 0;
    virtual std::optional<Json> getNode(const String& id) = 0;

    // 关系操作
    virtual bool createRelation(const String& from, const String& type, const String& to, const Json& properties) = 0;
    virtual bool deleteRelation(const String& from, const String& type, const String& to) = 0;
    virtual std::vector<Json> getRelations(const String& from, const String& type = "") = 0;

    // 路径查询
    virtual std::vector<std::vector<Json>> findPath(const String& from, const String& to, int maxDepth = 4) = 0;

    // 图查询
    virtual std::vector<Json> query(const String& cypher) = 0;
    virtual std::vector<Json> query(const Json& querySpec) = 0;

    // 批量操作
    virtual bool batchCreate(const std::vector<Triple>& triples) = 0;

    // 状态
    virtual String getStatus() const = 0;
};

} // namespace ontology
