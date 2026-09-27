// 八语自有文案的纯逻辑回归，不访问游戏进程、名称资源、字体或 Windows API。
// 除了翻译完整性，还验证 printf 的调用约定和同一消息编号跨语言切换的行为。
#include "ui_text.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace sky2party;
unsigned failures = 0;

void Check(bool passed, const char* reason, size_t textIndex, unsigned languageIndex) {
    if (!passed) {
        ++failures;
        std::printf("FAIL text=%zu language=%u: %s\n", textIndex, languageIndex, reason);
    }
}

bool ValidUtf8(const char* text) {
    if (!text) return false;
    const auto* at = reinterpret_cast<const unsigned char*>(text);
    while (*at) {
        const unsigned first = *at++;
        if (first < 0x80) {
            // 文案不应携带终端控制字符。换行可以用于明确的多行说明。
            if (first < 0x20 && first != '\n') return false;
            continue;
        }
        unsigned remaining = 0, value = 0, minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { remaining = 1; value = first & 0x1F; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { remaining = 2; value = first & 0x0F; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { remaining = 3; value = first & 0x07; minimum = 0x10000; }
        else return false;
        while (remaining--) {
            const unsigned next = *at;
            if (next < 0x80 || next > 0xBF) return false;
            ++at;
            value = (value << 6) | (next & 0x3F);
        }
        // 排除过长编码、UTF-16 代理区及超过 Unicode 上界的字节序列。
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    }
    return true;
}

// 当前文案合同仅允许字符串和无符号整数，无宽度、位置重排或 %n。
// 遇到其他格式必须显式扩展测试合同，不能由所有译文共同写错而蒙混通过。
std::string FormatArguments(const char* text) {
    std::string result;
    for (const char* at = text; *at; ++at) {
        if (*at != '%') continue;
        ++at;
        if (*at == '%') continue;
        if (*at != 's' && *at != 'u') return "!invalid-format!";
        result += *at;
    }
    return result;
}

const char* ExpectedArguments(Text id) {
    switch (id) {
    case Text::TitleFormat:
    case Text::UnknownStateFormat: return "s";
    case Text::PendingStateFormat:
    case Text::ConfirmRoleFormat: return "ss";
    case Text::FixedMemberFormat: return "us";
    case Text::FixedUnknownFormat: return "uu";
    case Text::FixedMoreFormat:
    case Text::CharacterIdFormat: return "u";
    default: return "";
    }
}
}

int main() {
    using namespace sky2party;
    for (size_t index = 0; index < static_cast<size_t>(Text::Count); ++index) {
        const auto id = static_cast<Text>(index);
        const auto& entry = kUiTexts[index];
        const char* const values[]{entry.chinese, entry.japanese, entry.english,
            entry.traditionalChinese, entry.german, entry.french, entry.spanish, entry.korean};
        for (unsigned language = 0; language < kLanguageCount; ++language) {
            const char* text = values[language];
            Check(text && (id == Text::None || *text), "missing translation", index, language);
            if (!text) continue;
            Check(id != Text::None || *text == '\0', "None must remain empty", index, language);
            Check(ValidUtf8(text), "invalid UTF-8 or control character", index, language);
            Check(FormatArguments(text) == ExpectedArguments(id), "printf parameter contract changed", index, language);
            Check(TextFor(static_cast<Language>(language), id) == text,
                "explicit language returns the wrong column", index, language);
            SetDisplayLanguage(static_cast<Language>(language));
            Check(std::strcmp(Tr(id), text) == 0, "Tr retained another language", index, language);
            // 完整长句不允许用同一份英语占位。短标签可合法同形，如德语 Status。
            if (language != static_cast<unsigned>(Language::English) && std::strlen(entry.english) > 40)
                Check(std::strcmp(text, entry.english) != 0, "English placeholder in translated sentence", index, language);
        }
    }

    // 模拟控制服务已经保存了一条失败状态；没有新事件也必须随游戏语言立即改变。
    const Text retainedStatus = Text::ControlSaveFailed;
    SetDisplayLanguage(Language::English);
    const std::string oldStatus = Tr(retainedStatus);
    SetDisplayLanguage(Language::Korean);
    Check(std::strcmp(Tr(retainedStatus), TextFor(Language::Korean, retainedStatus)) == 0 &&
        oldStatus != Tr(retainedStatus), "retained semantic status did not change language", 0, 0);
    SetDisplayLanguage(Language::Chinese);
    Check(std::strcmp(Tr(retainedStatus), TextFor(Language::Chinese, retainedStatus)) == 0,
        "returning to the original language failed", 0, 0);

    // 非法编号不能越界。缺失消息为空，非法语言用英语；语言设置器拒绝非法语言值。
    Check(std::strcmp(TextFor(Language::Chinese, Text::Count), "") == 0, "Count read out of bounds", 0, 0);
    Check(std::strcmp(TextFor(Language::English, static_cast<Text>(0xFFFF)), "") == 0,
        "invalid text ID read out of bounds", 0, 0);
    Check(std::strcmp(TextFor(static_cast<Language>(0xFF), Text::Close),
        TextFor(Language::English, Text::Close)) == 0, "invalid language fallback changed", 0, 0);
    SetDisplayLanguage(static_cast<Language>(0xFF));
    Check(CurrentLanguage() == Language::Chinese, "invalid display language was accepted", 0, 0);

    // 使用非 ASCII 的版本/角色占位值验证格式后的 UTF-8；这里不是任何游戏角色译名。
    for (unsigned language = 0; language < kLanguageCount; ++language) {
        char formatted[256]{};
        const auto locale = static_cast<Language>(language);
        const int count = std::snprintf(formatted, sizeof(formatted),
            TextFor(locale, Text::FixedMemberFormat), 3u, "测试用名称 Ω");
        Check(count > 0 && static_cast<size_t>(count) < sizeof(formatted) && ValidUtf8(formatted),
            "formatted native-name placeholder is invalid", 0, language);
        Check(std::strstr(formatted, "3") && std::strstr(formatted, "测试用名称 Ω"),
            "formatted identity lost an argument", 0, language);
    }

    // 解析器自身必须拒绝危险或未知格式，避免测试对新增错误语法失去约束。
    Check(FormatArguments("%u / %s / %%") == "us", "format parser positive fixture failed", 0, 0);
    Check(FormatArguments("%n") == "!invalid-format!", "format parser accepted %n", 0, 0);
    Check(FormatArguments("dangling %") == "!invalid-format!", "format parser accepted trailing %", 0, 0);
    Check(!ValidUtf8("\xC0\xAF") && !ValidUtf8("\xED\xA0\x80") && !ValidUtf8("\xF4\x90\x80\x80"),
        "UTF-8 parser accepted invalid sequences", 0, 0);
    std::printf("UI localization: %zu texts x %u languages; format, UTF-8 and live selection: %u failures\n",
        static_cast<size_t>(Text::Count), kLanguageCount, failures);
    return failures ? 1 : 0;
}
