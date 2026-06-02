# Graph Database Authority Source Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Transform the graph database from a write-only mirror into the authoritative data source, with in-memory data as a high-performance cache layer, enabling persistent recovery, graph query delegation, and graceful read-only degradation.

**Architecture:** Cache-first pattern — writes go to graphDB first (confirmed before updating memory), reads hit memory cache first (fallback to graphDB for complex queries), startup loads from graphDB (fallback to snapshot+WAL), degradation enters read-only mode when graphDB is unavailable.

**Tech Stack:** C++23, StellarDB (primary graphDB), Neo4j (secondary), Cypher query language, std::shared_mutex, nlohmann/json, spdlog, cpp-httplib

---

## Decision Record

| Decision | Choice | Reason |
|----------|--------|--------|
| Degradation strategy | Read-only mode | Guarantees data consistency; no orphan data in memory |
| GraphDB priority | StellarDB first | Production system already deployed |
| SSO solution | Manual Bearer Token | Simplest; StellarDB Guardian doesn't support programmatic OAuth2 |
| Transformation scope | Full authority source transformation | User requested comprehensive change |
| Cache architecture | Cache-first (Option A) | Preserves current performance for simple queries; graphDB handles complex queries |
| Dual-track elimination | Remove Class.superClasses field, use subClassOf index only | Single source of truth eliminates inconsistency |
| WAL role | Audit log + backup recovery path | GraphDB is primary recovery; WAL/snapshot for when graphDB is unavailable |
| Read-only recovery | Full replacement from graphDB | Simple and reliable; WAL preserves audit trail of read-only period operations |

---

## Section 1: Data Flow and Write Path

### Write flow (all mutation operations)

```
1. Receive write request (addClass, addTriple, addIndividual, etc.)
2. unique_lock(mutex_) — acquire write lock
3. Construct graphDB write operation (Cypher/REST)
4. Submit to graphDB → wait for confirmation
   ├─ Success → update memory indexes → record WAL → return success
   └─ Failure → do NOT update memory → return error (write rejected)
5. Release lock
```

**Key change:** Current behavior writes to memory first then asynchronously to graphDB (graphDB failure doesn't affect memory). After transformation: write to graphDB first, only update memory after confirmation. GraphDB unavailable = write rejected = read-only degradation.

### Batch write optimization

`batchAddTriples`, `batchAddClasses`, etc. use graphDB batch APIs (UNWIND Cypher) — single network round-trip for batch writes, update memory after batch confirmation.

### WAL role change

WAL transitions from "never-replayed backup" to "audit log after graphDB confirmation" — primarily for debugging and disaster recovery, not the main recovery path (main path is graphDB load).

---

## Section 2: Startup Recovery and Cache Loading

### Startup flow

```
1. Bootstrap::initialize()
2. Connect to graphDB (StellarDB/Neo4j)
   ├─ Success → normal mode
   └─ Failure → read-only degradation mode
3. Full data load from graphDB to memory
   a. Load all Class nodes → rebuild classes_ map + subClassOf index
   b. Load all Individual nodes → rebuild individuals_ map
   c. Load all Relation edges → rebuild relations_ map
   d. Load all Triple edges → rebuild TripleStore + 6 indexes
4. Compute derived indexes (transitive closure, superCache_, etc.)
5. Connect to vectorDB (Hippo) — vectorDB does not need to backfill memory
6. Service ready
```

### GraphDB load Cypher queries

```cypher
-- Load all classes
MATCH (c:Class) RETURN c.id, c.name, c.superClasses, c.properties

-- Load all individuals
MATCH (i:Individual) RETURN i.id, i.classId, i.name, i.properties

-- Load all relations
MATCH (a)-[r:RELATED]->(b) RETURN type(r), r.properties, startNode(r).id, endNode(r).id

-- Load all triple edges
MATCH (s)-[t:TRIPLE]->(o) RETURN startNode(t).id AS subject, type(t) AS predicate, endNode(t).id AS object
```

### Incremental recovery (future enhancement)

If WAL exists and records graphDB-confirmed changes, compare graphDB load timestamp vs WAL's last LSN to skip already-confirmed entries. First version: full load only, optimize later.

### Read-only degradation mode startup

```
1. GraphDB connection failed
2. Try to restore from latest Snapshot → if snapshot exists, load into memory
3. Try to replay WAL → if WAL exists, incrementally replay on top of snapshot
4. Mark isReadOnly_ = true
5. All write APIs return 503 "Service in read-only mode: graph database unavailable"
6. Log continuous warnings, background thread retries graphDB connection
```

### Auto-reconnection

Background thread attempts graphDB connection every 30 seconds. On success: execute full load + atomically switch to normal mode. During switch, briefly block all requests with `unique_lock` (1-2 seconds depending on data volume).

---

## Section 3: Graph Query Delegation

### Delegation principle

Simple single-hop queries use memory cache; multi-hop traversals and graph algorithms use graphDB.

| Query type | Execution location | Reason |
|-----------|-------------------|--------|
| findBySubject/Predicate/Object | Memory | O(1) hash index, 100x faster than network |
| getIndividual/getClass | Memory | Point lookup, cache hit |
| getDirectSubClasses | Memory | subClassOfIndex_ O(1) |
| getAllSubClasses | GraphDB | Multi-hop traversal, graphDB O(depth) vs memory O(n×b^d) |
| findPath | GraphDB | Shortest path algorithm, graphDB native |
| SPARQL queries | GraphDB | Standard graph query language, graphDB optimized |
| Community detection/PageRank | GraphDB | Graph algorithms, only graphDB can do efficiently |
| semanticSearch | VectorDB + Memory | Vector similarity via Hippo, details from memory |
| transitiveClosure | GraphDB | Deep traversal, graphDB native |

### Delegation implementation pattern

```cpp
std::vector<String> HybridStorage::getAllSubClasses(const String& classId) const {
    // Prefer graphDB
    if (graphDB_ && graphDB_->isConnected()) {
        auto result = graphDB_->getSubClassClosure(classId);
        if (result.success) return result.classes;
    }
    // Fallback to memory
    return getAllSubClassesFromMemory(classId);
}
```

When graphDB is unavailable, all delegated queries fall back to memory implementations. Performance regresses to current levels but functionality is not lost.

---

## Section 4: Dual-Track Data Consistency

### Problem

Currently `Class.superClasses` property and `subClassOf` triples store the same information with no synchronization — they can diverge.

### Solution: Eliminate dual-track, property graph model as authority

| Data | Old (dual-track) | New (single authority) |
|------|-----------------|----------------------|
| Class inheritance | Class.superClasses property + subClassOf triple | **subClassOf edge only** (graphDB authority) |
| Individual type | Individual.classId property + instanceOf triple | **instanceOf edge (authority) + classId cache field** (populated from edge on load, kept for O(1) access) |
| Class properties | Class.properties JSON | **Class node properties** (graphDB authority) |
| General relations | Triple {s,p,o} | **TRIPLE edge** (graphDB authority) |

### Memory cache data structure change

```cpp
// Old: Class stores superClasses vector
struct Class {
    String id, name;
    std::vector<String> superClasses;  // ← redundant, remove
    Json properties;
};

// New: Class does not store superClasses, retrieved via index
struct Class {
    String id, name;
    Json properties;
    // superClasses retrieved via subClassOfIndex_ reverse lookup
};
```

### subClassOfIndex_ becomes the sole inheritance index

```cpp
// HybridStorage additions
std::unordered_map<String, std::vector<String>> subClassOfIndex_;
    // key=parent class, value=direct subclass list
mutable std::unordered_map<String, std::vector<String>> superCache_;
    // key=class, value=all superclass closure (lazy computed)
mutable bool superCacheValid_ = false;
```

### Access pattern change

```cpp
// Old: classObj.superClasses
// New:
std::vector<String> HybridStorage::getSuperClasses(const String& classId) const {
    if (superCacheValid_) {
        auto it = superCache_.find(classId);
        if (it != superCache_.end()) return it->second;
    }
    // GraphDB query or in-memory DFS computation
}
```

### Write-time index maintenance

```cpp
bool HybridStorage::addClass(const Class& cls) {
    // 1. Write to graphDB: create Class node
    // 2. After confirmation, update memory
    classes_[cls.id] = cls;  // cls no longer contains superClasses field
    // 3. Inheritance maintained via addTriple(subClassOf)
    superCacheValid_ = false;
}
```

### Migration compatibility

Existing code that directly accesses `classObj.superClasses` (~15 call sites) is mechanically replaced with `storage->getSuperClasses(classId)`. No logic change involved.

### JSON serialization compatibility

`/api/classes` still returns `superClasses` field (dynamically populated from index). API interface unchanged, frontend has no awareness of the change.

---

## Section 5: Read-Only Degradation Mode

### Degradation triggers

1. GraphDB connection failure at startup
2. GraphDB write operation timeout/failure during runtime (3 consecutive failures trigger degradation)
3. GraphDB health check consecutive failures (background check every 30s, 3 failures trigger)

### State machine

```
                  Connection success + data load
    [STOPPED] ──────────────────→ [NORMAL]
        ↑                              │
        │                    3 consecutive write failures
        │                              ↓
        │                        [DEGRADING]
        │                              │
        └──── Reconnect failed ←──── [READ_ONLY]
                                       │
                              Reconnect success + full load
                                       ↓
                                  [NORMAL]
```

### Read-only mode behavior

```cpp
// HybridStorage new members
bool isReadOnly_ = false;
int consecutiveWriteFailures_ = 0;
static constexpr int MAX_WRITE_FAILURES = 3;

// Unified check in all write methods
bool HybridStorage::addTriple(const Triple& triple) {
    if (isReadOnly_) {
        spdlog::warn("Write rejected: service in read-only mode");
        return false;
    }

    // Attempt graphDB write
    if (graphDB_ && graphDB_->isConnected()) {
        bool ok = graphDB_->createTriple(triple);
        if (!ok) {
            consecutiveWriteFailures_++;
            if (consecutiveWriteFailures_ >= MAX_WRITE_FAILURES) {
                isReadOnly_ = true;
                spdlog::error("GraphDB write failures exceeded threshold, entering read-only mode");
                startReconnectionLoop();
            }
            return false;
        }
        consecutiveWriteFailures_ = 0;
    }

    // Update memory
    std::unique_lock lock(mutex_);
    tripleStore_.add(triple);
}
```

### HTTP API response in read-only mode

```cpp
if (storage->isReadOnly()) {
    res.status = 503;
    res.set_content(R"({"error":"Service in read-only mode","reason":"graph database unavailable"})",
                    "application/json");
    return;
}
```

### Enhanced /api/health response

```json
{
    "status": "degraded",
    "mode": "read_only",
    "graphDB": {
        "connected": false,
        "lastError": "Connection timeout",
        "reconnectAttempt": 4
    },
    "vectorDB": { "connected": true },
    "memory": {
        "classes": 156,
        "individuals": 2340,
        "triples": 8920
    }
}
```

### Auto-recovery

Background reconnection thread attempts graphDB connection every 30s. On success: execute full load + atomically switch `isReadOnly_ = false`. During switch, briefly block all requests with `unique_lock` to ensure data consistency.

---

## Section 6: StellarDB SSO Integration

### Manual Token configuration

**Token acquisition flow:**
1. Admin opens `https://10.100.12.212:38282/manager/home` in browser
2. Login with hive/KanwxHive20@26 via Guardian SSO
3. After login, copy Bearer Token from browser DevTools (Cookie or Authorization header)
4. Paste into `config.json` `stellardb.token` field

**Config example:**

```json
{
    "stellardb": {
        "enabled": true,
        "host": "10.100.12.212",
        "port": 38282,
        "username": "hive",
        "password": "KanwxHive20@26",
        "token": "eyJhbGciOiJSUzI1NiIs...",
        "useHttps": true,
        "graphName": "ontology"
    }
}
```

### Authentication priority logic

```cpp
bool StellarDBClient::authenticate() {
    // 1. Prefer pre-configured Token
    if (!config_.token.empty()) {
        authToken_ = config_.token;
        if (verifyToken()) return true;  // lightweight query to verify
        spdlog::warn("Pre-configured token invalid or expired");
        authToken_ = "";
    }

    // 2. Try username/password (for direct REST API ports)
    if (!config_.username.empty()) {
        if (tryBasicAuth()) return true;
    }

    // 3. SSO gateway detection
    spdlog::error("StellarDB auth failed. If using Guardian SSO, "
                  "obtain a token via browser login and set 'stellardb.token' in config.");
    return false;
}
```

### Token expiration handling

Guardian Tokens typically have a validity period (default 24h). When expired, graphDB writes fail, triggering read-only degradation. Admin must update Token and restart service (or use hot-reload API endpoint in future).

### Hot-reload Token (reserved for future, first version can exclude)

```cpp
// Reserved: update token without restart
void StellarDBClient::updateToken(const String& newToken) {
    std::unique_lock lock(mutex_);
    authToken_ = newToken;
}

// Reserved API endpoint
// PUT /api/admin/graphdb/token  { "token": "new_token_here" }
```

---

## Section 7: GraphDatabase Interface Unification

### Unified interface

```cpp
class GraphDatabase {
public:
    // Lifecycle
    virtual bool connect() = 0;
    virtual void close() = 0;
    virtual bool isConnected() const = 0;
    virtual HealthStatus healthCheck() const = 0;

    // Data loading (for startup recovery)
    virtual LoadResult loadAllClasses(std::vector<Class>& out) = 0;
    virtual LoadResult loadAllIndividuals(std::vector<Individual>& out) = 0;
    virtual LoadResult loadAllRelations(std::vector<Relation>& out) = 0;
    virtual LoadResult loadAllTriples(std::vector<Triple>& out) = 0;

    // Single writes
    virtual bool createClass(const Class& cls) = 0;
    virtual bool createIndividual(const Individual& ind) = 0;
    virtual bool createTriple(const Triple& triple) = 0;
    virtual bool createRelation(const Relation& rel) = 0;

    // Single deletes
    virtual bool deleteClass(const String& id) = 0;
    virtual bool deleteIndividual(const String& id) = 0;
    virtual bool deleteTriple(const Triple& triple) = 0;
    virtual bool deleteRelation(const String& id) = 0;

    // Batch writes
    virtual BatchResult batchCreate(const std::vector<Class>& classes,
                                    const std::vector<Individual>& individuals,
                                    const std::vector<Triple>& triples) = 0;

    // Graph queries
    virtual GraphQueryResult query(const String& cypher, const Json& params = {}) = 0;
    virtual PathResult findShortestPath(const String& from, const String& to) = 0;
    virtual std::vector<String> getSubClassClosure(const String& classId) = 0;
    virtual std::vector<String> getSuperClassClosure(const String& classId) = 0;

    // Graph algorithms
    virtual CommunityResult detectCommunities(const String& algorithm,
                                              const Json& params = {}) = 0;
};
```

### Supporting types

```cpp
struct LoadResult {
    bool success;
    int count;
    String error;
};

struct BatchResult {
    bool success;
    int created;
    int failed;
    String error;
};

struct PathResult {
    bool success;
    std::vector<String> nodes;
    std::vector<String> edges;
    int totalWeight;
    String error;
};

struct GraphQueryResult {
    bool success;
    std::vector<Json> rows;  // each row is a JSON object
    String error;
};

struct CommunityResult {
    bool success;
    std::vector<std::vector<String>> communities;
    String error;
};

struct HealthStatus {
    bool connected;
    String version;
    int nodeCount;
    int edgeCount;
    String error;
};
```

### StellarDB implementation mapping

| Method | Cypher/REST | Notes |
|--------|-------------|-------|
| loadAllClasses | `MATCH (c:Class) RETURN c` | Batch return |
| createClass | `CREATE (c:Class {id:$id, name:$name, properties:$props})` | Parameterized |
| createTriple | `MATCH (s),(o) WHERE s.id=$s AND o.id=$o CREATE (s)-[:TRIPLE {predicate:$p}]->(o)` | Match endpoints first |
| batchCreate | `UNWIND $batch AS row MERGE (n {id:row.id}) SET n+=row.props` | Batch UNWIND |
| findShortestPath | `MATCH path=shortestPath((s{id:$from})-[*]-(t{id:$to})) RETURN path` | Native algorithm |
| getSubClassClosure | `MATCH (c:Class{id:$id})<-[:subClassOf*]-(sub) RETURN sub.id` | Variable-length path |
| detectCommunities | `CALL algo.louvain.stream('Class','subClassOf',{}) YIELD nodeId,community` | Stored procedure |

### StellarDB vs Neo4j differences

Both support Cypher with high syntax compatibility. Key differences:

- **Authentication:** StellarDB uses Bearer Token (Guardian), Neo4j uses Basic Auth
- **HTTPS:** StellarDB defaults to HTTPS + self-signed certs, needs SSL verification bypass config
- **Graph space:** StellarDB has multi-graphspace concept (graphName), queries need: `USE ontology MATCH ...`
- **Batch API:** StellarDB may not support UNWIND, fallback to multiple single writes

Differences isolated in subclasses, public interface unchanged.

---

## Section 8: WAL and Snapshot Role Restructuring

### Current problem

WAL writes but never replays; snapshots create but never restore. Both are "write-and-forget."

### Restructured roles

```
Primary recovery path: GraphDB full load (at startup)
Backup recovery path: Snapshot + WAL (when graphDB unavailable)
Audit log: WAL (always recorded, for post-hoc audit and disaster recovery)
```

### WAL restructuring

```cpp
class WalManager {
public:
    // Existing: write log (unchanged)
    Lsn append(WalEntry::Type type, const Json& payload);

    // New: get last confirmed LSN
    Lsn lastConfirmedLsn() const;

    // Refactored: replay log (from specified LSN)
    // Used for backup recovery when graphDB unavailable
    size_t replayFrom(Lsn fromLsn, HybridStorage& storage);

    // New: graphDB confirmation marker
    // Records "this change has been confirmed persisted by graphDB"
    void markConfirmed(Lsn lsn);

    // New: truncate confirmed log entries
    // After graphDB confirmation, earlier WAL records can be safely deleted
    void truncateConfirmed();
};
```

### WAL entry confirmation marker

```json
{
    "lsn": 12345,
    "type": "ADD_TRIPLE",
    "payload": {"subject":"S","predicate":"P","object":"O"},
    "timestamp": "2026-06-02T10:30:00Z",
    "confirmed": true
}
```

### Snapshot restructuring

```cpp
class SnapshotManager {
public:
    // Existing: create snapshot (unchanged)
    bool createSnapshot(const HybridStorage& storage);

    // Refactored: restore snapshot (actually functional)
    bool restoreSnapshot(HybridStorage& storage, const String& snapshotId = "latest");

    // New: get latest snapshot info
    std::optional<SnapshotInfo> getLatestSnapshot() const;

    // New: snapshot timestamp
    // Used to determine if snapshot is newer than graphDB data
    std::chrono::system_clock::time_point getLatestSnapshotTime() const;
};
```

### Recovery priority decision tree

```
Startup → GraphDB available?
  ├─ Yes → Full load from graphDB → Ignore snapshot/WAL (graphDB is authority)
  └─ No → Snapshot exists?
       ├─ Yes → Load latest snapshot → WAL exists?
       │        ├─ Yes → Replay WAL from snapshot point → Read-only mode
       │        └─ No → Enter read-only mode directly
       └─ No → WAL exists?
            ├─ Yes → Replay WAL from beginning → Read-only mode
            └─ No → Empty memory → Read-only mode
```

### Key constraint

After recovery from snapshot/WAL into read-only mode, newly written data exists only in memory. When graphDB reconnects:
- **Full replacement** (chosen for v1): GraphDB load overwrites memory (discards read-only period in-memory data) — simple, reliable, WAL preserves audit trail
- **Merge** (future): Compare memory vs graphDB differences, write differences back — complex but preserves data

First version uses full replacement. Read-only period operations are recorded in WAL for post-hoc audit.

---

## Section 9: Migration Path

### Problem: existing instances have data in memory but empty graphDB

When upgrading an existing instance, the graphDB is empty but memory may contain data from the previous session (lost on restart) or from API calls during the current session. A bootstrap migration is needed to populate graphDB from the current in-memory state.

### Migration flow (one-time, on first startup with graphDB authority)

```
1. Connect to graphDB → Success
2. Load data from graphDB → Returns 0 classes/individuals/triples (empty)
3. Check: is this a fresh graphDB or a migrated one?
   a. If memory has data AND graphDB is empty → MIGRATION NEEDED
   b. If memory is empty AND graphDB is empty → Fresh start, no migration
   c. If graphDB has data → Normal load, no migration
4. Migration: batch-write all memory data to graphDB
   a. batchCreate(classes_, individuals_, triples_)
   b. Verify counts match
5. Continue normal startup
```

### Detection heuristic

```cpp
// In Bootstrap::initialize() after graphDB load
if (graphDB->isConnected()) {
    auto loadResult = graphDB->loadAllClasses(classes);
    if (loadResult.success && loadResult.count == 0 && !classes_.empty()) {
        spdlog::info("Detected empty graphDB with in-memory data — running initial migration");
        migrateMemoryToGraphDB();
    }
}
```

### Note on Individual.classId

Unlike Class.superClasses, Individual.classId is retained as a denormalized cache field. It is populated from the instanceOf edge on graphDB load and maintained on writes. This avoids a graph traversal on every individual access. The authority source is the instanceOf edge in graphDB; the classId field is a performance optimization that must be kept consistent.

---

## Section 10: Configuration Schema Additions

New and changed config fields for graphDB authority mode:

```json
{
    "storage": {
        "stellardb": {
            "enabled": true,
            "token": "",
            "authorityMode": true,
            "sslVerify": false,
            "reconnectIntervalSeconds": 30,
            "writeTimeoutMs": 5000,
            "loadTimeoutMs": 60000
        },
        "neo4j": {
            "enabled": false,
            "authorityMode": false,
            "reconnectIntervalSeconds": 30,
            "writeTimeoutMs": 5000,
            "loadTimeoutMs": 60000
        }
    },
    "degradation": {
        "maxConsecutiveWriteFailures": 3,
        "autoRecoveryEnabled": true,
        "recoveryLoadTimeoutMs": 60000
    }
}
```
