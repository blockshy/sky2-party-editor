// 每个 ASI 都静态包含此实现；命名映射把 TLS 槽与窗口所有权统一到当前进程。
// 映射不含游戏数据/路径，不写磁盘。模块在游戏进程存活期间不动态卸载，故
// 映射句柄及唯一 TLS 槽保留至进程结束，避免在途 IAT 回调使用失效索引。
#include "input.h"
#include <cstdio>
namespace sky2solo {
namespace {
struct SharedState { volatile LONG owner; DWORD tls; DWORD magic; DWORD size; };
constexpr DWORD magic = 0x534F4C31; // SOLO 协议第 1 版，名称也包含版本，禁止误接其他布局。
constexpr uintptr_t captureBit = uintptr_t(1) << (sizeof(uintptr_t) * 8 - 1);
constexpr uintptr_t suppressViewBit = uintptr_t(1) << 17;
constexpr uintptr_t replayViewBit = uintptr_t(1) << 18;
constexpr uintptr_t requestBits = captureBit | suppressViewBit | replayViewBit;
SharedState* Shared() noexcept {
    static SharedState* shared = []() noexcept -> SharedState* {
        wchar_t name[96]{}, mutexName[96]{};
        swprintf_s(name, L"Local\\Sky2Standalone.Input.v1.%lu", GetCurrentProcessId());
        swprintf_s(mutexName, L"Local\\Sky2Standalone.Input.Init.v1.%lu", GetCurrentProcessId());
        HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName);
        if (!mutex) return nullptr;
        const DWORD wait = WaitForSingleObject(mutex, 1000);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) { CloseHandle(mutex); return nullptr; }
        HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedState), name);
        auto* value = mapping ? static_cast<SharedState*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState))) : nullptr;
        if (value && value->magic == 0) {
            const DWORD slot = TlsAlloc();
            if (slot != TLS_OUT_OF_INDEXES) { value->tls = slot; value->size = sizeof(SharedState); value->owner = 0; value->magic = magic; }
        }
        const bool valid = value && value->magic == magic && value->size == sizeof(SharedState) && value->tls != TLS_OUT_OF_INDEXES;
        ReleaseMutex(mutex); CloseHandle(mutex);
        if (!valid) { if (value) UnmapViewOfFile(value); if (mapping) CloseHandle(mapping); return nullptr; }
        // 成功后有意保留映射引用，防止唯一创建者退出局部初始化作用域后映射消失。
        return value;
    }();
    return shared;
}
}
bool IsPhysicalVirtualKey(unsigned key) noexcept {
    return (key >= 0x01 && key <= 0x06) || key == 0x08 || key == 0x09 || key == 0x0C || key == 0x0D ||
        (key >= 0x10 && key <= 0x39) || (key >= 0x41 && key <= 0x5D) || (key >= 0x5F && key <= 0x87) ||
        (key >= 0x90 && key <= 0x96) || (key >= 0xA0 && key <= 0xB7) || (key >= 0xBA && key <= 0xC0) ||
        (key >= 0xDB && key <= 0xDF) || (key >= 0xE1 && key <= 0xE4) || key == 0xE6 ||
        (key >= 0xE9 && key <= 0xFB) || key == 0xFD || key == 0xFE;
}
bool FrameHealthy(uint64_t lastSuccess, uint64_t now) noexcept {
    return lastSuccess && now >= lastSuccess && now - lastSuccess <= 500;
}
uint32_t InputOwner() noexcept {
    auto* shared = Shared(); return shared ? static_cast<uint32_t>(InterlockedCompareExchange(&shared->owner, 0, 0)) : 0;
}
PresentInstallGuard::PresentInstallGuard() noexcept {
    wchar_t name[96]{};
    swprintf_s(name,L"Local\\Sky2Standalone.Present.Install.v1.%lu",GetCurrentProcessId());
    mutex_=CreateMutexW(nullptr,FALSE,name);
    if(mutex_){const DWORD result=WaitForSingleObject(mutex_,5000);locked_=result==WAIT_OBJECT_0||result==WAIT_ABANDONED;}
}
PresentInstallGuard::~PresentInstallGuard() noexcept {
    if(locked_)ReleaseMutex(mutex_);if(mutex_)CloseHandle(mutex_);
}
bool InputLease::Claim() noexcept {
    auto* shared = Shared(); if (!shared || !token_) return false;
    InterlockedExchange(&shared->owner, static_cast<LONG>(token_)); return true;
}
bool InputLease::Owns() const noexcept {
    auto* shared = Shared(); return shared && token_ && InterlockedCompareExchange(&shared->owner, 0, 0) == static_cast<LONG>(token_);
}
void InputLease::Release() noexcept {
    if (auto* shared = Shared()) InterlockedCompareExchange(&shared->owner, 0, static_cast<LONG>(token_));
}
GamepadCall::GamepadCall() noexcept {
    auto* shared = Shared(); if (!shared) return;
    slot_ = shared->tls; previous_ = reinterpret_cast<uintptr_t>(TlsGetValue(slot_));
    const uintptr_t depth = previous_ & ~requestBits;
    if (depth >= 1024) { slot_ = TLS_OUT_OF_INDEXES; return; }
    outer_ = depth == 0;
    const uintptr_t next = (depth + 1) | (outer_ ? 0 : previous_ & requestBits);
    if (!TlsSetValue(slot_, reinterpret_cast<void*>(next))) { slot_ = TLS_OUT_OF_INDEXES; outer_ = false; }
}
GamepadCall::~GamepadCall() noexcept {
    if (slot_ == TLS_OUT_OF_INDEXES) return;
    const uintptr_t current = reinterpret_cast<uintptr_t>(TlsGetValue(slot_));
    // 内层返回时将捕获位向外合并，最外返回清空，不污染下一次设备轮询。
    const uintptr_t restored = outer_ ? previous_ : previous_ | (current & requestBits);
    TlsSetValue(slot_, reinterpret_cast<void*>(restored));
}
void GamepadCall::RequestCapture(bool capture) noexcept {
    if (capture && slot_ != TLS_OUT_OF_INDEXES) {
        const auto current = reinterpret_cast<uintptr_t>(TlsGetValue(slot_));
        TlsSetValue(slot_, reinterpret_cast<void*>(current | captureBit));
    }
}
bool GamepadCall::CaptureRequested() const noexcept {
    return slot_ != TLS_OUT_OF_INDEXES && (reinterpret_cast<uintptr_t>(TlsGetValue(slot_)) & captureBit) != 0;
}
void GamepadCall::RequestViewSuppression(bool suppress) noexcept {
    if (suppress && slot_ != TLS_OUT_OF_INDEXES) {
        const auto current = reinterpret_cast<uintptr_t>(TlsGetValue(slot_));
        TlsSetValue(slot_, reinterpret_cast<void*>(current | suppressViewBit));
    }
}
void GamepadCall::RequestViewReplay(bool replay) noexcept {
    if (replay && slot_ != TLS_OUT_OF_INDEXES) {
        const auto current = reinterpret_cast<uintptr_t>(TlsGetValue(slot_));
        TlsSetValue(slot_, reinterpret_cast<void*>(current | replayViewBit));
    }
}
void GamepadCall::Filter(XINPUT_GAMEPAD& pad) const noexcept {
    if (!outer_ || slot_ == TLS_OUT_OF_INDEXES) return;
    const auto current = reinterpret_cast<uintptr_t>(TlsGetValue(slot_));
    if (current & captureBit) { pad = {}; return; }
    if (current & suppressViewBit) pad.wButtons &= ~XINPUT_GAMEPAD_BACK;
    if (current & replayViewBit) pad.wButtons |= XINPUT_GAMEPAD_BACK;
}
}
