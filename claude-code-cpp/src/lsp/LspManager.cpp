#include <claude/lsp/LspManager.hpp>
#include <fstream>
#include <sstream>

namespace claude::lsp {

void LspManager::init(const std::filesystem::path& rootPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    rootPath_ = rootPath;
}

void LspManager::registerLanguageServer(const LanguageServerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    configs_[config.languageId] = config;

    // 映射扩展名到语言
    for (const auto& ext : config.extensions) {
        extensionToLanguage_[ext] = config.languageId;
    }
}

std::shared_ptr<LSPClient> LspManager::getClientForFile(const std::filesystem::path& path) {
    String lang = detectLanguage(path);
    if (lang.empty()) return nullptr;
    return getClient(lang);
}

std::shared_ptr<LSPClient> LspManager::getClient(const String& languageId) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查是否已有客户端
    auto it = clients_.find(languageId);
    if (it != clients_.end()) {
        return it->second;
    }

    // 查找配置
    auto configIt = configs_.find(languageId);
    if (configIt == configs_.end()) {
        return nullptr;
    }

    // 创建新客户端
    const auto& config = configIt->second;
    auto client = std::make_shared<StdioLSPClient>(config.command);

    if (!client->initialize(rootPath_.string())) {
        return nullptr;
    }

    clients_[languageId] = client;
    return client;
}

void LspManager::openDocument(const std::filesystem::path& path) {
    auto client = getClientForFile(path);
    if (!client) return;

    String uri = pathToUri(path);
    String lang = detectLanguage(path);

    // 读取文件内容
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();

    client->openDocument(uri, lang, buffer.str());
}

void LspManager::closeDocument(const std::filesystem::path& path) {
    auto client = getClientForFile(path);
    if (!client) return;

    String uri = pathToUri(path);
    client->closeDocument(uri);
}

void LspManager::notifyDidChange(const std::filesystem::path& path, const String& newContent) {
    // Close and re-open to force LSP refresh
    // (sendNotification is private on StdioLSPClient, not exposed on LSPClient base)
    closeDocument(path);
    // Re-open with new content will trigger re-analysis
    // Note: openDocument reads from disk, so the new content is already there
    openDocument(path);
}

std::vector<Diagnostic> LspManager::getDiagnostics(const std::filesystem::path& path) {
    auto client = getClientForFile(path);
    if (!client) return {};

    return client->getDiagnostics(pathToUri(path));
}

std::vector<Location> LspManager::gotoDefinition(const std::filesystem::path& path, int line, int column) {
    auto client = getClientForFile(path);
    if (!client) return {};

    return client->gotoDefinition(pathToUri(path), line, column);
}

std::vector<Location> LspManager::findReferences(const std::filesystem::path& path, int line, int column) {
    auto client = getClientForFile(path);
    if (!client) return {};

    return client->findReferences(pathToUri(path), line, column);
}

std::optional<HoverInfo> LspManager::hover(const std::filesystem::path& path, int line, int column) {
    auto client = getClientForFile(path);
    if (!client) return std::nullopt;

    return client->hover(pathToUri(path), line, column);
}

std::vector<SymbolInfo> LspManager::documentSymbols(const std::filesystem::path& path) {
    auto client = getClientForFile(path);
    if (!client) return {};

    return client->documentSymbols(pathToUri(path));
}

void LspManager::shutdownAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [lang, client] : clients_) {
        if (client) {
            client->shutdown();
        }
    }
    clients_.clear();
}

std::vector<String> LspManager::getSupportedLanguages() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<String> langs;
    langs.reserve(configs_.size());
    for (const auto& [lang, _] : configs_) {
        langs.push_back(lang);
    }
    return langs;
}

void LspManager::setDiagnosticsCallback(
    const String& languageId,
    std::function<void(const String& uri, std::vector<Diagnostic>)> callback
) {
    auto client = getClient(languageId);
    if (client) {
        client->setDiagnosticsCallback(std::move(callback));
    }
}

String LspManager::detectLanguage(const std::filesystem::path& path) const {
    String ext = path.extension().string();

    auto it = extensionToLanguage_.find(ext);
    if (it != extensionToLanguage_.end()) {
        return it->second;
    }

    // 默认映射
    static const std::unordered_map<String, String> defaults = {
        {".cpp", "cpp"}, {".cxx", "cpp"}, {".cc", "cpp"}, {".C", "cpp"},
        {".hpp", "cpp"}, {".hxx", "cpp"}, {".hh", "cpp"}, {".h", "cpp"},
        {".c", "c"},
        {".py", "python"}, {".pyw", "python"},
        {".ts", "typescript"}, {".tsx", "typescript"},
        {".js", "javascript"}, {".jsx", "javascript"},
        {".rs", "rust"},
        {".go", "go"},
        {".java", "java"},
        {".kt", "kotlin"}, {".kts", "kotlin"},
        {".swift", "swift"},
        {".rb", "ruby"},
        {".php", "php"},
        {".cs", "csharp"},
        {".json", "json"},
        {".yaml", "yaml"}, {".yml", "yaml"},
        {".md", "markdown"},
        {".sh", "shell"}, {".bash", "shell"},
        {".zsh", "shell"}
    };

    auto dit = defaults.find(ext);
    return dit != defaults.end() ? dit->second : "";
}

String LspManager::pathToUri(const std::filesystem::path& path) const {
    return "file://" + std::filesystem::absolute(path).string();
}

void LspManager::initDefaultServers() {
    // 注册常用语言服务器
    // 注意：这些需要用户系统已安装

    registerLanguageServer(LanguageServers::clangd());
    registerLanguageServer(LanguageServers::pylsp());
    registerLanguageServer(LanguageServers::typescript());
    registerLanguageServer(LanguageServers::rust());
    registerLanguageServer(LanguageServers::go());
}

// ========== 预定义语言服务器 ==========

namespace LanguageServers {

LanguageServerConfig clangd() {
    return {
        "cpp",
        "clangd --background-index --clang-tidy",
        {".cpp", ".cxx", ".cc", ".C", ".hpp", ".hxx", ".hh", ".h", ".c"},
        "clangd (C/C++)"
    };
}

LanguageServerConfig pylsp() {
    return {
        "python",
        "pylsp",  // 或 "pyright" 或 "ruff-lsp"
        {".py", ".pyw"},
        "Python LSP"
    };
}

LanguageServerConfig typescript() {
    return {
        "typescript",
        "typescript-language-server --stdio",
        {".ts", ".tsx", ".js", ".jsx"},
        "TypeScript/JavaScript"
    };
}

LanguageServerConfig rust() {
    return {
        "rust",
        "rust-analyzer",
        {".rs"},
        "rust-analyzer"
    };
}

LanguageServerConfig go() {
    return {
        "go",
        "gopls",
        {".go"},
        "gopls (Go)"
    };
}

LanguageServerConfig java() {
    return {
        "java",
        "jdtls",  // Eclipse JDT Language Server
        {".java"},
        "jdtls (Java)"
    };
}

} // namespace LanguageServers

} // namespace claude::lsp
