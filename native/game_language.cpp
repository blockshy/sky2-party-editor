// 仅适用于已通过完整哈希校验的 Steam build 25386012。
// 静态证据：语言设置窗口在 RVA 0x1869B8 取全局对象、0x1869C6 读取文本语言；
// 玩家确认选择后，RVA 0x186F6C 写回同一字节。场景资源初始化 0x02FA30 读取它，
// 并传给 0x02F330，后者按 0xAA5F30 的 jp/en/de/fr/es/tc/sc/ko 表选择语言资源。
// 相邻 +0x623A32 是语音语言，故这里有意只读取 +0x623A31。
// 这些证据已对队伍 Mod 接受的同一 EXE 完整 SHA-256 重新只读核验；独立版与
// ASI 版调用同一读取层，不引用宝箱 Mod 的模块、状态对象或导出函数。
#include "game_language.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <limits>

namespace sky2party {
namespace {
constexpr uintptr_t kSettingsManagerRva = 0xC60E50;
constexpr uintptr_t kTextLanguageOffset = 0x623A31;
constexpr ULONGLONG kRefreshIntervalMs = 250;
std::atomic<uintptr_t> g_validatedLanguageBase{0};
std::atomic<ULONGLONG> g_lastLanguageRead{0};
std::atomic_flag g_readingLanguage = ATOMIC_FLAG_INIT;

// 使用当前进程的只读系统调用，切图释放对象或页面暂时不可读时由系统返回失败。
// 不使用裸指针解引用或 VirtualQuery 后直接解引用，避免检查与读取之间的释放竞态。
template<class T> bool ReadLanguageValue(uintptr_t address, T& output) noexcept {
    if (address < 0x10000 || address > (std::numeric_limits<uintptr_t>::max)() - sizeof(T))
        return false;
    SIZE_T transferred = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                             &output, sizeof(T), &transferred) && transferred == sizeof(T);
}

// 只串行化本模块自己的短读取过程，防止多线程的旧样本覆盖刚检测到的新语言。
// 不持有游戏锁，也不等待另一个线程；重复调用直接跳过即可。
struct LanguageReadGuard {
    ~LanguageReadGuard() { g_readingLanguage.clear(std::memory_order_release); }
};
} // namespace

void InitializeGameLanguage(uintptr_t validatedBase) noexcept {
    if (validatedBase < 0x10000 ||
        validatedBase > (std::numeric_limits<uintptr_t>::max)() - kSettingsManagerRva)
        validatedBase = 0;
    g_validatedLanguageBase.store(validatedBase, std::memory_order_release);
    g_lastLanguageRead.store(0, std::memory_order_relaxed);
    RefreshGameLanguage();
}

void RefreshGameLanguage() noexcept {
    if (g_readingLanguage.test_and_set(std::memory_order_acquire)) return;
    const LanguageReadGuard guard;
    const auto base = g_validatedLanguageBase.load(std::memory_order_acquire);
    if (!base) return;
    const auto now = GetTickCount64();
    const auto previous = g_lastLanguageRead.load(std::memory_order_relaxed);
    if (previous && now - previous < kRefreshIntervalMs) return;
    g_lastLanguageRead.store(now, std::memory_order_relaxed);

    uintptr_t manager = 0, confirmedManager = 0;
    uint8_t native = 0;
    if (!ReadLanguageValue(base + kSettingsManagerRva, manager) || manager < 0x10000 ||
        manager > (std::numeric_limits<uintptr_t>::max)() - kTextLanguageOffset ||
        !ReadLanguageValue(manager + kTextLanguageOffset, native) ||
        !ReadLanguageValue(base + kSettingsManagerRva, confirmedManager) ||
        confirmedManager != manager ||
        g_validatedLanguageBase.load(std::memory_order_acquire) != base) return;

    // 0 是日语的有效编号，不能当作「未初始化」。只有对象读取失败或超出原生 0..7
    // 范围才忽略本次结果，以免读档、切图时面板突然退回默认中文。
    auto language = CurrentLanguage();
    if (ResolveNativeTextLanguage(native, language)) SetDisplayLanguage(language);
}

} // namespace sky2party
