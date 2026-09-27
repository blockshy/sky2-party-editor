// 八语界面共享的纯语言状态：不依赖 Windows、游戏地址或资源文件。
// 游戏文本语言由只读检测层更新；所有显示函数读取同一枚举，避免混用系统语言。
#pragma once
#include <atomic>
#include <cstdint>

namespace sky2party {
// 此顺序同时用于原生角色名称表；它与游戏内部语言编号不同，检测层负责转换。
enum class Language : uint8_t {
    Chinese = 0, Japanese = 1, English = 2, TraditionalChinese = 3,
    German = 4, French = 5, Spanish = 6, Korean = 7
};
inline constexpr unsigned kLanguageCount = 8;

// 使用 C++17 内联变量，让独立版、ASI 版及纯逻辑测试共用相同选择规则。
// 默认保持既有中文界面；检测到有效原生语言后再切换，不把未知值当成数组下标。
inline std::atomic<Language> g_displayLanguage{Language::Chinese};
inline Language CurrentLanguage() noexcept {
    return g_displayLanguage.load(std::memory_order_relaxed);
}
inline void SetDisplayLanguage(Language language) noexcept {
    if (static_cast<unsigned>(language) < kLanguageCount)
        g_displayLanguage.store(language, std::memory_order_relaxed);
}
inline const char* LocalizeFor(Language language, const char* chinese,
                              const char* japanese, const char* english,
                              const char* traditional = nullptr, const char* german = nullptr,
                              const char* french = nullptr, const char* spanish = nullptr,
                              const char* korean = nullptr) noexcept {
    const char* const texts[]{chinese, japanese, english, traditional, german, french, spanish, korean};
    const unsigned index = static_cast<unsigned>(language);
    const char* selected = index < kLanguageCount ? texts[index] : nullptr;
    // 防御非法编号及缺失文本；正式界面通过八语完整性检查，不依赖此回退。
    // 默认参数仅兼容纯逻辑调用方；游戏专名使用原生名称读取层，不在此跨语言回退。
    if (selected && *selected) return selected;
    return english && *english ? english : (chinese ? chinese : "");
}
inline const char* Localize(const char* chinese, const char* japanese,
                          const char* english, const char* traditional = nullptr,
                          const char* german = nullptr, const char* french = nullptr,
                          const char* spanish = nullptr, const char* korean = nullptr) noexcept {
    return LocalizeFor(CurrentLanguage(), chinese, japanese, english,
                       traditional, german, french, spanish, korean);
}
} // namespace sky2party
