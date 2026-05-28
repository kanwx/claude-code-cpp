#pragma once

#include <memory>
#include "../Core.hpp"

namespace ontology {

class HybridStorage;
class GraphDatabase;
class VectorDatabase;
class TripleStore;

using StoragePtr = std::shared_ptr<HybridStorage>;
using GraphDatabasePtr = std::shared_ptr<GraphDatabase>;
using VectorDatabasePtr = std::shared_ptr<VectorDatabase>;

} // namespace ontology
