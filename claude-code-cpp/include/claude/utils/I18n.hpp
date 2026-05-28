#pragma once

#include "../core/Types.hpp"
#include <unordered_map>
#include <mutex>

namespace claude {

/// 国际化服务 - i18n support
class I18n {
public:
    /// 支持的语言
    enum class Language {
        English,
        Chinese,
        Japanese,
        Korean,
        Auto    // 自动检测
    };

    static I18n& instance();

    /// 初始化
    void init(Language lang = Language::Auto);

    /// 获取翻译
    String tr(const String& key) const;

    /// 带参数的翻译
    String tr(const String& key, const std::unordered_map<String, String>& params) const;

    /// 设置语言
    void setLanguage(Language lang);

    /// 获取当前语言
    Language getCurrentLanguage() const;

    /// 获取语言名称
    String getLanguageName() const;

    /// 获取支持的语言列表
    static std::vector<std::pair<Language, String>> getSupportedLanguages();

private:
    I18n();

    mutable std::mutex mutex_;
    Language currentLang_ = Language::English;
    std::unordered_map<String, String> translations_;
    std::unordered_map<String, String> englishTranslations_;

    static Language detectSystemLanguage();
    static String languageToString(Language lang);

    void loadLanguagePack(Language lang);
    void initEnglishTranslations();
    void loadChineseTranslations();
};

/// 便捷宏
#define tr(key) I18n::instance().tr(key)
#define trp(key, params) I18n::instance().tr(key, params)

} // namespace claude
