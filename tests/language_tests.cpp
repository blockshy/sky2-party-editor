// 验证真实原生八种语言枚举、文字选择和失效指针处理；不启动游戏、不读取真实存档。
#include "game_language.h"
#include <cstdio>
#include <cstring>
#include <limits>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

using namespace sky2party;

int main() {
    unsigned failures = 0, assertions = 0;
    const auto check = [&](bool condition, const char* description) {
        ++assertions;
        if (!condition) { std::printf("FAIL: %s\n", description); ++failures; }
    };
    const Language expected[] = {
        Language::Japanese, Language::English, Language::German, Language::French,
        Language::Spanish, Language::TraditionalChinese, Language::Chinese, Language::Korean
    };
    for (unsigned native = 0; native < 8; ++native) {
        auto result = Language::Japanese;
        check(ResolveNativeTextLanguage(static_cast<uint8_t>(native), result) && result == expected[native],
              "原生 0..7 语言编号逐一对应正确的界面语言，不合并回退");
    }
    for (unsigned native = 8; native <= 255; ++native) {
        auto result = Language::Japanese;
        check(!ResolveNativeTextLanguage(static_cast<uint8_t>(native), result) && result == Language::Japanese,
              "原生未知编号不覆盖上一有效语言");
    }
    const char* labels[] = {"简体", "日本語", "English", "繁體", "Deutsch", "Français", "Español", "한국어"};
    for (unsigned index = 0; index < 8; ++index) {
        const auto language = static_cast<Language>(index);
        check(std::strcmp(LocalizeFor(language, labels[0], labels[1], labels[2], labels[3],
                                    labels[4], labels[5], labels[6], labels[7]), labels[index]) == 0,
              "八种文字选择与 sc/jp/en/tc/de/fr/es/ko 显示枚举顺序一致");
        SetDisplayLanguage(language);
        check(CurrentLanguage() == language, "八种有效显示枚举均可独立切换");
    }
    // 这些是接口的防御分支，不表示正式八语文案允许缺项；正式资源另有完整性检查。
    check(std::strcmp(LocalizeFor(static_cast<Language>(255), "中", "日", "English"), "English") == 0,
          "非法显示枚举安全使用明确提供的英文，不越界访问文本数组");
    check(std::strcmp(LocalizeFor(Language::German, "中", "日", "English", "繁", ""), "English") == 0,
          "单个文本为空时防御性使用英文");
    check(std::strcmp(LocalizeFor(Language::Korean, "中", "日", nullptr), "中") == 0,
          "目标文本与英文均缺失时保留明确的中文原文");
    check(std::strcmp(LocalizeFor(Language::French, nullptr, nullptr, nullptr), "") == 0,
          "全部文本缺失时返回安全空串，不返回空指针");
    SetDisplayLanguage(Language::English);
    for (unsigned invalid = 8; invalid <= 255; ++invalid) {
        SetDisplayLanguage(static_cast<Language>(invalid));
        check(CurrentLanguage() == Language::English, "无效显示枚举不覆盖已选语言");
    }

#ifdef _WIN32
    // 使用私有虚拟内存复现全局指针和设置对象，验证读取层，而非重复真实游戏的流程。
    constexpr size_t managerRva = 0xC60E50;
    constexpr size_t languageOffset = 0x623A31;
    const auto image = static_cast<unsigned char*>(VirtualAlloc(nullptr, managerRva + sizeof(uintptr_t),
                                                               MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    const auto manager = static_cast<unsigned char*>(VirtualAlloc(nullptr, languageOffset + 2,
                                                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(image && manager, "分配只供测试使用的原生内存布局");
    if (image && manager) {
        const auto imageBase = reinterpret_cast<uintptr_t>(image);
        const auto managerBase = reinterpret_cast<uintptr_t>(manager);
        std::memcpy(image + managerRva, &managerBase, sizeof(managerBase));
        manager[languageOffset] = 6;
        InitializeGameLanguage(imageBase);
        check(CurrentLanguage() == Language::Chinese, "读取到简体中文后自动选择中文");
        manager[languageOffset] = 0;
        InitializeGameLanguage(imageBase);
        check(CurrentLanguage() == Language::Japanese, "原生值 0 是有效日语，不能误判成加载态");
        manager[languageOffset] = 1;
        InitializeGameLanguage(imageBase);
        check(CurrentLanguage() == Language::English, "读取到英文后自动选择英文");
        for (unsigned native = 0; native < 8; ++native) {
            manager[languageOffset] = static_cast<uint8_t>(native);
            // +0x623A32 的相邻语音字段故意与文字语言不同；覆盖全部八种文字与
            // 八种语音组合，证明检测器没有混淆两字节或读取系统区域/显示语言。
            for (unsigned voice = 0; voice < 8; ++voice) {
                manager[languageOffset + 1] = static_cast<uint8_t>(voice);
                InitializeGameLanguage(imageBase);
                check(CurrentLanguage() == expected[native], "八种原生文字语言独立于相邻语音语言");
            }
        }
        manager[languageOffset] = 1;
        InitializeGameLanguage(imageBase);
        manager[languageOffset] = 255;
        InitializeGameLanguage(imageBase);
        check(CurrentLanguage() == Language::English, "无效字节保持上一次读取成功的英文");
        const uintptr_t nullManager = 0;
        std::memcpy(image + managerRva, &nullManager, sizeof(nullManager));
        InitializeGameLanguage(imageBase);
        check(CurrentLanguage() == Language::English, "空对象指针不当作日语，也不回退中文");
        const uintptr_t overflowManager = (std::numeric_limits<uintptr_t>::max)() - 1;
        std::memcpy(image + managerRva, &overflowManager, sizeof(overflowManager));
        InitializeGameLanguage(imageBase);
        check(CurrentLanguage() == Language::English, "对象地址加偏移溢出时停止读取");
        std::memcpy(image + managerRva, &managerBase, sizeof(managerBase));
        DWORD oldProtection = 0;
        manager[languageOffset] = 5;
        check(VirtualProtect(manager, languageOffset + 2, PAGE_READONLY, &oldProtection) != 0,
              "设置对象仅允许读取，不能以修改游戏设置来切换Mod语言");
        InitializeGameLanguage(imageBase);
        check(CurrentLanguage() == Language::TraditionalChinese, "只读页面仍可正常识别繁体中文");
        check(VirtualProtect(manager, languageOffset + 2, PAGE_NOACCESS, &oldProtection) != 0,
              "模拟游戏对象暂时不可读取");
        InitializeGameLanguage(imageBase);
        check(CurrentLanguage() == Language::TraditionalChinese, "不可读对象保持旧语言且不产生访问异常");
        // 全局指针所在页不可读同样必须保留旧值，不能只保护第二次对象读取。
        check(VirtualProtect(image, managerRva + sizeof(uintptr_t), PAGE_NOACCESS, &oldProtection) != 0,
              "模拟宿主全局指针区域不可读");
        InitializeGameLanguage(imageBase);
        check(CurrentLanguage() == Language::TraditionalChinese, "不可读全局指针保持旧语言");
        InitializeGameLanguage(0);
    }
    if (manager) VirtualFree(manager, 0, MEM_RELEASE);
    if (image) VirtualFree(image, 0, MEM_RELEASE);
    SetDisplayLanguage(Language::English);
    InitializeGameLanguage(1);
    RefreshGameLanguage();
    check(CurrentLanguage() == Language::English, "无效模块基址不产生读取或语言变化");
    InitializeGameLanguage((std::numeric_limits<uintptr_t>::max)());
    check(CurrentLanguage() == Language::English, "模块基址加偏移溢出时停止检测");
#endif
    std::printf("language checks: %u assertions, %s\n", assertions, failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
