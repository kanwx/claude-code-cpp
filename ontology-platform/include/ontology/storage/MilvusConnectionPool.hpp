#pragma once

#include "MilvusClient.hpp"
#include <mutex>
#include <condition_variable>
#include <stack>

namespace ontology {

// Milvus 连接池
class MilvusConnectionPool {
public:
    MilvusConnectionPool(const MilvusClient::Config& config, int poolSize = 10);
    std::shared_ptr<MilvusClient> acquire();
    void release(std::shared_ptr<MilvusClient> client);
    size_t availableConnections() const;

private:
    MilvusClient::Config config_;
    int maxPoolSize_;
    std::stack<std::shared_ptr<MilvusClient>> pool_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace ontology
