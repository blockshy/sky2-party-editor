// 独立 ASI 的合作输入协议：共享所有权与同一条游戏 XInput IAT 调用的捕获结果。
// 不绕过第三方映射，不枚举另一套设备，也不要求额外加载器或 DLL。
#pragma once
#include <Windows.h>
#include <Xinput.h>
#include <cstdint>
namespace sky2solo {
bool IsPhysicalVirtualKey(unsigned key) noexcept;
bool FrameHealthy(uint64_t lastSuccess, uint64_t now) noexcept;
uint32_t InputOwner() noexcept;
class PresentInstallGuard {
    HANDLE mutex_ = nullptr;
    bool locked_ = false;
public:
    PresentInstallGuard() noexcept;
    ~PresentInstallGuard() noexcept;
    explicit operator bool() const noexcept { return locked_; }
    PresentInstallGuard(const PresentInstallGuard&) = delete;
    PresentInstallGuard& operator=(const PresentInstallGuard&) = delete;
};
class InputLease {
    uint32_t token_;
public:
    explicit InputLease(uint32_t token) noexcept : token_(token) {}
    // 显式打开窗口时转交输入；失去所有权的旧窗口应关闭并保留其页面/位置。
    bool Claim() noexcept;
    bool Owns() const noexcept;
    void Release() noexcept;
};
class GamepadCall {
    DWORD slot_ = TLS_OUT_OF_INDEXES;
    uintptr_t previous_ = 0;
    bool outer_ = false;
public:
    GamepadCall() noexcept;
    ~GamepadCall() noexcept;
    GamepadCall(const GamepadCall&) = delete;
    GamepadCall& operator=(const GamepadCall&) = delete;
    bool Outermost() const noexcept { return outer_; }
    // 每层只提出隔离请求。只有最外层在所有观察者返回后才允许修改结果，
    // 因此内层窗口仍能看到完整的打开组合与导航轴，不受加载顺序影响。
    void RequestCapture(bool capture) noexcept;
    bool CaptureRequested() const noexcept;
    void RequestViewSuppression(bool suppress = true) noexcept;
    void RequestViewReplay(bool replay = true) noexcept;
    // 最外层完成所有观察后调用：模态捕获优先，否则合并 View 前缀延迟与补发。
    // 调用者应把 View+任意其他操作都标记为已组成组合，避免别人的组合被补发。
    void Filter(XINPUT_GAMEPAD& pad) const noexcept;
};
}
