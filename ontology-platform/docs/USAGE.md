# Ontology Platform 使用说明

## 编译

```bash
cd ontology-platform
mkdir -p build && cd build
cmake .. -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

运行测试：

```bash
ctest --output-on-failure
```

---

## 1. ALC 描述逻辑推理器

`DlReasoner` 实现了 ALC 描述逻辑的 Tableaux 算法，支持 TBox 推理。

### 1.1 基本用法

```cpp
#include <ontology/DlReasoner.hpp>
#include <ontology/ClassExpression.hpp>

using namespace ontology;

DlReasoner reasoner;

// 添加 TBox 公理
auto animal = ClassExpression::atomic("Animal");
auto mammal = ClassExpression::atomic("Mammal");
auto dog    = ClassExpression::atomic("Dog");
auto cat    = ClassExpression::atomic("Cat");

reasoner.addSubClassOf(mammal, animal);   // Mammal ⊑ Animal
reasoner.addSubClassOf(dog, mammal);       // Dog ⊑ Mammal
reasoner.addDisjointClasses(dog, cat);     // Dog ⊓ Cat ⊑ ⊥

// 添加 ABox 断言
reasoner.addConceptAssertion("rex", dog);
reasoner.addRoleAssertion("rex", "livesIn", "home");

// 推理服务
bool ok = reasoner.isSatisfiable(dog);             // true
bool sub = reasoner.isSubsumedBy(dog, animal);      // true (Dog ⊑ Animal)
bool eq  = reasoner.isEquivalent(dog, dog);          // true
bool consistent = reasoner.isConsistent();           // true
```

### 1.2 从 TripleStore 加载 TBox

```cpp
TripleStore store;
store.add({"Dog", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Mammal"});
store.add({"Mammal", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
store.add({"Dog", "http://www.w3.org/2002/07/owl#disjointWith", "Cat"});

DlReasoner reasoner;
reasoner.loadFromTripleStore(&store);
```

### 1.3 分类与实例化

```cpp
// 分类：计算完整的类层次
auto hierarchy = reasoner.classify();
// hierarchy["Dog"] = {"Mammal", "Animal"}
// hierarchy["Mammal"] = {"Animal"}

// 实例化：找到个体最具体的类型
auto types = reasoner.realize("rex");
// types = {"Dog"}
```

### 1.4 复杂类表达式

```cpp
using CE = ClassExpression;

auto herbivore = CE::atomic("Herbivore");
auto carnivore = CE::atomic("Carnivore");
auto omnivore = CE::intersection({herbivore, carnivore});  // Herbivore ⊓ Carnivore

reasoner.addSubClassOf(omnivore, animal);
bool sat = reasoner.isSatisfiable(omnivore);

auto somePlantEater = CE::someValues("eats", plant);  // ∃eats.Plant
auto allPlantEater  = CE::allValues("eats", plant);   // ∀eats.Plant
```

### 1.5 与 HybridReasoner 集成

```cpp
#include <ontology/Inference.hpp>

auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
HybridReasoner reasoner(storage);

auto dl = std::make_shared<DlReasoner>();
reasoner.setDlReasoner(dl.get());

// detectContradictions() 会自动使用 DlReasoner 检查 TBox 一致性
auto contradictions = reasoner.detectContradictions();
```

---

## 2. 时态推理 (Allen 区间代数)

### 2.1 Allen 关系判断

```cpp
#include <ontology/Temporal.hpp>

using namespace ontology;

TemporalInterval a{"2024-01-01T00:00:00Z", "2024-01-15T00:00:00Z"};
TemporalInterval b{"2024-01-10T00:00:00Z", "2024-01-20T00:00:00Z"};

auto rel = a.relationTo(b);  // AllenRelation::Overlaps

// 13 种关系: Before, After, Meets, MetBy, Overlaps, OverlappedBy,
//            During, Contains, Starts, StartedBy, Finishes, FinishedBy, Equals
```

### 2.2 Allen 组合与逆

```cpp
// 逆关系
auto inv = allenInverse(AllenRelation::Before);  // After
auto inv2 = allenInverse(AllenRelation::During); // Contains

// 组合: R1(A,B) ∘ R2(B,C) → R3(A,C)
auto possible = allenCompose(AllenRelation::Before, AllenRelation::Before);
// 结果: {Before} — Before ∘ Before = Before

auto complex = allenCompose(AllenRelation::During, AllenRelation::Contains);
// 结果: 所有13种关系都有可能
```

### 2.3 路径一致性检查

```cpp
std::vector<TemporalInterval> intervals = {
    {"2024-01-01T00:00:00Z", "2024-01-10T00:00:00Z"},  // 0
    {"2024-01-20T00:00:00Z", "2024-01-30T00:00:00Z"},  // 1
    {"2024-02-01T00:00:00Z", "2024-02-10T00:00:00Z"}   // 2
};

std::unordered_map<std::pair<int,int>, std::set<AllenRelation>, PairHash> relations;

// 自动从区间计算关系，检查全局一致性
bool consistent = isPathConsistent(intervals, relations);  // true

// 添加矛盾约束
relations[{0, 2}] = {AllenRelation::Overlaps};  // 强制 0 与 2 重叠
bool stillOk = isPathConsistent(intervals, relations);  // false — 与 Before∘Before 矛盾
```

### 2.4 时态 SWRL 内置函数

```cpp
#include <ontology/Swrl.hpp>

using namespace ontology;

// 时间点比较
bool before = SwrlBuiltIns::temporalBefore("2024-01-10T00:00:00Z", "2024-02-01T00:00:00Z");
// true

// 区间关系
bool during = SwrlBuiltIns::temporalDuring(
    "2024-01-05T00:00:00Z", "2024-01-10T00:00:00Z",  // 内区间
    "2024-01-01T00:00:00Z", "2024-01-20T00:00:00Z"); // 外区间
// true

bool contains = SwrlBuiltIns::temporalContains(
    "2024-01-01T00:00:00Z", "2024-01-20T00:00:00Z",  // 大区间
    "2024-01-05T00:00:00Z", "2024-01-10T00:00:00Z"); // 小区间
// true

bool overlaps = SwrlBuiltIns::temporalOverlaps(
    "2024-01-01T00:00:00Z", "2024-01-15T00:00:00Z",
    "2024-01-10T00:00:00Z", "2024-01-20T00:00:00Z");
// true
```

### 2.5 双时态三元组

```cpp
Triple t;
t.subject = "alice";
t.predicate = "employedAt";
t.object = "CompanyA";
t.validFrom = "2020-01-01T00:00:00Z";  // 有效时间起点
t.validTo   = "2023-12-31T00:00:00Z";  // 有效时间终点
t.recordedAt = "2024-06-01T00:00:00Z"; // 事务时间：何时录入系统

// 查询某时间点是否有效
bool valid2022 = t.isValidAt("2022-06-15T00:00:00Z");  // true
bool valid2024 = t.isValidAt("2024-06-15T00:00:00Z");  // false
```

---

## 3. 分布式存储

`DistributedStorage` 提供与 `HybridStorage` 兼容的接口，支持哈希分片、主从复制和仲裁写入。

### 3.1 基本配置与使用

```cpp
#include <ontology/Distributed.hpp>

using namespace ontology;

ClusterConfig config;
config.numShards = 4;            // 4 个分片
config.replicationFactor = 3;    // 每分片 1 主 + 2 从
config.writeQuorum = 2;          // 写入需 2 个副本确认
config.readFromFollowers = true; // 允许从副本读取

DistributedStorage storage(config);

// 添加三元组（自动路由到对应分片）
storage.addTriple({"alice", "knows", "bob"});
storage.addTriple({"carol", "worksAt", "CompanyA"});

// 查询
auto results = storage.findBySubject("alice");  // 路由到单个分片
auto allPred = storage.findByPredicate("knows"); // 扇出到所有分片

// 统计
size_t count = storage.tripleCount();
```

### 3.2 集群健康监控

```cpp
auto health = storage.getClusterHealth();
// health.totalShards = 4
// health.healthyShards = 4
// health.totalReplicas = 12
// health.allShardsAvailable = true

// 单个分片信息
auto shard0 = storage.getShardInfo(0);
// shard0.leaderName = "shard0_replica0"
// shard0.replicaNames = {"shard0_replica0", "shard0_replica1", "shard0_replica2"}
// shard0.tripleCount = ...
```

### 3.3 副本故障与恢复

```cpp
// 模拟副本故障
storage.failReplica(0, "shard0_replica1");

// 仍可写入（剩余 2 个副本 >= quorum 2）
storage.addTriple({"dave", "knows", "eve"});

// 仍可读取
auto results = storage.findBySubject("dave");

// 恢复副本（自动从 Leader 同步数据）
storage.recoverReplica(0, "shard0_replica1");
```

### 3.4 向量时钟

```cpp
VectorClock vc1, vc2;
vc1.increment(0);  // 分片0 的计数器 +1 → {0:1}
vc2.increment(1);  // 分片1 的计数器 +1 → {1:1}

// 因果关系判断
vc1.happensBefore(vc2);   // false — 并发
vc1.isConcurrent(vc2);    // true — 互不依赖

// 合并：取各分片计数器的最大值
auto merged = vc1.merge(vc2);  // {0:1, 1:2}
```

### 3.5 分片再平衡

```cpp
// 增加分片数后重新分布数据
storage.rebalance();
```

---

## 4. AutoModel 增强

### 4.1 TBox 感知的冲突检测

```cpp
#include <ontology/AutoModel.hpp>

auto storage = std::make_shared<HybridStorage>(nullptr, nullptr);
AutoModelEngine engine(storage);

// 添加不相交类
auto* ts = storage->getTripleStore();
ts->add({"Cat", "http://www.w3.org/2002/07/owl#disjointWith", "Dog"});
ts->add({"fluffy", "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", "Cat"});
ts->add({"fluffy", "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", "Dog"});

// 检测冲突
auto conflicts = engine.detectConflicts();
// 找到 DisjointClassAssertion 冲突：fluffy 不能同时是 Cat 和 Dog

// 也检测函数属性违反
// 例如：hasMother 是函数属性，alice 不能有两个母亲
```

### 4.2 多信号实体对齐

```cpp
auto alignments = engine.alignEntities({"Dog"}, {"Canine"});

for (const auto& a : alignments) {
    // a.embeddingScore   — 向量嵌入余弦相似度
    // a.structuralScore  — Jaccard 共享属性系数
    // a.labelScore       — 标签 Levenshtein 归一化相似度
    // a.combinedScore    — 加权综合分 (0.5*emb + 0.3*struct + 0.2*label)
}
```

### 4.3 来源感知的本体融合

```cpp
std::vector<Triple> externalTriples = {
    {"Cat", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"},
    {"Bird", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"}
};

// 融合外部本体，自动去重并记录来源
engine.mergeOntologies(externalTriples, "ext_ontology_1", "External Zoo Ontology");
```

---

## 5. IO 序列化

### 5.1 JSON-LD 输出

```cpp
#include <ontology/OntologyIO.hpp>

JsonLdWriter writer;
RdfGraph graph;
graph.addPrefix("ex", "http://example.org/");
graph.addPrefix("rdf", "http://www.w3.org/1999/02/22-rdf-syntax-ns#");

RdfTriple t;
t.subject = "ex:Dog";
t.predicate = "rdf:type";
t.object = "ex:Animal";
graph.addTriple(t);

String jsonld = writer.writeRdf(graph);
// 输出标准 JSON-LD 格式：@context, @graph, rdf:type → @type
```

### 5.2 OWL/XML 输出

```cpp
OwlXmlWriter writer;
String owlxml = writer.write(ontology);
// 输出 OWL 2 XML 格式：
// <Declaration><Class IRI="..."/></Declaration>
// <SubClassOf>...</SubClassOf>
// <DisjointClasses>...</DisjointClasses>
// <ClassAssertion>...</ClassAssertion>
```

### 5.3 其他格式

```cpp
TurtleWriter turtleWriter;
String turtle = turtleWriter.write(ontology);

NTriplesWriter ntriplesWriter;
String ntriples = ntriplesWriter.write(ontology);

RdfXmlWriter rdfxmlWriter;
String rdfxml = rdfxmlWriter.write(ontology);
```

---

## 6. 完整示例：构建本体 + 推理 + 导出

```cpp
#include <ontology/Core.hpp>
#include <ontology/Storage.hpp>
#include <ontology/Inference.hpp>
#include <ontology/DlReasoner.hpp>
#include <ontology/Swrl.hpp>
#include <ontology/Temporal.hpp>
#include <ontology/Distributed.hpp>
#include <ontology/OntologyIO.hpp>

using namespace ontology;

int main() {
    // 1. 创建分布式存储
    ClusterConfig config;
    config.numShards = 2;
    config.replicationFactor = 3;
    DistributedStorage storage(config);

    // 2. 添加三元组
    storage.addTriple({"Dog", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    storage.addTriple({"Cat", "http://www.w3.org/2000/01/rdf-schema#subClassOf", "Animal"});
    storage.addTriple({"Dog", "http://www.w3.org/2002/07/owl#disjointWith", "Cat"});
    storage.addTriple({"rex", "http://www.w3.org/1999/02/22-rdf-syntax-ns#type", "Dog"});

    // 3. DL 推理
    DlReasoner dl;
    // (通过 loadFromTripleStore 或手动添加 TBox 公理)
    auto dog = ClassExpression::atomic("Dog");
    auto animal = ClassExpression::atomic("Animal");
    dl.addSubClassOf(dog, animal);
    bool subsumed = dl.isSubsumedBy(dog, animal);  // true

    // 4. 时态查询
    TemporalInterval employment{"2020-01-01T00:00:00Z", "2023-12-31T00:00:00Z"};
    TemporalInterval project{"2022-06-01T00:00:00Z", "2023-06-30T00:00:00Z"};
    auto rel = employment.relationTo(project);  // AllenRelation::Contains

    // 5. 冲突检测
    auto hybridStorage = std::make_shared<HybridStorage>(nullptr, nullptr);
    AutoModelEngine automodel(hybridStorage);
    auto conflicts = automodel.detectConflicts();

    // 6. 导出
    Ontology onto;
    onto.classes["Dog"] = Class{"Dog", "Dog", "A domestic dog"};
    onto.classes["Cat"] = Class{"Cat", "Cat", "A domestic cat"};
    JsonLdWriter jsonldWriter;
    String output = jsonldWriter.write(onto);

    return 0;
}
```
