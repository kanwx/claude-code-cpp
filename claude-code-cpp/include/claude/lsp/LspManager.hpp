#pragma once

#include "LspClient.hpp"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <filesystem>

namespace claude::lsp {

/// 语言服务器配置
struct LanguageServerConfig {
    String languageId;     // cpp, python, typescript, etc.
    String command;        // 启动命令
    std::vector<String> extensions;  // 文件扩展名
    String name;           // 显示名称
};

/// LSP 管理器 - 管理多个语言服务器
class LspManager {
public:
    static LspManager& instance() {
        static LspManager manager;
        return manager;
    }

    /// 初始化（设置工作目录）
    void init(const std::filesystem::path& rootPath);

    /// 注册语言服务器
    void registerLanguageServer(const LanguageServerConfig& config);

    /// 获取文档的语言服务器
    std::shared_ptr<LSPClient> getClientForFile(const std::filesystem::path& path);

    /// 获取指定语言的客户端
    std::shared_ptr<LSPClient> getClient(const String& languageId);

    /// 打开文档
    void openDocument(const std::filesystem::path& path);

    /// 关闭文档
    void closeDocument(const std::filesystem::path& path);

    /// Notify LSP servers that a file has changed (textDocument/didChange)
    void notifyDidChange(const std::filesystem::path& path, const String& newContent);

    /// 获取诊断
    std::vector<Diagnostic> getDiagnostics(const std::filesystem::path& path);

    /// 跳转到定义
    std::vector<Location> gotoDefinition(const std::filesystem::path& path, int line, int column);

    /// 查找引用
    std::vector<Location> findReferences(const std::filesystem::path& path, int line, int column);

    /// 获取悬停
    std::optional<HoverInfo> hover(const std::filesystem::path& path, int line, int column);

    /// 获取文档符号
    std::vector<SymbolInfo> documentSymbols(const std::filesystem::path& path);

    /// 关闭所有服务器
    void shutdownAll();

    /// 获取已注册的语言列表
    std::vector<String> getSupportedLanguages() const;

    /// 设置诊断回调
    void setDiagnosticsCallback(
        const String& languageId,
        std::function<void(const String& uri, std::vector<Diagnostic>)> callback
    );

private:
    LspManager() {
        initDefaultServers();
    }

    void initDefaultServers();

    String detectLanguage(const std::filesystem::path& path) const;
    String pathToUri(const std::filesystem::path& path) const;

    mutable std::mutex mutex_;
    std::filesystem::path rootPath_;
    std::unordered_map<String, std::shared_ptr<LSPClient>> clients_;
    std::unordered_map<String, LanguageServerConfig> configs_;
    std::unordered_map<String, String> extensionToLanguage_;
};

/// 预定义的语言服务器
namespace LanguageServers {
    LanguageServerConfig clangd();        // C/C++
    LanguageServerConfig pylsp();         // Python
    LanguageServerConfig typescript();    // TypeScript/JavaScript
    LanguageServerConfig rust();          // Rust
    LanguageServerConfig go();            // Go
    LanguageServerConfig java();          // Java
}

} // namespace claude::lsp
