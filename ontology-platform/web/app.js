// Ontology Platform Web Frontend - Enhanced Version
const API_BASE = window.location.origin;
let currentPage = 1;

// ============================================================================
// API Helper
// ============================================================================
async function api(endpoint, options = {}) {
    const response = await fetch(API_BASE + endpoint, {
        ...options,
        headers: { 'Content-Type': 'application/json', ...options.headers },
        body: options.body ? JSON.stringify(options.body) : undefined
    });
    if (!response.ok) {
        const error = await response.text();
        throw new Error(error || `API Error: ${response.status}`);
    }
    return response.json();
}

// ============================================================================
// Notification System
// ============================================================================
function notify(message, type = 'success') {
    const container = document.getElementById('notifications');
    const div = document.createElement('div');
    div.className = `notification ${type}`;
    div.innerHTML = `<i class="fas fa-${type === 'success' ? 'check-circle' : type === 'error' ? 'times-circle' : 'exclamation-circle'}"></i><span>${message}</span>`;
    container.appendChild(div);
    setTimeout(() => div.remove(), 4000);
}

// ============================================================================
// Loading State
// ============================================================================
function showLoading() {
    document.getElementById('loading-overlay').classList.remove('hidden');
}

function hideLoading() {
    document.getElementById('loading-overlay').classList.add('hidden');
}

// ============================================================================
// Sidebar Navigation
// ============================================================================
document.querySelectorAll('.nav-item').forEach(item => {
    item.addEventListener('click', (e) => {
        e.preventDefault();

        // Update active state
        document.querySelectorAll('.nav-item').forEach(i => i.classList.remove('active'));
        item.classList.add('active');

        // Show page
        const pageId = item.dataset.page;
        document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
        document.getElementById(`page-${pageId}`).classList.add('active');

        // Update page title
        const pageTitle = item.querySelector('span').textContent;
        document.getElementById('page-title').textContent = pageTitle;

        // Load page data
        loadPageData(pageId);
    });
});

// Sidebar toggle
document.getElementById('sidebar-toggle').addEventListener('click', () => {
    document.querySelector('.sidebar').classList.toggle('collapsed');
});

// ============================================================================
// Tab Navigation (for sub-tabs within pages)
// ============================================================================
document.querySelectorAll('.tabs .tab').forEach(tab => {
    tab.addEventListener('click', () => {
        const tabsContainer = tab.closest('.tabs-container');
        if (!tabsContainer) return;

        tabsContainer.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
        tabsContainer.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));

        tab.classList.add('active');
        const tabId = tab.dataset.tab;
        document.getElementById(`tab-${tabId}`).classList.add('active');

        loadTabData(tabId);
    });
});

// ============================================================================
// Range Input Value Display
// ============================================================================
document.querySelectorAll('input[type="range"]').forEach(input => {
    const updateValue = () => {
        const valueSpan = document.getElementById(input.id + '-value');
        if (valueSpan) valueSpan.textContent = input.value;
    };
    input.addEventListener('input', updateValue);
    updateValue();
});

// ============================================================================
// Data Loading Functions
// ============================================================================
// (Primary loadPageData is defined later — this is a placeholder removed to avoid duplication)

async function loadTabData(tab) {
    try {
        switch (tab) {
            case 'classes':
                const classes = await api('/api/classes');
                renderClasses(classes);
                break;
            case 'relations':
                const relations = await api('/api/relations');
                renderRelations(relations);
                break;
            case 'individuals':
                const individuals = await api('/api/individuals');
                renderIndividuals(individuals);
                break;
        }
    } catch (e) {
        console.error('Load tab error:', e);
    }
}

// ============================================================================
// Dashboard Loading
// ============================================================================
async function loadDashboard() {
    // Load stats
    try {
        const stats = await api('/api/stats');
        document.getElementById('stat-classes').textContent = stats.classes || 0;
        document.getElementById('stat-individuals').textContent = stats.individuals || 0;
        document.getElementById('stat-triples').textContent = stats.triples || 0;
        document.getElementById('stat-relations').textContent = stats.relations || 0;
        const symEl = document.getElementById('stat-symbolic');
        const neurEl = document.getElementById('stat-neural');
        const rulesEl = document.getElementById('stat-rules');
        if (symEl) symEl.textContent = stats.symbolicRules || 0;
        if (neurEl) neurEl.textContent = stats.neuralEnabled || 0;
        if (rulesEl) rulesEl.textContent = stats.rules || 0;
    } catch (e) {
        console.error('Stats load error:', e);
    }

    // Load class hierarchy
    try {
        const classes = await api('/api/classes');
        renderClassHierarchy(classes);
    } catch (e) {
        console.error('Hierarchy load error:', e);
    }

    // Load recent activities
    await renderRecentActivities();

    // Load storage status
    loadStorageStatus();
}

function renderClassHierarchy(classes) {
    const container = document.getElementById('class-hierarchy');
    if (!classes || classes.length === 0) {
        container.innerHTML = '<div class="empty-state"><p>暂无类定义</p></div>';
        return;
    }

    // Build hierarchy tree
    const classMap = new Map(classes.map(c => [c.id, { ...c, children: [] }]));
    const roots = [];

    classes.forEach(c => {
        const node = classMap.get(c.id);
        if (c.superClasses && c.superClasses.length > 0) {
            const parent = classMap.get(c.superClasses[0]);
            if (parent) {
                parent.children.push(node);
            } else {
                roots.push(node);
            }
        } else {
            roots.push(node);
        }
    });

    // Render tree
    function renderNode(node, depth = 0) {
        const indent = '  '.repeat(depth);
        let html = `<div class="hierarchy-item" style="margin-left: ${depth * 20}px">
            <i class="fas fa-cube"></i>
            <span>${node.name || node.id}</span>
        </div>`;
        if (node.children.length > 0) {
            html += `<div class="hierarchy-node">${node.children.map(c => renderNode(c, depth + 1)).join('')}</div>`;
        }
        return html;
    }

    container.innerHTML = roots.map(r => renderNode(r)).join('');
}

async function renderRecentActivities() {
    const container = document.getElementById('recent-activities');
    try {
        const triples = await api('/api/triples?limit=5');
        if (Array.isArray(triples) && triples.length > 0) {
            container.innerHTML = triples.slice(0, 5).map(t => `
                <div class="activity-item">
                    <div class="activity-icon create">
                        <i class="fas fa-plus"></i>
                    </div>
                    <div class="activity-content">
                        <div class="activity-text">添加了三元组 (${t.subject}, ${t.predicate}, ${t.object})</div>
                        <div class="activity-time">置信度: ${(t.confidence * 100).toFixed(0)}%</div>
                    </div>
                </div>
            `).join('');
            return;
        }
    } catch (e) { /* fallback to empty */ }

    container.innerHTML = '<p class="text-muted" style="text-align: center; padding: 1rem">暂无活动</p>';
}

async function loadStorageStatus() {
    // Check Neo4j
    try {
        const neo4jStatus = await api('/api/storage/neo4j/status');
        const neo4jEl = document.getElementById('neo4j-status');
        neo4jEl.textContent = neo4jStatus.connected ? '已连接' : '未连接';
        neo4jEl.className = `storage-status-text ${neo4jStatus.connected ? 'connected' : ''}`;
    } catch (e) {
        document.getElementById('neo4j-status').textContent = '未连接';
    }

    // Check Milvus
    try {
        const milvusStatus = await api('/api/storage/milvus/status');
        const milvusEl = document.getElementById('milvus-status');
        milvusEl.textContent = milvusStatus.connected ? '已连接' : '未连接';
        milvusEl.className = `storage-status-text ${milvusStatus.connected ? 'connected' : ''}`;
    } catch (e) {
        document.getElementById('milvus-status').textContent = '未连接';
    }

    // Check Qdrant
    try {
        const qdrantStatus = await api('/api/storage/qdrant/status');
        const qdrantEl = document.getElementById('qdrant-status');
        qdrantEl.textContent = qdrantStatus.connected ? '已连接' : '未连接';
        qdrantEl.className = `storage-status-text ${qdrantStatus.connected ? 'connected' : ''}`;
    } catch (e) {
        document.getElementById('qdrant-status').textContent = '未连接';
    }

    // Check StellarDB
    try {
        const stellardbStatus = await api('/api/storage/stellardb/status');
        const stellardbEl = document.getElementById('stellardb-status');
        stellardbEl.textContent = stellardbStatus.connected ? '已连接' : '未连接';
        stellardbEl.className = `storage-status-text ${stellardbStatus.connected ? 'connected' : ''}`;
    } catch (e) {
        document.getElementById('stellardb-status').textContent = '未连接';
    }

    // Check Hippo
    try {
        const hippoStatus = await api('/api/storage/hippo/status');
        const hippoEl = document.getElementById('hippo-status');
        hippoEl.textContent = hippoStatus.connected ? '已连接' : '未连接';
        hippoEl.className = `storage-status-text ${hippoStatus.connected ? 'connected' : ''}`;
    } catch (e) {
        document.getElementById('hippo-status').textContent = '未连接';
    }
}

// ============================================================================
// Classes Management
// ============================================================================
function renderClasses(classes) {
    const container = document.getElementById('class-list');
    if (!classes || classes.length === 0) {
        container.innerHTML = '<div class="empty-state"><i class="fas fa-cubes"></i><p>暂无类定义</p></div>';
        return;
    }

    container.innerHTML = classes.map(c => `
        <div class="entity-item">
            <div class="entity-info">
                <h4>${c.name || c.id}</h4>
                <p>${c.description || (c.superClasses ? `继承: ${c.superClasses.join(', ')}` : '顶层类')}</p>
            </div>
            <div class="entity-actions">
                <button class="btn-sm" onclick="editClass('${c.id}')"><i class="fas fa-edit"></i></button>
                <button class="btn-sm" onclick="deleteEntity('class', '${c.id}')"><i class="fas fa-trash"></i></button>
            </div>
        </div>
    `).join('');
}

function filterClasses() {
    const query = document.getElementById('class-search').value.toLowerCase();
    document.querySelectorAll('#class-list .entity-item').forEach(item => {
        const name = item.querySelector('h4').textContent.toLowerCase();
        const desc = item.querySelector('p').textContent.toLowerCase();
        item.style.display = (name.includes(query) || desc.includes(query)) ? '' : 'none';
    });
}

// ============================================================================
// Relations Management
// ============================================================================
function renderRelations(relations) {
    const tbody = document.getElementById('relation-tbody');
    if (!relations || relations.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" class="text-center text-muted">暂无关系定义</td></tr>';
        return;
    }

    tbody.innerHTML = relations.map(r => `
        <tr>
            <td><code>${r.id}</code></td>
            <td>${r.name || r.id}</td>
            <td>${r.domain || '-'}</td>
            <td>${r.range || '-'}</td>
            <td>${renderRelationProperties(r)}</td>
            <td>
                <button class="btn-sm" onclick="editRelation('${r.id}')"><i class="fas fa-edit"></i></button>
                <button class="btn-sm" onclick="deleteEntity('relation', '${r.id}')"><i class="fas fa-trash"></i></button>
            </td>
        </tr>
    `).join('');
}

function renderRelationProperties(r) {
    const props = [];
    if (r.transitive) props.push('传递');
    if (r.symmetric) props.push('对称');
    if (r.reflexive) props.push('自反');
    if (r.inverseOf) props.push(`逆: ${r.inverseOf}`);
    return props.length > 0 ? props.join(', ') : '-';
}

function filterRelations() {
    const query = document.getElementById('relation-search').value.toLowerCase();
    document.querySelectorAll('#relation-tbody tr').forEach(row => {
        const text = row.textContent.toLowerCase();
        row.style.display = text.includes(query) ? '' : 'none';
    });
}

// ============================================================================
// Individuals Management
// ============================================================================
function renderIndividuals(individuals) {
    const container = document.getElementById('individual-list');
    if (!individuals || individuals.length === 0) {
        container.innerHTML = '<div class="empty-state"><i class="fas fa-user-friends"></i><p>暂无实例</p></div>';
        return;
    }

    container.innerHTML = individuals.map(ind => `
        <div class="entity-item">
            <div class="entity-info">
                <h4>${ind.name || ind.id}</h4>
                <p>类: ${ind.classId || 'Unknown'}</p>
            </div>
            <div class="entity-actions">
                <button class="btn-sm" onclick="editIndividual('${ind.id}')"><i class="fas fa-edit"></i></button>
                <button class="btn-sm" onclick="deleteEntity('individual', '${ind.id}')"><i class="fas fa-trash"></i></button>
            </div>
        </div>
    `).join('');
}

function filterIndividuals() {
    const query = document.getElementById('individual-search').value.toLowerCase();
    const classFilter = document.getElementById('individual-class-filter').value;

    document.querySelectorAll('#individual-list .entity-item').forEach(item => {
        const name = item.querySelector('h4').textContent.toLowerCase();
        const classText = item.querySelector('p').textContent;
        const matchesQuery = name.includes(query);
        const matchesClass = !classFilter || classText.includes(classFilter);
        item.style.display = (matchesQuery && matchesClass) ? '' : 'none';
    });
}

// ============================================================================
// Triples Management
// ============================================================================
async function loadTriples() {
    try {
        const triples = await api('/api/triples?limit=100');
        renderTriples(triples);
    } catch (e) {
        console.error('Triples load error:', e);
    }
}

function renderTriples(triples) {
    const tbody = document.getElementById('triple-tbody');
    if (!triples || triples.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" class="text-center text-muted">暂无三元组</td></tr>';
        return;
    }

    tbody.innerHTML = triples.map(t => `
        <tr>
            <td class="triple-subject">${t.subject}</td>
            <td class="triple-predicate">${t.predicate}</td>
            <td class="triple-object">${t.object}</td>
            <td>${(t.confidence || 1).toFixed(2)}</td>
            <td>${t.source || 'user'}</td>
            <td>
                <button class="btn-sm" onclick="deleteTriple('${t.subject}', '${t.predicate}', '${t.object}')">
                    <i class="fas fa-trash"></i>
                </button>
            </td>
        </tr>
    `).join('');
}

async function queryTriples() {
    const subject = document.getElementById('triple-subject').value;
    const predicate = document.getElementById('triple-predicate').value;
    const object = document.getElementById('triple-object').value;

    try {
        showLoading();
        const body = {};
        if (subject) body.subject = subject;
        if (predicate) body.predicate = predicate;
        if (object) body.object = object;

        const result = await api('/api/triples/query', { method: 'POST', body });
        renderTriples(result);
        notify(`找到 ${result.length} 条匹配结果`);
    } catch (e) {
        notify(e.message, 'error');
    } finally {
        hideLoading();
    }
}

// ============================================================================
// Rules Management
// ============================================================================
async function loadRules() {
    try {
        const rules = await api('/api/rules');
        renderRules(rules);
    } catch (e) {
        // If endpoint doesn't exist, show empty state
        document.getElementById('rule-list').innerHTML = '<div class="empty-state"><p>暂无规则定义</p></div>';
    }
}

function renderRules(rules) {
    const container = document.getElementById('rule-list');
    if (!rules || rules.length === 0) {
        container.innerHTML = '<div class="empty-state"><i class="fas fa-code-branch"></i><p>暂无规则定义</p></div>';
        return;
    }

    container.innerHTML = rules.map(r => `
        <div class="rule-item">
            <div class="rule-header">
                <span class="rule-name">${r.name || r.id}</span>
                <div class="entity-actions">
                    <button class="btn-sm" onclick="editRule('${r.id}')"><i class="fas fa-edit"></i></button>
                    <button class="btn-sm" onclick="deleteRule('${r.id}')"><i class="fas fa-trash"></i></button>
                </div>
            </div>
            <div class="rule-body">${r.body || r.definition}</div>
        </div>
    `).join('');
}

// ============================================================================
// Inference Functions
// ============================================================================
async function performInference() {
    const entityId = document.getElementById('infer-entity').value;
    const depth = parseInt(document.getElementById('infer-depth').value);
    const type = document.getElementById('reasoning-type').value;
    const threshold = parseFloat(document.getElementById('confidence-threshold').value);
    const symbolicWeight = parseFloat(document.getElementById('symbolic-weight').value);
    const neuralWeight = parseFloat(document.getElementById('neural-weight').value);

    if (!entityId) {
        notify('请输入实体ID', 'warning');
        return;
    }

    try {
        showLoading();
        const result = await api('/api/infer', {
            method: 'POST',
            body: {
                entityId,
                maxDepth: depth,
                type,
                confidenceThreshold: threshold,
                symbolicWeight,
                neuralWeight
            }
        });

        renderInferenceResults(result);
        notify('推理完成');
    } catch (e) {
        notify(e.message, 'error');
    } finally {
        hideLoading();
    }
}

function renderInferenceResults(result) {
    const container = document.getElementById('infer-results');
    const facts = result.facts || result.results || result || [];

    if (facts.length === 0) {
        container.innerHTML = '<div class="empty-state"><i class="fas fa-lightbulb"></i><p>未发现新知识</p></div>';
        return;
    }

    container.innerHTML = `
        <h4 style="margin-bottom: 1rem">推理结果 (${facts.length} 条)</h4>
        <div class="triple-table-container" style="border-radius: 0.5rem">
            <table class="triple-table">
                <thead>
                    <tr>
                        <th>主语</th>
                        <th>谓词</th>
                        <th>宾语</th>
                        <th>置信度</th>
                        <th>来源</th>
                    </tr>
                </thead>
                <tbody>
                    ${facts.map(f => `
                        <tr>
                            <td class="triple-subject">${f.subject}</td>
                            <td class="triple-predicate">${f.predicate}</td>
                            <td class="triple-object">${f.object}</td>
                            <td>${(f.confidence || 1).toFixed(2)}</td>
                            <td>${f.inferred ? '推理' : '原始'}</td>
                        </tr>
                    `).join('')}
                </tbody>
            </table>
        </div>
    `;
}

async function executeRules() {
    const maxIterations = parseInt(document.getElementById('rule-max-iterations').value);

    try {
        showLoading();
        const result = await api('/api/rules/execute', {
            method: 'POST',
            body: { maxIterations }
        });

        document.getElementById('rule-infer-results').innerHTML = `
            <div class="mt-3">
                <div class="stat-card" style="box-shadow: none; background: var(--bg)">
                    <div class="stat-content">
                        <div class="stat-value">${result.newFacts || 0}</div>
                        <div class="stat-label">新增事实</div>
                    </div>
                </div>
            </div>
        `;
        notify(`规则执行完成，新增 ${result.newFacts || 0} 条事实`);
    } catch (e) {
        notify(e.message, 'error');
    } finally {
        hideLoading();
    }
}

// ============================================================================
// SPARQL Functions
// ============================================================================
function loadSparqlTemplate(type) {
    const editor = document.getElementById('sparql-query');
    switch (type) {
        case 'select':
            editor.value = `SELECT ?subject ?predicate ?object
WHERE {
  ?subject ?predicate ?object .
}
LIMIT 100`;
            break;
        case 'ask':
            editor.value = `ASK {
  ?subject a <http://example.org/Person> .
}`;
            break;
        case 'construct':
            editor.value = `CONSTRUCT {
  ?subject <http://example.org/relatedTo> ?object .
}
WHERE {
  ?subject ?predicate ?object .
}`;
            break;
    }
}

async function executeSparql() {
    const query = document.getElementById('sparql-query').value;
    if (!query.trim()) {
        notify('请输入 SPARQL 查询', 'warning');
        return;
    }

    try {
        showLoading();
        const startTime = Date.now();
        const result = await api('/api/sparql', { method: 'POST', body: { query } });
        const execTime = Date.now() - startTime;

        document.getElementById('sparql-exec-time').textContent = `执行时间: ${execTime}ms`;

        if (result.results && result.results.bindings) {
            const bindings = result.results.bindings;
            document.getElementById('sparql-result-count').textContent = `${bindings.length} 条结果`;
            renderSparqlResults(bindings);
        } else if (result.boolean !== undefined) {
            document.getElementById('sparql-result-count').textContent = '布尔结果';
            document.getElementById('sparql-results-table').innerHTML = `
                <div class="text-center" style="padding: 2rem">
                    <span class="stat-value">${result.boolean ? 'true' : 'false'}</span>
                </div>
            `;
        } else {
            document.getElementById('sparql-result-count').textContent = '查询完成';
            document.getElementById('sparql-results-table').innerHTML = '<pre>' + JSON.stringify(result, null, 2) + '</pre>';
        }

        notify('查询执行成功');
    } catch (e) {
        notify(e.message, 'error');
    } finally {
        hideLoading();
    }
}

function renderSparqlResults(bindings) {
    if (bindings.length === 0) {
        document.getElementById('sparql-results-table').innerHTML = '<div class="empty-state"><p>无结果</p></div>';
        return;
    }

    const vars = Object.keys(bindings[0]);
    const table = `
        <table class="triple-table">
            <thead>
                <tr>${vars.map(v => `<th>${v}</th>`).join('')}</tr>
            </thead>
            <tbody>
                ${bindings.map(b => `
                    <tr>${vars.map(v => `<td>${b[v]?.value || '-'}</td>`).join('')}</tr>
                `).join('')}
            </tbody>
        </table>
    `;
    document.getElementById('sparql-results-table').innerHTML = table;
}

// ============================================================================
// Semantic Search
// ============================================================================
async function semanticSearch() {
    const query = document.getElementById('semantic-query').value;
    const topK = parseInt(document.getElementById('search-topk').value);
    const classFilter = document.getElementById('search-class-filter').value;
    const threshold = parseFloat(document.getElementById('search-threshold').value);

    if (!query.trim()) {
        notify('请输入查询内容', 'warning');
        return;
    }

    try {
        showLoading();
        const result = await api('/api/search', {
            method: 'POST',
            body: { query, topK, classFilter, threshold }
        });

        renderSearchResults(result.results || result);
        notify('搜索完成');
    } catch (e) {
        notify(e.message, 'error');
    } finally {
        hideLoading();
    }
}

function renderSearchResults(results) {
    const container = document.getElementById('search-results');
    if (!results || results.length === 0) {
        container.innerHTML = '<div class="empty-state"><i class="fas fa-search"></i><p>无匹配结果</p></div>';
        return;
    }

    container.innerHTML = results.map(r => `
        <div class="entity-item" style="margin-bottom: 0.5rem">
            <div class="entity-info">
                <h4>${r.id || r.entity}</h4>
                <p>相似度: ${(r.score || r.similarity || 0).toFixed(3)} ${r.classId ? `| 类: ${r.classId}` : ''}</p>
            </div>
            <button class="btn-sm" onclick="viewEntity('${r.id || r.entity}')">
                <i class="fas fa-eye"></i> 查看
            </button>
        </div>
    `).join('');
}

// ============================================================================
// Embeddings Functions
// ============================================================================
async function loadEmbeddingInfo() {
    try {
        const info = await api('/api/embeddings/info');
        if (info) {
            document.getElementById('embedding-model').value = info.model || 'TransE';
            document.getElementById('embedding-dimension').value = info.dimension || 128;
        }
    } catch (e) {
        // Ignore if endpoint doesn't exist
    }
}

async function trainEmbeddings() {
    const model = document.getElementById('embedding-model').value;
    const dimension = parseInt(document.getElementById('embedding-dimension').value);

    try {
        showLoading();
        notify('开始训练嵌入，请稍候...');

        const result = await api('/api/embeddings/train', {
            method: 'POST',
            body: { model, dimension }
        });

        notify(`训练完成，损失: ${(result.finalLoss || 0).toFixed(4)}`);
    } catch (e) {
        notify(e.message, 'error');
    } finally {
        hideLoading();
    }
}

async function visualizeEmbeddings() {
    try {
        showLoading();
        const result = await api('/api/embeddings/visualize');

        // Simple 2D scatter plot placeholder
        const container = document.getElementById('embedding-visualization');
        container.innerHTML = `
            <div style="padding: 1rem">
                <p class="mb-3">向量空间可视化 (PCA 降维)</p>
                <canvas id="embedding-canvas" width="600" height="400" style="background: var(--bg); border-radius: 0.5rem"></canvas>
            </div>
        `;

        // If we had actual visualization data, we'd plot it here
        notify('可视化加载完成');
    } catch (e) {
        notify(e.message, 'error');
    } finally {
        hideLoading();
    }
}

// ============================================================================
// Storage Configuration
// ============================================================================
async function loadStorageConfig() {
    try {
        const config = await api('/api/storage/config');
        if (config.storage) {
            // Neo4j
            if (config.storage.neo4j) {
                document.getElementById('neo4j-enabled').checked = config.storage.neo4j.enabled || false;
                document.getElementById('neo4j-uri').value = config.storage.neo4j.uri || 'bolt://localhost:7687';
                document.getElementById('neo4j-username').value = config.storage.neo4j.username || 'neo4j';
                document.getElementById('neo4j-password').value = config.storage.neo4j.password || '';
                document.getElementById('neo4j-timeout').value = config.storage.neo4j.connectionTimeout || 30;
                document.getElementById('neo4j-pool').value = config.storage.neo4j.maxPoolSize || 100;
            }

            // Milvus
            if (config.storage.milvus) {
                document.getElementById('milvus-enabled').checked = config.storage.milvus.enabled || false;
                document.getElementById('milvus-host').value = config.storage.milvus.host || 'localhost';
                document.getElementById('milvus-port').value = config.storage.milvus.port || 19530;
                document.getElementById('milvus-collection').value = config.storage.milvus.collection || 'ontology_vectors';
                document.getElementById('milvus-dimension').value = config.storage.milvus.dimension || 128;
                document.getElementById('milvus-metric').value = config.storage.milvus.metric || 'cosine';
            }

            // Qdrant
            if (config.storage.qdrant) {
                document.getElementById('qdrant-enabled').checked = config.storage.qdrant.enabled || false;
                document.getElementById('qdrant-host').value = config.storage.qdrant.host || 'localhost';
                document.getElementById('qdrant-port').value = config.storage.qdrant.port || 6333;
                document.getElementById('qdrant-collection').value = config.storage.qdrant.collection || 'ontology_vectors';
            }

            // StellarDB
            if (config.storage.stellardb) {
                document.getElementById('stellardb-enabled').checked = config.storage.stellardb.enabled || false;
                document.getElementById('stellardb-host').value = config.storage.stellardb.host || 'localhost';
                document.getElementById('stellardb-port').value = config.storage.stellardb.port || 8182;
                document.getElementById('stellardb-graph-name').value = config.storage.stellardb.graphName || 'ontology';
                document.getElementById('stellardb-username').value = config.storage.stellardb.username || '';
                document.getElementById('stellardb-password').value = config.storage.stellardb.password || '';
                document.getElementById('stellardb-token').value = config.storage.stellardb.token || '';
                document.getElementById('stellardb-use-https').checked = config.storage.stellardb.useHttps || false;
            }

            // Hippo
            if (config.storage.hippo) {
                document.getElementById('hippo-enabled').checked = config.storage.hippo.enabled || false;
                document.getElementById('hippo-host').value = config.storage.hippo.host || 'localhost';
                document.getElementById('hippo-port').value = config.storage.hippo.port || 9200;
                document.getElementById('hippo-username').value = config.storage.hippo.username || '';
                document.getElementById('hippo-password').value = config.storage.hippo.password || '';
                document.getElementById('hippo-token').value = config.storage.hippo.token || '';
                document.getElementById('hippo-conn-timeout').value = config.storage.hippo.connectionTimeout || 30;
                document.getElementById('hippo-search-timeout').value = config.storage.hippo.searchTimeout || 60;
                document.getElementById('hippo-pool').value = config.storage.hippo.maxPoolSize || 10;
                document.getElementById('hippo-use-https').checked = config.storage.hippo.useHttps || false;
                document.getElementById('hippo-enable-ssl').checked = config.storage.hippo.enableSsl || false;
                document.getElementById('hippo-ssl-cert-path').value = config.storage.hippo.sslCertPath || '';
            }
        }
    } catch (e) {
        console.error('Config load error:', e);
    }
}

async function testConnection(service) {
    const statusEl = document.getElementById(`${service}-conn-status`);
    statusEl.textContent = '测试中...';
    statusEl.className = 'status-badge';

    try {
        const result = await api(`/api/storage/${service}/status`);
        if (result.connected) {
            statusEl.textContent = '已连接';
            statusEl.classList.add('connected');
            notify(`${service} 连接成功`);
        } else {
            statusEl.textContent = '连接失败';
            statusEl.classList.add('disconnected');
            notify(`${service} 连接失败`, 'error');
        }
    } catch (e) {
        statusEl.textContent = '连接失败';
        statusEl.classList.add('disconnected');
        notify(e.message, 'error');
    }
}

async function saveStorageConfig() {
    const config = {
        storage: {
            neo4j: {
                enabled: document.getElementById('neo4j-enabled').checked,
                uri: document.getElementById('neo4j-uri').value,
                username: document.getElementById('neo4j-username').value,
                password: document.getElementById('neo4j-password').value,
                connectionTimeout: parseInt(document.getElementById('neo4j-timeout').value),
                maxPoolSize: parseInt(document.getElementById('neo4j-pool').value)
            },
            milvus: {
                enabled: document.getElementById('milvus-enabled').checked,
                host: document.getElementById('milvus-host').value,
                port: parseInt(document.getElementById('milvus-port').value),
                collection: document.getElementById('milvus-collection').value,
                dimension: parseInt(document.getElementById('milvus-dimension').value),
                metric: document.getElementById('milvus-metric').value
            },
            qdrant: {
                enabled: document.getElementById('qdrant-enabled').checked,
                host: document.getElementById('qdrant-host').value,
                port: parseInt(document.getElementById('qdrant-port').value),
                collection: document.getElementById('qdrant-collection').value
            },
            stellardb: {
                enabled: document.getElementById('stellardb-enabled').checked,
                host: document.getElementById('stellardb-host').value,
                port: parseInt(document.getElementById('stellardb-port').value),
                graphName: document.getElementById('stellardb-graph-name').value,
                username: document.getElementById('stellardb-username').value,
                password: document.getElementById('stellardb-password').value,
                token: document.getElementById('stellardb-token').value,
                useHttps: document.getElementById('stellardb-use-https').checked
            },
            hippo: {
                enabled: document.getElementById('hippo-enabled').checked,
                host: document.getElementById('hippo-host').value,
                port: parseInt(document.getElementById('hippo-port').value),
                username: document.getElementById('hippo-username').value,
                password: document.getElementById('hippo-password').value,
                token: document.getElementById('hippo-token').value,
                useHttps: document.getElementById('hippo-use-https').checked,
                enableSsl: document.getElementById('hippo-enable-ssl').checked,
                sslCertPath: document.getElementById('hippo-ssl-cert-path').value,
                connectionTimeout: parseInt(document.getElementById('hippo-conn-timeout').value),
                searchTimeout: parseInt(document.getElementById('hippo-search-timeout').value),
                maxPoolSize: parseInt(document.getElementById('hippo-pool').value)
            }
        }
    };

    try {
        await api('/api/storage/config', { method: 'POST', body: config });
        notify('配置已保存');
    } catch (e) {
        notify(e.message, 'error');
    }
}

async function syncToExternalStorage() {
    try {
        showLoading();
        notify('正在同步到外部存储...');
        await api('/api/storage/sync', { method: 'POST' });
        notify('同步完成');
    } catch (e) {
        notify(e.message, 'error');
    } finally {
        hideLoading();
    }
}

// ============================================================================
// System Settings
// ============================================================================
async function loadSettings() {
    try {
        const config = await api('/api/config');
        if (config.server) {
            document.getElementById('http-port').value = config.server.port || 8080;
            document.getElementById('mcp-port').value = config.server.mcpPort || 9090;
            document.getElementById('max-connections').value = config.server.maxConnections || 100;
        }
        if (config.reasoner) {
            document.getElementById('default-depth').value = config.reasoner.symbolic?.maxInferenceDepth || 5;
        }
        if (config.cache) {
            document.getElementById('cache-ttl').value = config.cache.ttl || 3600;
        }
    } catch (e) {
        console.error('Settings load error:', e);
    }
}

async function saveSettings() {
    const config = {
        server: {
            port: parseInt(document.getElementById('http-port').value),
            mcpPort: parseInt(document.getElementById('mcp-port').value),
            maxConnections: parseInt(document.getElementById('max-connections').value)
        },
        reasoner: {
            symbolic: {
                maxInferenceDepth: parseInt(document.getElementById('default-depth').value)
            }
        },
        cache: {
            ttl: parseInt(document.getElementById('cache-ttl').value)
        }
    };

    try {
        await api('/api/config', { method: 'POST', body: config });
        notify('设置已保存');
    } catch (e) {
        notify(e.message, 'error');
    }
}

// ============================================================================
// Performance Monitor
// ============================================================================
async function loadMonitorData() {
    try {
        const metrics = await api('/api/monitor/metrics');
        if (metrics) {
            document.getElementById('monitor-requests').textContent = metrics.totalRequests || 0;
            document.getElementById('monitor-latency').textContent = `${metrics.avgLatency || 0}ms`;
            document.getElementById('monitor-cache-hit').textContent = `${metrics.cacheHitRate || 0}%`;
            document.getElementById('monitor-memory').textContent = `${metrics.memoryUsage || 0}MB`;

            // Render operation stats
            const tbody = document.getElementById('monitor-tbody');
            const ops = metrics.operations || [];
            tbody.innerHTML = ops.map(op => `
                <tr>
                    <td>${op.name}</td>
                    <td>${op.count}</td>
                    <td>${op.avgTime.toFixed(2)}ms</td>
                    <td>${op.minTime.toFixed(2)}ms</td>
                    <td>${op.maxTime.toFixed(2)}ms</td>
                </tr>
            `).join('') || '<tr><td colspan="5" class="text-center text-muted">暂无数据</td></tr>';
        }
    } catch (e) {
        console.error('Monitor load error:', e);
        // Set defaults
        document.getElementById('monitor-requests').textContent = '0';
        document.getElementById('monitor-latency').textContent = '0ms';
        document.getElementById('monitor-cache-hit').textContent = '0%';
        document.getElementById('monitor-memory').textContent = '0MB';
    }
}

function refreshMonitor() {
    loadMonitorData();
    notify('监控数据已刷新');
}

// ============================================================================
// Modal System
// ============================================================================
function showModal(title, bodyHtml, onSubmit) {
    document.getElementById('modal-title').textContent = title;
    document.getElementById('modal-body').innerHTML = bodyHtml;
    document.getElementById('modal').classList.add('active');

    document.getElementById('modal-submit').onclick = async () => {
        try {
            await onSubmit();
            closeModal();
            notify('操作成功');
        } catch (e) {
            notify(e.message, 'error');
        }
    };
}

function closeModal() {
    document.getElementById('modal').classList.remove('active');
}

// Close modal on background click
document.getElementById('modal').addEventListener('click', (e) => {
    if (e.target.id === 'modal') closeModal();
});

// ============================================================================
// Create Modals
// ============================================================================
function showCreateClassModal() {
    showModal('新建类', `
        <div class="form-group">
            <label>ID *</label>
            <input type="text" id="class-id" required placeholder="例如: Person">
        </div>
        <div class="form-group">
            <label>名称</label>
            <input type="text" id="class-name" placeholder="例如: 人员">
        </div>
        <div class="form-group">
            <label>描述</label>
            <textarea id="class-desc" rows="3"></textarea>
        </div>
        <div class="form-group">
            <label>父类 (逗号分隔)</label>
            <input type="text" id="class-super" placeholder="例如: Agent, Entity">
        </div>
    `, async () => {
        const data = {
            id: document.getElementById('class-id').value,
            name: document.getElementById('class-name').value,
            description: document.getElementById('class-desc').value,
            superClasses: document.getElementById('class-super').value.split(',').map(s => s.trim()).filter(s => s)
        };
        await api('/api/classes', { method: 'POST', body: data });
        loadTabData('classes');
    });
}

function showCreateRelationModal() {
    showModal('新建关系', `
        <div class="form-group">
            <label>ID *</label>
            <input type="text" id="rel-id" required placeholder="例如: manages">
        </div>
        <div class="form-group">
            <label>名称</label>
            <input type="text" id="rel-name" placeholder="例如: 管理">
        </div>
        <div class="form-row">
            <div class="form-group">
                <label>定义域</label>
                <input type="text" id="rel-domain" placeholder="例如: Person">
            </div>
            <div class="form-group">
                <label>值域</label>
                <input type="text" id="rel-range" placeholder="例如: Department">
            </div>
        </div>
        <div class="form-group">
            <label>特性</label>
            <div style="display: flex; gap: 1rem; margin-top: 0.5rem">
                <label class="checkbox-label"><input type="checkbox" id="rel-transitive"> 传递</label>
                <label class="checkbox-label"><input type="checkbox" id="rel-symmetric"> 对称</label>
                <label class="checkbox-label"><input type="checkbox" id="rel-reflexive"> 自反</label>
            </div>
        </div>
    `, async () => {
        const data = {
            id: document.getElementById('rel-id').value,
            name: document.getElementById('rel-name').value,
            domain: document.getElementById('rel-domain').value,
            range: document.getElementById('rel-range').value,
            transitive: document.getElementById('rel-transitive').checked,
            symmetric: document.getElementById('rel-symmetric').checked,
            reflexive: document.getElementById('rel-reflexive').checked
        };
        await api('/api/relations', { method: 'POST', body: data });
        loadTabData('relations');
    });
}

function showCreateIndividualModal() {
    showModal('新建实例', `
        <div class="form-group">
            <label>ID *</label>
            <input type="text" id="ind-id" required placeholder="例如: alice">
        </div>
        <div class="form-group">
            <label>名称</label>
            <input type="text" id="ind-name" placeholder="例如: Alice">
        </div>
        <div class="form-group">
            <label>类ID *</label>
            <input type="text" id="ind-class" required placeholder="例如: Person">
        </div>
        <div class="form-group">
            <label>属性 (JSON)</label>
            <textarea id="ind-props" rows="3" placeholder='{"age": 30, "department": "Engineering"}'></textarea>
        </div>
    `, async () => {
        const data = {
            id: document.getElementById('ind-id').value,
            name: document.getElementById('ind-name').value,
            classId: document.getElementById('ind-class').value
        };
        const propsStr = document.getElementById('ind-props').value;
        if (propsStr) {
            try {
                data.properties = JSON.parse(propsStr);
            } catch (e) {
                // Ignore parse error
            }
        }
        await api('/api/individuals', { method: 'POST', body: data });
        loadTabData('individuals');
    });
}

function showCreateTripleModal() {
    showModal('新建三元组', `
        <div class="form-group">
            <label>主语 *</label>
            <input type="text" id="triple-s" required placeholder="实体ID">
        </div>
        <div class="form-group">
            <label>谓词 *</label>
            <input type="text" id="triple-p" required placeholder="关系ID">
        </div>
        <div class="form-group">
            <label>宾语 *</label>
            <input type="text" id="triple-o" required placeholder="实体ID 或 字面值">
        </div>
        <div class="form-row">
            <div class="form-group">
                <label>置信度</label>
                <input type="number" id="triple-conf" value="1" min="0" max="1" step="0.1">
            </div>
        </div>
    `, async () => {
        const data = {
            subject: document.getElementById('triple-s').value,
            predicate: document.getElementById('triple-p').value,
            object: document.getElementById('triple-o').value,
            confidence: parseFloat(document.getElementById('triple-conf').value)
        };
        await api('/api/triples', { method: 'POST', body: data });
        loadTriples();
    });
}

function showCreateRuleModal() {
    showModal('新建规则', `
        <div class="form-group">
            <label>规则名称</label>
            <input type="text" id="rule-name" placeholder="例如: TransitiveManager">
        </div>
        <div class="form-group">
            <label>规则定义 (SWRL)</label>
            <textarea id="rule-body" rows="4" placeholder="Person(?p1) ^ manages(?p1, ?p2) ^ manages(?p2, ?p3) -> manages(?p1, ?p3)"></textarea>
        </div>
    `, async () => {
        const data = {
            name: document.getElementById('rule-name').value,
            body: document.getElementById('rule-body').value
        };
        await api('/api/rules', { method: 'POST', body: data });
        loadRules();
    });
}

function showImportModal() {
    showModal('导入本体', `
        <div class="form-group">
            <label>数据格式</label>
            <select id="import-format">
                <option value="json">JSON</option>
                <option value="turtle">Turtle (RDF)</option>
                <option value="nt">N-Triples</option>
            </select>
        </div>
        <div class="form-group">
            <label>数据内容</label>
            <textarea id="import-data" rows="8" placeholder="粘贴本体数据..."></textarea>
        </div>
    `, async () => {
        const format = document.getElementById('import-format').value;
        const data = document.getElementById('import-data').value;
        await api('/api/import', { method: 'POST', body: { format, data } });
        loadDashboard();
    });
}

function showQuickAddModal() {
    showModal('快速添加', `
        <div class="form-group">
            <label>选择类型</label>
            <select id="quick-type">
                <option value="class">类</option>
                <option value="relation">关系</option>
                <option value="individual">实例</option>
                <option value="triple">三元组</option>
            </select>
        </div>
        <div class="form-group">
            <label>输入数据 (JSON)</label>
            <textarea id="quick-data" rows="5"></textarea>
        </div>
    `, async () => {
        const type = document.getElementById('quick-type').value;
        const data = JSON.parse(document.getElementById('quick-data').value);
        await api(`/api/${type}s`, { method: 'POST', body: data });
        loadDashboard();
    });
}

// ============================================================================
// Entity Operations
// ============================================================================
async function deleteEntity(type, id) {
    if (!confirm(`确定删除 ${type} "${id}"?`)) return;

    try {
        await api(`/api/${type}s/${encodeURIComponent(id)}`, { method: 'DELETE' });
        notify('删除成功');

        // Reload appropriate data
        if (type === 'class') loadTabData('classes');
        else if (type === 'relation') loadTabData('relations');
        else if (type === 'individual') loadTabData('individuals');
    } catch (e) {
        notify(e.message, 'error');
    }
}

async function deleteTriple(s, p, o) {
    if (!confirm(`确定删除三元组 "${s} ${p} ${o}"?`)) return;

    try {
        await api('/api/triples', {
            method: 'DELETE',
            body: { subject: s, predicate: p, object: o }
        });
        notify('删除成功');
        loadTriples();
    } catch (e) {
        notify(e.message, 'error');
    }
}

function editClass(id) {
    // Load class data and show edit modal
    api(`/api/classes/${encodeURIComponent(id)}`).then(cls => {
        showModal('编辑类', `
            <div class="form-group">
                <label>ID</label>
                <input type="text" value="${cls.id}" disabled>
            </div>
            <div class="form-group">
                <label>名称</label>
                <input type="text" id="class-name" value="${cls.name || ''}">
            </div>
            <div class="form-group">
                <label>描述</label>
                <textarea id="class-desc" rows="3">${cls.description || ''}</textarea>
            </div>
            <div class="form-group">
                <label>父类 (逗号分隔)</label>
                <input type="text" id="class-super" value="${(cls.superClasses || []).join(', ')}">
            </div>
        `, async () => {
            const data = {
                name: document.getElementById('class-name').value,
                description: document.getElementById('class-desc').value,
                superClasses: document.getElementById('class-super').value.split(',').map(s => s.trim()).filter(s => s)
            };
            await api(`/api/classes/${encodeURIComponent(id)}`, { method: 'PUT', body: data });
            loadTabData('classes');
        });
    });
}

// ============================================================================
// Utility Functions
// ============================================================================
function refreshData() {
    const activePage = document.querySelector('.nav-item.active').dataset.page;
    loadPageData(activePage);
    notify('数据已刷新');
}

async function exportOntology() {
    try {
        showLoading();
        const data = await api('/api/export');
        const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `ontology_${new Date().toISOString().slice(0,10)}.json`;
        a.click();
        URL.revokeObjectURL(url);
        notify('导出成功');
    } catch (e) {
        notify(e.message, 'error');
    } finally {
        hideLoading();
    }
}

async function checkConsistency() {
    try {
        showLoading();
        const result = await api('/api/consistency/check');
        if (result.valid) {
            notify('本体一致性检查通过');
        } else {
            notify(`发现 ${result.violations?.length || 0} 个一致性问题`, 'warning');
        }
    } catch (e) {
        notify(e.message, 'error');
    } finally {
        hideLoading();
    }
}

function expandAll() {
    document.querySelectorAll('.hierarchy-node').forEach(node => {
        node.style.display = 'block';
    });
}

function collapseAll() {
    document.querySelectorAll('.hierarchy-node').forEach(node => {
        node.style.display = 'none';
    });
}

function exportResults() {
    const results = document.getElementById('infer-results');
    const text = results.innerText;
    const blob = new Blob([text], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'inference_results.txt';
    a.click();
    URL.revokeObjectURL(url);
}

function viewEntity(id) {
    showModal('实体详情', `
        <div class="form-group">
            <label>ID</label>
            <input type="text" value="${id}" disabled>
        </div>
        <div id="entity-details">
            <p>加载中...</p>
        </div>
    `, () => {
        // Just close
    });

    // Load entity details
    api(`/api/individuals/${encodeURIComponent(id)}`).then(entity => {
        document.getElementById('entity-details').innerHTML = `
            <p><strong>名称:</strong> ${entity.name || '-'}</p>
            <p><strong>类:</strong> ${entity.classId || '-'}</p>
            <p><strong>属性:</strong></p>
            <pre>${JSON.stringify(entity.properties || {}, null, 2)}</pre>
        `;
    }).catch(e => {
        document.getElementById('entity-details').innerHTML = `<p class="text-muted">无法加载详情</p>`;
    });
}

// ============================================================================
// Navigation Helper
// ============================================================================
function navigateTo(page) {
    document.querySelectorAll('.nav-item').forEach(item => {
        item.classList.toggle('active', item.dataset.page === page);
    });
    document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
    document.getElementById(`page-${page}`).classList.add('active');

    const navItem = document.querySelector(`.nav-item[data-page="${page}"]`);
    if (navItem) {
        document.getElementById('page-title').textContent = navItem.querySelector('span').textContent;
    }
    loadPageData(page);
}

// ============================================================================
// Knowledge Graph Visualization
// ============================================================================
let graphData = { nodes: [], edges: [] };
let graphCanvas, graphCtx;
let graphTransform = { x: 0, y: 0, scale: 1 };

async function loadKnowledgeGraph() {
    try {
        const data = await api('/api/graph?depth=2');
        graphData = data;
        renderKnowledgeGraph();
        updateGraphStats();
    } catch (e) {
        // Generate sample data for demo
        graphData = generateSampleGraph();
        renderKnowledgeGraph();
        updateGraphStats();
    }
    initDragDropEditing();
}

function generateSampleGraph() {
    const nodes = [
        { id: 'Person', type: 'class', label: 'Person', x: 400, y: 100 },
        { id: 'Employee', type: 'class', label: 'Employee', x: 250, y: 200 },
        { id: 'Manager', type: 'class', label: 'Manager', x: 550, y: 200 },
        { id: 'Department', type: 'class', label: 'Department', x: 400, y: 350 },
        { id: 'alice', type: 'individual', label: 'Alice', x: 150, y: 300 },
        { id: 'bob', type: 'individual', label: 'Bob', x: 250, y: 350 },
        { id: 'charlie', type: 'individual', label: 'Charlie', x: 550, y: 300 },
        { id: 'engineering', type: 'individual', label: 'Engineering', x: 400, y: 450 },
    ];
    const edges = [
        { source: 'Employee', target: 'Person', label: 'isA' },
        { source: 'Manager', target: 'Person', label: 'isA' },
        { source: 'alice', target: 'Employee', label: 'instanceOf' },
        { source: 'bob', target: 'Employee', label: 'instanceOf' },
        { source: 'charlie', target: 'Manager', label: 'instanceOf' },
        { source: 'alice', target: 'bob', label: 'manages' },
        { source: 'bob', target: 'charlie', label: 'reportsTo' },
        { source: 'alice', target: 'engineering', label: 'worksIn' },
        { source: 'charlie', target: 'engineering', label: 'heads' },
    ];
    return { nodes, edges };
}

function renderKnowledgeGraph() {
    graphCanvas = document.getElementById('kg-canvas');
    if (!graphCanvas) return;

    graphCtx = graphCanvas.getContext('2d');
    const container = graphCanvas.parentElement;
    graphCanvas.width = container.clientWidth;
    graphCanvas.height = container.clientHeight;

    drawGraph();
    setupGraphInteraction();
}

function drawGraph() {
    if (!graphCtx) return;

    const ctx = graphCtx;
    const { nodes, edges } = graphData;

    ctx.clearRect(0, 0, graphCanvas.width, graphCanvas.height);
    ctx.save();
    ctx.translate(graphTransform.x, graphTransform.y);
    ctx.scale(graphTransform.scale, graphTransform.scale);

    // Draw edges
    edges.forEach(edge => {
        const source = nodes.find(n => n.id === edge.source);
        const target = nodes.find(n => n.id === edge.target);
        if (source && target) {
            ctx.beginPath();
            ctx.moveTo(source.x, source.y);
            ctx.lineTo(target.x, target.y);
            ctx.strokeStyle = 'rgba(255, 255, 255, 0.3)';
            ctx.lineWidth = 1.5;
            ctx.stroke();

            // Arrow
            const angle = Math.atan2(target.y - source.y, target.x - source.x);
            const arrowLen = 10;
            ctx.beginPath();
            ctx.moveTo(target.x - arrowLen * Math.cos(angle - Math.PI / 6),
                       target.y - arrowLen * Math.sin(angle - Math.PI / 6));
            ctx.lineTo(target.x, target.y);
            ctx.lineTo(target.x - arrowLen * Math.cos(angle + Math.PI / 6),
                       target.y - arrowLen * Math.sin(angle + Math.PI / 6));
            ctx.stroke();

            // Label
            const midX = (source.x + target.x) / 2;
            const midY = (source.y + target.y) / 2;
            ctx.fillStyle = 'rgba(255, 255, 255, 0.5)';
            ctx.font = '10px sans-serif';
            ctx.fillText(edge.label, midX + 5, midY);
        }
    });

    // Draw nodes
    nodes.forEach(node => {
        const radius = node.type === 'class' ? 25 : 18;
        const color = node.type === 'class' ? '#3b82f6' :
                      node.type === 'individual' ? '#10b981' : '#f59e0b';

        // Glow effect
        ctx.beginPath();
        ctx.arc(node.x, node.y, radius + 5, 0, Math.PI * 2);
        ctx.fillStyle = color.replace(')', ', 0.2)').replace('rgb', 'rgba');
        ctx.fill();

        // Node circle
        ctx.beginPath();
        ctx.arc(node.x, node.y, radius, 0, Math.PI * 2);
        ctx.fillStyle = color;
        ctx.fill();
        ctx.strokeStyle = 'white';
        ctx.lineWidth = 2;
        ctx.stroke();

        // Label
        ctx.fillStyle = 'white';
        ctx.font = node.type === 'class' ? 'bold 12px sans-serif' : '10px sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(node.label, node.x, node.y);
    });

    ctx.restore();
}

function setupGraphInteraction() {
    let isDragging = false;
    let lastX, lastY;

    graphCanvas.addEventListener('mousedown', (e) => {
        isDragging = true;
        lastX = e.clientX;
        lastY = e.clientY;
    });

    graphCanvas.addEventListener('mousemove', (e) => {
        if (isDragging) {
            graphTransform.x += e.clientX - lastX;
            graphTransform.y += e.clientY - lastY;
            lastX = e.clientX;
            lastY = e.clientY;
            drawGraph();
        }
    });

    graphCanvas.addEventListener('mouseup', () => isDragging = false);
    graphCanvas.addEventListener('mouseleave', () => isDragging = false);

    graphCanvas.addEventListener('wheel', (e) => {
        e.preventDefault();
        const delta = e.deltaY > 0 ? 0.9 : 1.1;
        graphTransform.scale *= delta;
        drawGraph();
    });
}

function updateGraphStats() {
    document.getElementById('kg-nodes').textContent = graphData.nodes.length;
    document.getElementById('kg-edges').textContent = graphData.edges.length;
    const avgDegree = graphData.nodes.length > 0 ?
        (graphData.edges.length * 2 / graphData.nodes.length).toFixed(1) : 0;
    document.getElementById('kg-avg-degree').textContent = avgDegree;
    document.getElementById('kg-components').textContent = 1; // Simplified
}

function zoomIn() {
    graphTransform.scale *= 1.2;
    drawGraph();
}

function zoomOut() {
    graphTransform.scale *= 0.8;
    drawGraph();
}

function fitToScreen() {
    graphTransform = { x: 50, y: 50, scale: 1 };
    drawGraph();
}

function resetGraphView() {
    graphTransform = { x: 0, y: 0, scale: 1 };
    drawGraph();
}

function changeGraphLayout() {
    // Re-layout nodes based on selected algorithm
    notify('布局已更新');
}

function exportGraphImage() {
    const link = document.createElement('a');
    link.download = 'knowledge-graph.png';
    link.href = graphCanvas.toDataURL();
    link.click();
    notify('图片已导出');
}

// ============================================================================
// Dashboard Graph Preview
// ============================================================================
function renderGraphPreview() {
    const canvas = document.getElementById('graph-canvas');
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    const container = canvas.parentElement;
    canvas.width = container.clientWidth;
    canvas.height = container.clientHeight;

    // Use sample data
    if (graphData.nodes.length === 0) {
        graphData = generateSampleGraph();
    }

    // Draw mini version
    const scale = Math.min(canvas.width / 800, canvas.height / 400) * 0.8;
    const offsetX = (canvas.width - 800 * scale) / 2;
    const offsetY = (canvas.height - 400 * scale) / 2;

    ctx.save();
    ctx.translate(offsetX, offsetY);
    ctx.scale(scale, scale);

    // Dark background
    ctx.fillStyle = '#1a1f2e';
    ctx.fillRect(0, 0, 800, 400);

    // Draw edges
    graphData.edges.forEach(edge => {
        const source = graphData.nodes.find(n => n.id === edge.source);
        const target = graphData.nodes.find(n => n.id === edge.target);
        if (source && target) {
            ctx.beginPath();
            ctx.moveTo(source.x, source.y);
            ctx.lineTo(target.x, target.y);
            ctx.strokeStyle = 'rgba(255, 255, 255, 0.2)';
            ctx.lineWidth = 1;
            ctx.stroke();
        }
    });

    // Draw nodes
    graphData.nodes.forEach(node => {
        const radius = node.type === 'class' ? 20 : 14;
        const color = node.type === 'class' ? '#3b82f6' :
                      node.type === 'individual' ? '#10b981' : '#f59e0b';

        ctx.beginPath();
        ctx.arc(node.x, node.y, radius, 0, Math.PI * 2);
        ctx.fillStyle = color;
        ctx.fill();

        ctx.fillStyle = 'white';
        ctx.font = '10px sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(node.label, node.x, node.y);
    });

    ctx.restore();
}

// ============================================================================
// Neural Network View
// ============================================================================
async function loadNeuralView() {
    renderEmbeddingSpace();
    renderTrainingCurves();
    renderRelationEmbeddings();
}

function renderEmbeddingSpace() {
    const canvas = document.getElementById('embedding-canvas');
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    const container = canvas.parentElement;
    canvas.width = container.clientWidth;
    canvas.height = container.clientHeight;

    // Generate sample 2D points (would be UMAP/PCA projection in real app)
    const points = [];
    for (let i = 0; i < 100; i++) {
        points.push({
            x: Math.random() * canvas.width,
            y: Math.random() * canvas.height,
            type: Math.random() > 0.7 ? 'class' : 'individual',
            label: `e${i}`
        });
    }

    // Draw points
    points.forEach(p => {
        const color = p.type === 'class' ? '#3b82f6' : '#10b981';
        ctx.beginPath();
        ctx.arc(p.x, p.y, 4, 0, Math.PI * 2);
        ctx.fillStyle = color;
        ctx.fill();
    });
}

function renderTrainingCurves() {
    const canvas = document.getElementById('loss-curve');
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    const container = canvas.parentElement;
    canvas.width = container.clientWidth;
    canvas.height = container.clientHeight;

    const width = canvas.width;
    const height = canvas.height;
    const padding = 30;

    // Generate sample loss curve
    const losses = [];
    for (let i = 0; i < 100; i++) {
        losses.push(0.5 * Math.exp(-i * 0.03) + 0.02 + Math.random() * 0.02);
    }

    // Draw axes
    ctx.strokeStyle = '#e2e8f0';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(padding, padding);
    ctx.lineTo(padding, height - padding);
    ctx.lineTo(width - padding, height - padding);
    ctx.stroke();

    // Draw loss curve
    ctx.beginPath();
    losses.forEach((loss, i) => {
        const x = padding + (i / (losses.length - 1)) * (width - 2 * padding);
        const y = height - padding - (loss / 0.5) * (height - 2 * padding);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    });
    ctx.strokeStyle = '#3b82f6';
    ctx.lineWidth = 2;
    ctx.stroke();

    // Labels
    ctx.fillStyle = '#64748b';
    ctx.font = '10px sans-serif';
    ctx.fillText('Loss', padding + 5, padding + 10);
    ctx.fillText('Epoch', width - padding - 30, height - padding + 15);
}

function renderRelationEmbeddings() {
    const container = document.getElementById('relation-embeddings');
    if (!container) return;

    const relations = ['manages', 'reportsTo', 'worksIn', 'heads', 'isA', 'instanceOf'];

    container.innerHTML = relations.map(r => `
        <div class="relation-viz-item">
            <div class="relation-name">${r}</div>
            <div class="relation-vector" style="background: linear-gradient(90deg, #3b82f6 ${Math.random()*100}%, #10b981 ${Math.random()*100}%)"></div>
        </div>
    `).join('');
}

async function predictTail() {
    const head = document.getElementById('link-head').value;
    const relation = document.getElementById('link-relation').value;

    if (!head || !relation) {
        notify('请输入头实体和关系', 'warning');
        return;
    }

    try {
        showLoading();
        const results = await api('/api/embeddings/predict', 'POST', { head, relation });

        document.getElementById('link-prediction-results').innerHTML = `
            <h5 class="mb-2">预测结果</h5>
            ${results.map((r, i) => `
                <div class="prediction-item" style="display: flex; justify-content: space-between; padding: 0.375rem; background: ${i === 0 ? 'rgba(16, 185, 129, 0.1)' : 'var(--bg)'}; border-radius: 0.25rem; margin-bottom: 0.25rem">
                    <span>${r.entity}</span>
                    <span class="text-muted">${(r.score * 100).toFixed(1)}%</span>
                </div>
            `).join('')}
        `;
    } catch (e) {
        notify(e.message, 'error');
    } finally {
        hideLoading();
    }
}

function refreshNeuralView() {
    loadNeuralView();
    notify('神经网络视图已刷新');
}

// ============================================================================
// Agent Collaboration
// ============================================================================
async function loadCollaboration() {
    try {
        // 获取智能体列表
        const agents = await api('/api/agents');
        renderAgentList(agents);

        // 更新首页仪表盘的智能体列表
        renderDashboardAgents(agents);

        // 更新协作架构图
        renderCollabDiagram(agents);

        // 获取活动日志
        const activities = await api('/api/agents/activities?limit=20');
        renderActivityLog(activities);

        // 获取工具统计
        const toolStats = await api('/api/agents/tools/stats');
        renderToolStats(toolStats);

        // 获取总览
        const summary = await api('/api/agents/summary');
        document.getElementById('connected-agent-count').textContent = summary.onlineAgents || 0;
        document.getElementById('agent-count').textContent = summary.totalAgents || 0;
    } catch (e) {
        console.error('Collaboration load error:', e);
    }
}

function renderDashboardAgents(agents) {
    const container = document.getElementById('dashboard-agent-list');
    if (!container) return;

    if (!agents || agents.length === 0) {
        container.innerHTML = `
            <div class="agent-item" style="color:#888;text-align:center;padding:20px;">
                <i class="fas fa-robot" style="font-size:24px;margin-bottom:8px;display:block;"></i>
                尚无已连接智能体
            </div>`;
        return;
    }

    container.innerHTML = agents.map(agent => `
        <div class="agent-item">
            <div class="agent-avatar ${agent.type || ''}">
                <i class="fas fa-robot"></i>
            </div>
            <div class="agent-info">
                <div class="agent-name">${agent.name}</div>
                <div class="agent-status ${agent.online ? 'online' : 'offline'}">
                    <span class="status-dot"></span> ${agent.online ? '在线' : '离线'}
                </div>
            </div>
            <div class="agent-stats">
                <span>${agent.queryCount || 0} 查询</span>
            </div>
        </div>
    `).join('');
}

function renderAgentList(agents) {
    const container = document.getElementById('agent-list');
    if (!container || !agents || agents.length === 0) return;

    container.innerHTML = agents.map(agent => `
        <div class="agent-card">
            <div class="agent-card-header">
                <div class="agent-avatar large ${agent.type}">
                    <i class="fas fa-robot"></i>
                </div>
                <div class="agent-card-info">
                    <h4>${agent.name}</h4>
                    <span class="agent-version">${agent.version || agent.type}</span>
                </div>
                <span class="status-badge ${agent.online ? 'connected' : ''}">${agent.online ? '在线' : '离线'}</span>
            </div>
            <div class="agent-card-body">
                <div class="agent-metrics">
                    <div class="metric">
                        <span class="metric-value">${agent.queryCount || 0}</span>
                        <span class="metric-label">查询次数</span>
                    </div>
                    <div class="metric">
                        <span class="metric-value">${agent.avgLatencyMs || 0}ms</span>
                        <span class="metric-label">平均延迟</span>
                    </div>
                    <div class="metric">
                        <span class="metric-value">${(agent.successRate || 0).toFixed(1)}%</span>
                        <span class="metric-label">成功率</span>
                    </div>
                </div>
                <div class="agent-tools">
                    ${(agent.registeredTools || []).map(t => `<span class="tool-badge">${t}</span>`).join('')}
                </div>
            </div>
            <div class="agent-card-footer">
                <span class="last-activity">最后活动: ${formatTime(agent.lastActivityTime)}</span>
                <button class="btn-sm" onclick="viewAgentLogs('${agent.agentId}')">
                    <i class="fas fa-history"></i> 日志
                </button>
            </div>
        </div>
    `).join('');
}

function renderCollabDiagram(agents) {
    const nodesG = document.getElementById('svg-agent-nodes');
    const statsText = document.getElementById('backend-stats-text');
    if (!nodesG) return;

    const onlineCount = (agents || []).filter(a => a.online).length;
    if (statsText) statsText.textContent = `${onlineCount} 在线智能体`;

    if (!agents || agents.length === 0) {
        nodesG.innerHTML = '<text x="400" y="340" text-anchor="middle" class="agent-label" fill="#888">暂无连接的智能体</text>';
        return;
    }

    // Position agents in a circle around the center (400, 200)
    const n = agents.length;
    const radius = 220;
    const typeClasses = { claude: 'claude', openai: 'gpt', local: 'local', custom: 'custom' };
    let svg = '';

    agents.forEach((agent, i) => {
        const angle = (2 * Math.PI * i / n) - Math.PI / 2;
        const cx = 400 + radius * Math.cos(angle);
        const cy = 200 + radius * Math.sin(angle);
        const tc = typeClasses[agent.type] || 'custom';
        const shortName = agent.name.length > 8 ? agent.name.substring(0, 7) + '…' : agent.name;
        const statusDot = agent.online ? '● 在线' : '○ 离线';
        const statusClass = agent.online ? 'online' : 'offline';
        const lineClass = agent.online ? 'active' : '';

        svg += `<line x1="${cx}" y1="${cy}" x2="400" y2="200" class="connection-line ${lineClass}"/>`;
        svg += `<g class="agent-node" transform="translate(${cx}, ${cy})">`;
        svg += `<circle r="40" class="agent-circle ${tc}"/>`;
        svg += `<text class="agent-label" text-anchor="middle" dy="-5">${shortName}</text>`;
        svg += `<text class="agent-status ${statusClass}" text-anchor="middle" dy="12">${statusDot}</text>`;
        svg += `</g>`;
        const labelX = (cx + 400) / 2;
        const labelY = (cy + 200) / 2;
        svg += `<text class="protocol-label" x="${labelX}" y="${labelY}">MCP</text>`;
    });

    nodesG.innerHTML = svg;
}

function renderActivityLog(activities) {
    const container = document.getElementById('collab-activity-log');
    if (!container || !activities || activities.length === 0) return;

    container.innerHTML = activities.map(act => {
        const paramsStr = formatParams(act.params);
        const resultStr = formatResult(act.result);

        return `
            <div class="log-entry">
                <div class="log-time">${formatTime(act.timestamp, true)}</div>
                <div class="log-agent ${getAgentClass(act.agentId)}">${getAgentName(act.agentId)}</div>
                <div class="log-action">
                    <span class="log-tool">${act.tool}</span>
                    <span class="log-params">${paramsStr}</span>
                </div>
                <div class="log-result ${act.success ? 'success' : 'error'}">${resultStr}</div>
            </div>
        `;
    }).join('');
}

function renderToolStats(toolStats) {
    const container = document.querySelector('.tools-stats-grid');
    if (!container || !toolStats || toolStats.length === 0) return;

    const maxCount = Math.max(...toolStats.map(t => t.callCount), 1);

    container.innerHTML = toolStats.map(stat => `
        <div class="tool-stat-item">
            <div class="tool-name">${stat.toolName}</div>
            <div class="tool-bar-container">
                <div class="tool-bar" style="width: ${(stat.callCount / maxCount) * 100}%"></div>
            </div>
            <div class="tool-count">${stat.callCount} 次</div>
        </div>
    `).join('');
}

function formatTime(timestamp, withTime = false) {
    if (!timestamp) return '-';

    const date = new Date(timestamp);
    if (withTime) {
        return date.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    }

    const now = Date.now();
    const diff = now - timestamp;

    if (diff < 60000) return '刚刚';
    if (diff < 3600000) return Math.floor(diff / 60000) + '分钟前';
    if (diff < 86400000) return Math.floor(diff / 3600000) + '小时前';
    return date.toLocaleDateString('zh-CN');
}

function formatParams(params) {
    if (!params || Object.keys(params).length === 0) return '';

    const keys = Object.keys(params).slice(0, 2);
    return keys.map(k => `${k}: ${JSON.stringify(params[k]).slice(0, 20)}`).join(', ');
}

function formatResult(result) {
    if (!result) return '完成';

    if (result.count !== undefined) return `${result.count} 结果`;
    if (result.newFacts !== undefined) return `${result.newFacts} 新事实`;
    if (result.created !== undefined) return '已创建';
    if (result.error) return result.error;

    return '完成';
}

function getAgentClass(agentId) {
    if (agentId.includes('claude')) return 'claude';
    if (agentId.includes('gpt')) return 'gpt';
    return '';
}

function getAgentName(agentId) {
    if (agentId.includes('claude')) return 'Claude';
    if (agentId.includes('gpt')) return 'GPT-4';
    if (agentId.includes('llama')) return 'LLaMA';
    return agentId;
}

function registerNewAgent() {
    showModal('注册智能体', `
        <div class="form-group">
            <label>智能体ID</label>
            <input type="text" id="agent-id" placeholder="例如: my-agent-001">
        </div>
        <div class="form-group">
            <label>智能体名称</label>
            <input type="text" id="agent-name" placeholder="例如: MyAgent">
        </div>
        <div class="form-group">
            <label>类型</label>
            <select id="agent-type">
                <option value="claude">Claude</option>
                <option value="openai">OpenAI</option>
                <option value="local">本地模型</option>
                <option value="custom">自定义</option>
            </select>
        </div>
        <div class="form-group">
            <label>版本</label>
            <input type="text" id="agent-version" placeholder="例如: v1.0">
        </div>
        <div class="form-group">
            <label>API 端点</label>
            <input type="text" id="agent-endpoint" placeholder="http://localhost:8080/mcp">
        </div>
        <div class="form-group">
            <label>支持的工具 (逗号分隔)</label>
            <input type="text" id="agent-tools" placeholder="cognitive_query, cognitive_infer">
        </div>
    `, async () => {
        const toolsStr = document.getElementById('agent-tools').value;
        const tools = toolsStr ? toolsStr.split(',').map(t => t.trim()).filter(t => t) : [];

        const data = {
            agentId: document.getElementById('agent-id').value,
            name: document.getElementById('agent-name').value,
            type: document.getElementById('agent-type').value,
            version: document.getElementById('agent-version').value,
            endpoint: document.getElementById('agent-endpoint').value,
            tools: tools
        };

        await api('/api/agents/register', { method: 'POST', body: data });
        loadCollaboration();
        notify('智能体已注册');
    });
}

function viewAgentLogs(agentId) {
    showModal('智能体日志 - ' + agentId, `
        <div class="activity-log" style="max-height: 400px">
            <p class="text-muted" style="text-align: center; padding: 2rem">加载中...</p>
        </div>
    `, () => {});

    // 加载该智能体的日志
    api(`/api/agents/activities?limit=50&agentId=${agentId}`).then(activities => {
        const logContainer = document.querySelector('#modal-body .activity-log');
        if (activities && activities.length > 0) {
            logContainer.innerHTML = activities.map(act => `
                <div class="log-entry">
                    <div class="log-time">${formatTime(act.timestamp, true)}</div>
                    <div class="log-action">
                        <span class="log-tool">${act.tool}</span>
                        <span class="log-params">${formatParams(act.params)}</span>
                    </div>
                    <div class="log-result ${act.success ? 'success' : 'error'}">
                        ${formatResult(act.result)} (${act.latencyMs}ms)
                    </div>
                </div>
            `).join('');
        } else {
            logContainer.innerHTML = '<p class="text-muted" style="text-align: center; padding: 2rem">暂无活动记录</p>';
        }
    }).catch(e => {
        const logContainer = document.querySelector('#modal-body .activity-log');
        logContainer.innerHTML = '<p class="text-muted" style="text-align: center; padding: 2rem">加载失败</p>';
    });
}

// ============================================================================
// Update Page Data Loading
// ============================================================================
async function loadPageData(page) {
    try {
        switch (page) {
            case 'dashboard':
                await loadDashboard();
                renderGraphPreview();
                break;
            case 'ontology':
                await loadTabData('classes');
                populateClassDropdowns();
                break;
            case 'triples':
                await loadTriples();
                break;
            case 'reasoning':
                break;
            case 'rules':
                await loadRules();
                break;
            case 'sparql':
                break;
            case 'search':
                populateSearchDropdowns();
                break;
            case 'embeddings':
                await loadEmbeddingInfo();
                break;
            case 'knowledge-graph':
                await loadKnowledgeGraph();
                populateKGDropdowns();
                break;
            case 'neural-view':
                await loadNeuralView();
                break;
            case 'collaboration':
                await loadCollaboration();
                break;
            case 'storage':
                await loadStorageConfig();
                break;
            case 'settings':
                await loadSettings();
                break;
            case 'monitor':
                await loadMonitorData();
                break;
            case 'llm':
                await loadLLMConfig();
                await loadLocalModelConfig();
                break;
            case 'rag':
                await loadRagPage();
                break;
        }
    } catch (e) {
        console.error('Load page error:', e);
        notify(`加载失败: ${e.message}`, 'error');
    }
}

// ============================================================================
// Initialize
// ============================================================================
document.addEventListener('DOMContentLoaded', () => {
    loadPageData('dashboard');
    initWebSocket();

    // Hippo SSL toggle: show/hide cert path row
    const hippoSslCheckbox = document.getElementById('hippo-enable-ssl');
    if (hippoSslCheckbox) {
        hippoSslCheckbox.addEventListener('change', function() {
            const sslRow = document.getElementById('hippo-ssl-row');
            if (sslRow) sslRow.style.display = this.checked ? '' : 'none';
        });
    }
});

// ============================================================================
// WebSocket Real-time Updates
// ============================================================================
let ws = null;
let wsReconnectTimer = null;

function initWebSocket() {
    const wsPort = window.location.port ? parseInt(window.location.port) + 1 : 8081;
    const wsUrl = `ws://${window.location.hostname}:${wsPort}`;

    try {
        ws = new WebSocket(wsUrl);

        ws.onopen = () => {
            console.log('WebSocket connected');
            notify('实时连接已建立', 'success');

            // 更新连接状态
            const icon = document.getElementById('ws-status-icon');
            const text = document.getElementById('ws-status-text');
            if (icon) { icon.className = 'fas fa-circle text-success'; }
            if (text) { text.textContent = '实时已连接'; }

            // 订阅所有事件主题
            ws.send(JSON.stringify({ action: 'subscribe', topic: 'all' }));
            ws.send(JSON.stringify({ action: 'subscribe', topic: 'triple_add' }));
            ws.send(JSON.stringify({ action: 'subscribe', topic: 'entity_create' }));
            ws.send(JSON.stringify({ action: 'subscribe', topic: 'system_status' }));
        };

        ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                handleWsMessage(data);
            } catch (e) {
                console.warn('WebSocket message parse error:', e);
            }
        };

        ws.onclose = () => {
            console.log('WebSocket disconnected');

            // 更新连接状态
            const icon = document.getElementById('ws-status-icon');
            const text = document.getElementById('ws-status-text');
            if (icon) { icon.className = 'fas fa-circle text-warning'; }
            if (text) { text.textContent = '重连中...'; }

            // 自动重连
            wsReconnectTimer = setTimeout(initWebSocket, 5000);
        };

        ws.onerror = (err) => {
            console.warn('WebSocket error');
            ws.close();
        };
    } catch (e) {
        console.warn('WebSocket not available, using polling fallback');
        startPolling();
    }
}

function handleWsMessage(data) {
    const eventType = data.type;

    switch (eventType) {
        case 'connected':
            console.log('WebSocket session:', data.connectionId);
            break;

        case 'kg_overview':
            updateDashboardStats(data);
            break;

        case 'triple_add':
            notify(`新三元组: ${data.data.subject} → ${data.data.predicate} → ${data.data.object}`, 'info');
            refreshCurrentPage();
            break;

        case 'triple_remove':
            notify('三元组已删除', 'info');
            refreshCurrentPage();
            break;

        case 'entity_create':
            notify(`新${data.data.entityType}: ${data.data.id}`, 'info');
            refreshCurrentPage();
            break;

        case 'entity_update':
            notify(`${data.data.entityType} 已更新: ${data.data.id}`, 'info');
            refreshCurrentPage();
            break;

        case 'infer_complete':
            notify(`推理完成: ${data.data.count} 条结果`, 'success');
            break;

        case 'consistency_check':
            if (data.data.passed) {
                notify('一致性检查通过', 'success');
            } else {
                notify(`一致性违规: ${data.data.violations.length} 条`, 'error');
            }
            break;

        case 'embedding_progress':
            updateEmbeddingProgress(data.data);
            break;

        case 'system_status':
            notify(`系统: ${data.data.component} - ${data.data.status}`, 'info');
            break;

        case 'subscribed':
            console.log('Subscribed to:', data.topic);
            break;

        default:
            console.log('WS event:', eventType);
    }
}

function updateDashboardStats(data) {
    const statsEl = document.getElementById('stat-classes');
    if (statsEl) statsEl.textContent = data.classes || 0;
    const indEl = document.getElementById('stat-individuals');
    if (indEl) indEl.textContent = data.individuals || 0;
    const triEl = document.getElementById('stat-triples');
    if (triEl) triEl.textContent = data.triples || 0;
}

function updateEmbeddingProgress(data) {
    const lossEl = document.getElementById('training-loss');
    const trainedEl = document.getElementById('last-trained');
    if (lossEl && data.loss !== undefined) {
        lossEl.textContent = data.loss.toFixed(4);
    }
    if (trainedEl && data.epoch !== undefined) {
        trainedEl.textContent = `Epoch ${data.epoch}/${data.total || '?'}`;
    }
}

let refreshTimer = null;
function refreshCurrentPage() {
    // 防抖：500ms 内多次更新只刷新一次
    if (refreshTimer) clearTimeout(refreshTimer);
    refreshTimer = setTimeout(() => {
        const activePage = document.querySelector('.page.active');
        if (!activePage) return;
        const pageId = activePage.id.replace('page-', '');
        loadPageData(pageId);
    }, 500);
}

function startPolling() {
    // WebSocket 不可用时的降级方案
    setInterval(async () => {
        try {
            const health = await api('/api/health');
            updateDashboardStats(health.stats || {});
        } catch (e) {}
    }, 10000);
}

// ============================================================================
// 3D Embedding Visualization (Three.js)
// ============================================================================
async function visualize3DEmbeddings() {
    const container = document.getElementById('embedding-3d-container');
    if (!container) return;

    try {
        // 获取嵌入数据
        const response = await api('/api/search/embeddings?limit=500');
        const points = response.embeddings || [];

        if (points.length === 0) {
            container.innerHTML = '<p class="text-muted" style="text-align:center;padding:2rem">暂无嵌入数据，请先训练模型</p>';
            return;
        }

        // 使用 Canvas 2D 模拟 3D 投影 (无需 Three.js 依赖)
        render3DProjection(container, points);
    } catch (e) {
        container.innerHTML = `<p class="text-muted" style="text-align:center;padding:2rem">加载失败: ${e.message}</p>`;
    }
}

function render3DProjection(container, points) {
    const width = container.clientWidth || 800;
    const height = 500;

    // 创建 Canvas
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    canvas.style.background = '#0a0a1a';
    canvas.style.borderRadius = '8px';
    canvas.style.cursor = 'grab';
    container.innerHTML = '';
    container.appendChild(canvas);

    const ctx = canvas.getContext('2d');

    // 降维到 2D (简单 PCA 投影)
    let projected = simplePCA(points);

    // 缩放和偏移
    let scale = Math.min(width, height) / 4;
    let offsetX = width / 2;
    let offsetY = height / 2;
    let rotation = 0;
    let isDragging = false;
    let lastX = 0, lastY = 0;

    function draw() {
        ctx.clearRect(0, 0, width, height);

        // 绘制网格
        ctx.strokeStyle = 'rgba(50, 50, 80, 0.3)';
        ctx.lineWidth = 0.5;
        for (let x = 0; x < width; x += 50) {
            ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke();
        }
        for (let y = 0; y < height; y += 50) {
            ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke();
        }

        // 应用旋转
        const cos = Math.cos(rotation);
        const sin = Math.sin(rotation);

        // 绘制连接线 (同类的实体之间)
        ctx.strokeStyle = 'rgba(100, 150, 255, 0.08)';
        ctx.lineWidth = 0.5;
        for (let i = 0; i < projected.length; i++) {
            for (let j = i + 1; j < projected.length; j++) {
                if (projected[i].classId && projected[i].classId === projected[j].classId) {
                    const x1 = projected[i].x * cos - projected[i].y * sin + offsetX;
                    const y1 = projected[i].x * sin + projected[i].y * cos + offsetY;
                    const x2 = projected[j].x * cos - projected[j].y * sin + offsetX;
                    const y2 = projected[j].x * sin + projected[j].y * cos + offsetY;
                    ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();
                }
            }
        }

        // 绘制点
        for (const p of projected) {
            const rx = p.x * cos - p.y * sin;
            const ry = p.x * sin + p.y * cos;
            const px = rx * scale + offsetX;
            const py = ry * scale + offsetY;

            if (px < 0 || px > width || py < 0 || py > height) continue;

            const size = 3 + (p.confidence || 0.5) * 4;
            const alpha = 0.6 + (p.confidence || 0.5) * 0.4;

            // 按类分配颜色
            const hue = getClassHue(p.classId || 'default');

            ctx.beginPath();
            ctx.arc(px, py, size, 0, Math.PI * 2);
            ctx.fillStyle = `hsla(${hue}, 80%, 60%, ${alpha})`;
            ctx.fill();

            // 发光效果
            ctx.beginPath();
            ctx.arc(px, py, size + 2, 0, Math.PI * 2);
            ctx.fillStyle = `hsla(${hue}, 80%, 60%, ${alpha * 0.2})`;
            ctx.fill();

            // 标签
            if (scale > 60) {
                ctx.fillStyle = `hsla(${hue}, 70%, 80%, 0.8)`;
                ctx.font = '9px monospace';
                ctx.fillText(p.label || p.id, px + size + 3, py + 3);
            }
        }

        // 信息标签
        ctx.fillStyle = 'rgba(200, 200, 255, 0.7)';
        ctx.font = '12px monospace';
        ctx.fillText(`Entities: ${projected.length} | Rotation: ${(rotation * 180 / Math.PI).toFixed(1)}°`, 10, height - 10);
    }

    // 简单颜色映射
    const classHues = {};
    let nextHue = 0;
    function getClassHue(classId) {
        if (!classHues[classId]) {
            classHues[classId] = nextHue;
            nextHue = (nextHue + 47) % 360;
        }
        return classHues[classId];
    }

    // 交互
    canvas.addEventListener('mousedown', (e) => {
        isDragging = true;
        lastX = e.offsetX;
        lastY = e.offsetY;
        canvas.style.cursor = 'grabbing';
    });

    canvas.addEventListener('mousemove', (e) => {
        if (!isDragging) return;
        const dx = e.offsetX - lastX;
        rotation += dx * 0.005;
        offsetX += e.offsetX - lastX;
        offsetY += e.offsetY - lastY;
        lastX = e.offsetX;
        lastY = e.offsetY;
        draw();
    });

    canvas.addEventListener('mouseup', () => {
        isDragging = false;
        canvas.style.cursor = 'grab';
    });

    canvas.addEventListener('wheel', (e) => {
        e.preventDefault();
        scale *= e.deltaY > 0 ? 0.9 : 1.1;
        scale = Math.max(10, Math.min(500, scale));
        draw();
    });

    draw();
}

function simplePCA(points) {
    if (points.length === 0) return [];

    // 简单的 2D 投影 (取前两个维度)
    const projected = [];
    for (const p of points) {
        const vec = p.vector || p.embedding || [0, 0];
        projected.push({
            id: p.id || '',
            label: p.name || p.id || '',
            classId: p.classId || p.type || 'default',
            confidence: p.confidence || p.score || 0.5,
            x: vec.length > 0 ? vec[0] : (Math.random() - 0.5) * 2,
            y: vec.length > 1 ? vec[1] : (Math.random() - 0.5) * 2
        });
    }

    // 归一化
    let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
    for (const p of projected) {
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
        if (p.y < minY) minY = p.y;
        if (p.y > maxY) maxY = p.y;
    }
    const rangeX = maxX - minX || 1;
    const rangeY = maxY - minY || 1;
    for (const p of projected) {
        p.x = ((p.x - minX) / rangeX - 0.5) * 2;
        p.y = ((p.y - minY) / rangeY - 0.5) * 2;
    }

    return projected;
}

// ============================================================================
// Drag & Drop Editing for Knowledge Graph
// ============================================================================
function initDragDropEditing() {
    const graphContainer = document.getElementById('knowledge-graph-full');
    if (!graphContainer) return;

    // 使类和实体可拖拽
    graphContainer.addEventListener('dragstart', (e) => {
        if (e.target.classList.contains('graph-node')) {
            e.dataTransfer.setData('text/plain', e.target.dataset.nodeId);
            e.dataTransfer.setData('type', e.target.dataset.nodeType);
            e.target.style.opacity = '0.5';
        }
    });

    graphContainer.addEventListener('dragend', (e) => {
        if (e.target.classList.contains('graph-node')) {
            e.target.style.opacity = '1';
        }
    });

    graphContainer.addEventListener('dragover', (e) => {
        e.preventDefault();
        const target = e.target.closest('.graph-node');
        if (target) {
            target.style.outline = '2px solid #4CAF50';
        }
    });

    graphContainer.addEventListener('dragleave', (e) => {
        const target = e.target.closest('.graph-node');
        if (target) {
            target.style.outline = '';
        }
    });

    graphContainer.addEventListener('drop', async (e) => {
        e.preventDefault();
        const target = e.target.closest('.graph-node');
        if (!target) return;
        target.style.outline = '';

        const sourceId = e.dataTransfer.getData('text/plain');
        const sourceType = e.dataTransfer.getData('type');
        const targetId = target.dataset.nodeId;
        const targetType = target.dataset.nodeType;

        if (sourceId === targetId) return;

        // 弹出关系选择
        const relations = await api('/api/relations');
        const relOptions = (relations || []).map(r =>
            `<option value="${r.id}">${r.name || r.id}</option>`
        ).join('');

        showModal('创建关系', `
            <div class="form-group">
                <label>从 ${sourceId} 到 ${targetId}</label>
                <select id="drag-relation-select" class="form-control">
                    <option value="">选择关系...</option>
                    ${relOptions}
                </select>
            </div>
            <div class="form-group">
                <label>或输入新关系名称</label>
                <input type="text" id="drag-new-relation" class="form-control" placeholder="新关系名">
            </div>
        `, async () => {
            const relId = document.getElementById('drag-relation-select').value ||
                          document.getElementById('drag-new-relation').value;
            if (!relId) {
                notify('请选择或输入关系', 'error');
                return;
            }

            try {
                await api('/api/triples', {
                    method: 'POST',
                    body: {
                        subject: sourceId,
                        predicate: relId,
                        object: targetId,
                        confidence: 1.0,
                        source: 'drag-drop'
                    }
                });
                notify(`关系已创建: ${sourceId} → ${relId} → ${targetId}`);
                loadKnowledgeGraph();
            } catch (e) {
                notify('创建关系失败: ' + e.message, 'error');
            }
        });
    });
}

// ============================================================================
// LLM Auto-Modeling UI
// ============================================================================
async function llmAutoModel() {
    const text = document.getElementById('llm-model-text')?.value;
    const domain = document.getElementById('llm-model-domain')?.value || '';
    const apiKey = document.getElementById('llm-api-key')?.value || '';

    if (!text || text.trim().length === 0) {
        notify('请输入文本内容', 'error');
        return;
    }

    showLoading();
    try {
        const result = await api('/api/llm/auto-model', {
            method: 'POST',
            body: { text, domain, apiKey, apply: false }
        });

        // 显示建议
        const container = document.getElementById('llm-model-results');
        if (container) {
            let html = `<div class="suggestion-card">
                <h4>本体建议 (置信度: ${(result.confidence * 100).toFixed(0)}%)</h4>`;

            if (result.classes && result.classes.length > 0) {
                html += `<div class="suggestion-section"><strong>类:</strong> ${result.classes.join(', ')}</div>`;
            }
            if (result.relations && result.relations.length > 0) {
                html += `<div class="suggestion-section"><strong>关系:</strong> ${result.relations.join(', ')}</div>`;
            }
            if (result.triples && result.triples.length > 0) {
                html += `<div class="suggestion-section"><strong>三元组:</strong><ul>`;
                for (const t of result.triples) {
                    html += `<li>${t.subject} → ${t.predicate} → ${t.object}</li>`;
                }
                html += `</ul></div>`;
            }

            html += `<button class="btn btn-primary" onclick="applyAutoModel()">应用到知识图谱</button></div>`;
            container.innerHTML = html;
        }
    } catch (e) {
        notify('自动建模失败: ' + e.message, 'error');
    } finally {
        hideLoading();
    }
}

async function applyAutoModel() {
    const text = document.getElementById('llm-model-text')?.value;
    const domain = document.getElementById('llm-model-domain')?.value || '';
    const apiKey = document.getElementById('llm-api-key')?.value || '';

    showLoading();
    try {
        const result = await api('/api/llm/auto-model', {
            method: 'POST',
            body: { text, domain, apiKey, apply: true }
        });

        const applied = result.applied || {};
        notify(`已应用: ${applied.classes || 0} 类, ${applied.relations || 0} 关系, ${applied.triples || 0} 三元组`);
        loadDashboard();
    } catch (e) {
        notify('应用失败: ' + e.message, 'error');
    } finally {
        hideLoading();
    }
}

async function llmExtract() {
    const text = document.getElementById('llm-extract-text')?.value;
    const domain = document.getElementById('llm-extract-domain')?.value || '';
    const apiKey = document.getElementById('llm-api-key')?.value || '';

    if (!text || text.trim().length === 0) {
        notify('请输入文本内容', 'error');
        return;
    }

    showLoading();
    try {
        const result = await api('/api/llm/extract', {
            method: 'POST',
            body: { text, domain, apiKey, apply: false }
        });

        const container = document.getElementById('llm-extract-results');
        if (container) {
            let html = `<div class="extraction-card">
                <h4>提取结果 (置信度: ${(result.confidence * 100).toFixed(0)}%)</h4>`;

            if (result.entities && result.entities.length > 0) {
                html += `<div class="extraction-section"><strong>实体:</strong><ul>`;
                for (const e of result.entities) {
                    html += `<li><span class="entity-id">${e.id}</span> <span class="entity-type">(${e.type})</span></li>`;
                }
                html += `</ul></div>`;
            }
            if (result.relations && result.relations.length > 0) {
                html += `<div class="extraction-section"><strong>关系:</strong><ul>`;
                for (const r of result.relations) {
                    html += `<li>${r.subject} → ${r.predicate} → ${r.object}</li>`;
                }
                html += `</ul></div>`;
            }

            html += `<button class="btn btn-primary" onclick="applyExtraction()">应用到知识图谱</button></div>`;
            container.innerHTML = html;
        }
    } catch (e) {
        notify('提取失败: ' + e.message, 'error');
    } finally {
        hideLoading();
    }
}

async function applyExtraction() {
    const text = document.getElementById('llm-extract-text')?.value;
    const domain = document.getElementById('llm-extract-domain')?.value || '';
    const apiKey = document.getElementById('llm-api-key')?.value || '';

    showLoading();
    try {
        const result = await api('/api/llm/extract', {
            method: 'POST',
            body: { text, domain, apiKey, apply: true }
        });

        const applied = result.applied || {};
        notify(`已应用: ${applied.entities || 0} 实体, ${applied.triples || 0} 三元组`);
        loadDashboard();
    } catch (e) {
        notify('应用失败: ' + e.message, 'error');
    } finally {
        hideLoading();
    }
}

async function llmReason() {
    const query = document.getElementById('llm-reason-query')?.value;
    const apiKey = document.getElementById('llm-api-key')?.value || '';

    if (!query || query.trim().length === 0) {
        notify('请输入问题', 'error');
        return;
    }

    showLoading();
    try {
        const result = await api('/api/llm/reason', {
            method: 'POST',
            body: { query, apiKey }
        });

        const container = document.getElementById('llm-reason-results');
        if (container) {
            let html = `<div class="reason-result-card" style="background: var(--bg-secondary); padding: 1rem; border-radius: 8px;">
                <div style="margin-bottom: 0.5rem"><strong>回答:</strong></div>
                <div style="margin-bottom: 1rem; line-height: 1.6">${result.answer || '无回答'}</div>
                <div style="font-size: 0.85rem; color: var(--text-muted)">
                    置信度: ${(result.confidence * 100).toFixed(0)}% |
                    事实数: ${result.facts ? result.facts.length : 0} |
                    推理步骤: ${result.reasoning ? result.reasoning.length : 0}
                </div>`;

            if (result.reasoning && result.reasoning.length > 0) {
                html += `<div style="margin-top: 0.5rem; font-size: 0.85rem"><strong>推理链:</strong><ul>`;
                for (const step of result.reasoning) {
                    html += `<li>${step}</li>`;
                }
                html += `</ul></div>`;
            }

            html += '</div>';
            container.innerHTML = html;
        }
    } catch (e) {
        notify('推理失败: ' + e.message, 'error');
    } finally {
        hideLoading();
    }
}

async function saveLlmConfig() {
    const apiKey = document.getElementById('llm-api-key')?.value || '';
    const model = document.getElementById('llm-model')?.value || 'claude-sonnet-4-20250514';

    try {
        await api('/api/llm/config', {
            method: 'POST',
            body: { apiKey, model }
        });
        notify('LLM 配置已保存');
    } catch (e) {
        notify('保存失败: ' + e.message, 'error');
    }
}

function switchEmbeddingTab(tab) {
    const viz2d = document.getElementById('embedding-visualization');
    const viz3d = document.getElementById('embedding-3d-container');

    if (tab === '3d') {
        if (viz2d) viz2d.style.display = 'none';
        if (viz3d) viz3d.style.display = 'block';
        visualize3DEmbeddings();
    } else {
        if (viz2d) viz2d.style.display = 'block';
        if (viz3d) viz3d.style.display = 'none';
        visualizeEmbeddings();
    }

    // 更新标签状态
    const tabs = document.querySelectorAll('#page-embeddings .tab');
    tabs.forEach(t => t.classList.remove('active'));
    if (tab === '3d' && tabs[1]) tabs[1].classList.add('active');
    if (tab === '2d' && tabs[0]) tabs[0].classList.add('active');
}

// ============================================================================
// Missing onclick Functions
// ============================================================================

function toggleClassView() {
    const treeView = document.getElementById('class-tree-view');
    const listView = document.getElementById('class-list-view');
    if (treeView && listView) {
        const isTreeVisible = treeView.style.display !== 'none';
        treeView.style.display = isTreeVisible ? 'none' : 'block';
        listView.style.display = isTreeVisible ? 'block' : 'none';
    }
}

async function batchImportTriples() {
    const textarea = document.getElementById('batch-triple-input');
    if (!textarea || !textarea.value.trim()) {
        notify('请输入三元组数据', 'warning');
        return;
    }

    try {
        const lines = textarea.value.trim().split('\n').filter(l => l.trim());
        const triples = [];
        for (const line of lines) {
            const parts = line.split('\t');
            if (parts.length >= 3) {
                triples.push({ subject: parts[0], predicate: parts[1], object: parts[2], confidence: 1.0 });
            }
        }

        if (triples.length === 0) {
            notify('未找到有效的三元组', 'warning');
            return;
        }

        const result = await api('/api/triples/batch', 'POST', triples);
        notify(`成功导入 ${triples.length} 个三元组`, 'success');
        textarea.value = '';
        await loadTriples();
    } catch (e) {
        notify(`批量导入失败: ${e.message}`, 'error');
    }
}

function prevPage() {
    if (currentPage > 1) {
        currentPage--;
        loadTriples();
    }
}

function nextPage() {
    currentPage++;
    loadTriples();
}

function formatSparql() {
    const textarea = document.getElementById('sparql-query');
    if (!textarea) return;
    let query = textarea.value;
    // Basic SPARQL formatting
    const keywords = ['SELECT', 'WHERE', 'OPTIONAL', 'FILTER', 'UNION', 'ORDER BY', 'GROUP BY',
                      'HAVING', 'LIMIT', 'OFFSET', 'DISTINCT', 'ASK', 'CONSTRUCT', 'DESCRIBE',
                      'PREFIX', 'BASE', 'FROM'];
    for (const kw of keywords) {
        const regex = new RegExp('\\b' + kw + '\\b', 'gi');
        query = query.replace(regex, '\n' + kw);
    }
    query = query.replace(/\n\s*\n/g, '\n').trim();
    textarea.value = query;
}

function explainSparql() {
    const textarea = document.getElementById('sparql-query');
    if (!textarea || !textarea.value.trim()) {
        notify('请输入SPARQL查询', 'warning');
        return;
    }
    const query = textarea.value.trim();
    let explanation = '查询分析:\n';

    if (query.match(/\bSELECT\b/i)) explanation += '• SELECT 查询: 返回变量绑定结果\n';
    if (query.match(/\bASK\b/i)) explanation += '• ASK 查询: 返回布尔结果\n';
    if (query.match(/\bCONSTRUCT\b/i)) explanation += '• CONSTRUCT 查询: 返回构造的三元组\n';
    if (query.match(/\bDESCRIBE\b/i)) explanation += '• DESCRIBE 查询: 返回资源描述\n';
    if (query.match(/\bOPTIONAL\b/i)) explanation += '• OPTIONAL: 可选匹配，不满足时变量为空\n';
    if (query.match(/\bFILTER\b/i)) explanation += '• FILTER: 结果过滤条件\n';
    if (query.match(/\bUNION\b/i)) explanation += '• UNION: 合并多个模式的查询结果\n';
    if (query.match(/\bORDER\s+BY\b/i)) explanation += '• ORDER BY: 结果排序\n';
    if (query.match(/\bGROUP\s+BY\b/i)) explanation += '• GROUP BY: 分组聚合\n';
    if (query.match(/\bHAVING\b/i)) explanation += '• HAVING: 分组过滤条件\n';
    if (query.match(/\bLIMIT\b/i)) explanation += '• LIMIT: 限制结果数量\n';

    notify(explanation, 'info');
}

function editRelation(id) {
    const rel = document.querySelector(`[data-relation-id="${id}"]`);
    if (!rel) return;
    const nameEl = rel.querySelector('.relation-name');
    if (!nameEl) return;
    const oldName = nameEl.textContent;
    const newName = prompt('编辑关系名称:', oldName);
    if (newName && newName !== oldName) {
        api(`/api/relations/${id}`, 'PUT', { name: newName }).then(() => {
            nameEl.textContent = newName;
            notify('关系已更新', 'success');
        }).catch(e => notify(`更新失败: ${e.message}`, 'error'));
    }
}

function editIndividual(id) {
    // Navigate to individual detail
    const page = document.getElementById('page-ontology');
    if (page) {
        const tab = page.querySelector('[data-tab="individuals"]');
        if (tab) tab.click();
    }
    notify(`编辑个体: ${id}`, 'info');
}

function deleteRule(id) {
    if (!confirm('确定要删除此规则吗?')) return;
    api(`/api/rules/${id}`, 'DELETE').then(() => {
        notify('规则已删除', 'success');
        loadRules();
    }).catch(e => notify(`删除失败: ${e.message}`, 'error'));
}

// ============================================================================
// Dropdown Population Functions
// ============================================================================

async function populateClassDropdowns() {
    try {
        const classes = await api('/api/classes');
        const selects = ['individual-class-filter', 'search-class-filter'];
        for (const selectId of selects) {
            const select = document.getElementById(selectId);
            if (!select) continue;
            const currentVal = select.value;
            select.innerHTML = '<option value="">所有类</option>';
            if (Array.isArray(classes)) {
                for (const cls of classes) {
                    const opt = document.createElement('option');
                    opt.value = cls.id;
                    opt.textContent = cls.name || cls.id;
                    select.appendChild(opt);
                }
            }
            if (currentVal) select.value = currentVal;
        }
    } catch (e) { /* silent */ }
}

async function populateSearchDropdowns() {
    await populateClassDropdowns();
}

async function populateKGDropdowns() {
    try {
        const relations = await api('/api/relations');
        const select = document.getElementById('kg-relation-filter');
        if (select) {
            const currentVal = select.value;
            select.innerHTML = '<option value="">全部关系</option>';
            if (Array.isArray(relations)) {
                for (const rel of relations) {
                    const opt = document.createElement('option');
                    opt.value = rel.id;
                    opt.textContent = rel.name || rel.id;
                    select.appendChild(opt);
                }
            }
            if (currentVal) select.value = currentVal;
        }

        // Also populate link-relation
        const linkSelect = document.getElementById('link-relation');
        if (linkSelect) {
            linkSelect.innerHTML = '<option value="">选择关系</option>';
            if (Array.isArray(relations)) {
                for (const rel of relations) {
                    const opt = document.createElement('option');
                    opt.value = rel.id;
                    opt.textContent = rel.name || rel.id;
                    linkSelect.appendChild(opt);
                }
            }
        }
    } catch (e) { /* silent */ }
}

// ============================================================================
// LLM Config Page
// ============================================================================

async function loadLLMConfig() {
    try {
        const config = await api('/api/config');
        const apiKeyInput = document.getElementById('llm-api-key');
        const endpointInput = document.getElementById('llm-endpoint');
        const modelInput = document.getElementById('llm-model');
        if (apiKeyInput && config.llm) apiKeyInput.value = config.llm.apiKey || '';
        if (endpointInput && config.llm) endpointInput.value = config.llm.endpoint || '';
        if (modelInput && config.llm) modelInput.value = config.llm.model || '';
    } catch (e) { /* silent */ }
}

// ============================================================================
// Local Model Configuration (HTTP API)
// ============================================================================

async function loadLocalModelConfig() {
    try {
        const config = await api('/api/storage/config');
        if (config.rag && config.rag.localModel) {
            const lm = config.rag.localModel;

            // Embedding
            if (lm.embedding) {
                document.getElementById('local-embedding-endpoint').value = lm.embedding.endpoint || '';
                document.getElementById('local-embedding-model').value = lm.embedding.model || '';
                document.getElementById('local-embedding-api-path').value = lm.embedding.apiPath || '/v1/embeddings';
                document.getElementById('local-embedding-timeout').value = lm.embedding.timeout || 30000;
            }
            if (lm.embeddingApiKey !== undefined) {
                document.getElementById('local-embedding-api-key').value = lm.embeddingApiKey || '';
            }

            // Rerank
            if (lm.rerank) {
                document.getElementById('local-rerank-endpoint').value = lm.rerank.endpoint || '';
                document.getElementById('local-rerank-model').value = lm.rerank.model || '';
                document.getElementById('local-rerank-api-path').value = lm.rerank.apiPath || '/v1/rerank';
                document.getElementById('local-rerank-timeout').value = lm.rerank.timeout || 30000;
            }
            if (lm.rerankApiKey !== undefined) {
                document.getElementById('local-rerank-api-key').value = lm.rerankApiKey || '';
            }

            // LLM
            if (lm.llm) {
                document.getElementById('local-llm-endpoint').value = lm.llm.endpoint || '';
                document.getElementById('local-llm-model').value = lm.llm.model || '';
                document.getElementById('local-llm-api-path').value = lm.llm.apiPath || '/v1/chat/completions';
                document.getElementById('local-llm-timeout').value = lm.llm.timeout || 60000;
                document.getElementById('local-llm-max-tokens').value = lm.llm.maxTokens || 4096;
            }
            if (lm.llmApiKey !== undefined) {
                document.getElementById('local-llm-api-key').value = lm.llmApiKey || '';
            }

            // Image2Text toggle
            document.getElementById('local-enable-image2text').checked = lm.enableImageToText || false;
        }
    } catch (e) {
        console.error('Local model config load error:', e);
    }
}

async function saveLocalModelConfig() {
    // Load current full config first, then merge localModel section
    let fullConfig = {};
    try {
        fullConfig = await api('/api/storage/config');
    } catch (e) { /* empty */ }

    fullConfig.rag = fullConfig.rag || {};
    fullConfig.rag.localModel = {
        embedding: {
            endpoint: document.getElementById('local-embedding-endpoint').value,
            model: document.getElementById('local-embedding-model').value,
            apiPath: document.getElementById('local-embedding-api-path').value,
            apiKey: document.getElementById('local-embedding-api-key').value,
            timeout: parseInt(document.getElementById('local-embedding-timeout').value) || 30000
        },
        rerank: {
            endpoint: document.getElementById('local-rerank-endpoint').value,
            model: document.getElementById('local-rerank-model').value,
            apiPath: document.getElementById('local-rerank-api-path').value,
            apiKey: document.getElementById('local-rerank-api-key').value,
            timeout: parseInt(document.getElementById('local-rerank-timeout').value) || 30000
        },
        llm: {
            endpoint: document.getElementById('local-llm-endpoint').value,
            model: document.getElementById('local-llm-model').value,
            apiPath: document.getElementById('local-llm-api-path').value,
            apiKey: document.getElementById('local-llm-api-key').value,
            timeout: parseInt(document.getElementById('local-llm-timeout').value) || 60000,
            maxTokens: parseInt(document.getElementById('local-llm-max-tokens').value) || 4096
        },
        enableImageToText: document.getElementById('local-enable-image2text').checked
    };

    try {
        await api('/api/storage/config', { method: 'POST', body: fullConfig });
        notify('本地模型配置已保存');
    } catch (e) {
        notify('保存失败: ' + e.message, 'error');
    }
}

// ============================================================================
// RAG Page
// ============================================================================

async function loadRagPage() {
    try {
        const kbs = await api('/rag/knowledge-bases');
        const container = document.getElementById('rag-kb-list');
        if (container) {
            container.innerHTML = '';
            if (Array.isArray(kbs) && kbs.length > 0) {
                for (const kb of kbs) {
                    const item = document.createElement('div');
                    item.className = 'kb-item';
                    item.innerHTML = `
                        <div class="kb-info">
                            <h4>${kb.name || kb.id}</h4>
                            <p>${kb.description || ''}</p>
                            <span class="kb-meta">${kb.documentCount || 0} 文档 · ${kb.chunkCount || 0} 分块</span>
                        </div>
                        <div class="kb-actions">
                            <button onclick="deleteKB('${kb.id}')" class="btn btn-sm btn-danger">删除</button>
                        </div>
                    `;
                    container.appendChild(item);
                }
            } else {
                container.innerHTML = '<p class="text-muted">暂无知识库</p>';
            }
        }
    } catch (e) {
        console.error('Load RAG page error:', e);
    }
}

async function createKnowledgeBase() {
    const name = document.getElementById('rag-kb-name')?.value;
    const desc = document.getElementById('rag-kb-desc')?.value;
    if (!name) { notify('请输入知识库名称', 'warning'); return; }

    try {
        await api('/rag/knowledge-bases', 'POST', { name, description: desc || '' });
        notify('知识库创建成功', 'success');
        loadRagPage();
    } catch (e) {
        notify(`创建失败: ${e.message}`, 'error');
    }
}

async function uploadRagDocument() {
    const kbId = document.getElementById('rag-upload-kb')?.value;
    const title = document.getElementById('rag-doc-title')?.value;
    const content = document.getElementById('rag-doc-content')?.value;
    if (!title || !content) { notify('请输入标题和内容', 'warning'); return; }

    try {
        await api('/rag/documents', 'POST', { knowledgeBaseId: kbId || 'default', title, content, source: 'web' });
        notify('文档上传成功', 'success');
    } catch (e) {
        notify(`上传失败: ${e.message}`, 'error');
    }
}

async function ragSearch() {
    const query = document.getElementById('rag-search-input')?.value;
    if (!query) { notify('请输入搜索内容', 'warning'); return; }

    try {
        const results = await api('/rag/search', 'POST', { query, topK: 10 });
        const container = document.getElementById('rag-search-results');
        if (container) {
            container.innerHTML = '';
            if (Array.isArray(results) && results.length > 0) {
                for (const r of results) {
                    const item = document.createElement('div');
                    item.className = 'rag-result-item';
                    item.innerHTML = `
                        <div class="rag-result-header">
                            <strong>${r.title || r.id || '结果'}</strong>
                            <span class="rag-result-score">相似度: ${(r.score * 100).toFixed(1)}%</span>
                        </div>
                        <p class="rag-result-text">${r.text || r.content || ''}</p>
                    `;
                    container.appendChild(item);
                }
            } else {
                container.innerHTML = '<p class="text-muted">未找到相关结果</p>';
            }
        }
    } catch (e) {
        notify(`搜索失败: ${e.message}`, 'error');
    }
}

async function deleteKB(id) {
    if (!confirm('确定要删除此知识库吗?')) return;
    try {
        await api(`/rag/knowledge-bases/${id}`, 'DELETE');
        notify('知识库已删除', 'success');
        loadRagPage();
    } catch (e) {
        notify(`删除失败: ${e.message}`, 'error');
    }
}
