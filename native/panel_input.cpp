// 独立面板的合作 IAT 输入层：每层观察同一条游戏调用链返回的样本，
// 仅最外层统一过滤；不挂 XInput 导出，不绕过 Steam Input，不伪造其他 Mod 按键。
#include "panel_input.h"
#include "panel_input_policy.h"
#include "runtime.h"
#include "ui_text.h"
#include <Xinput.h>
#include "standalone_ui/input.h"
#include "standalone_ui/hotkeys.h"
#include <atomic>
#include <cstring>

namespace sky2party {
namespace {
using StateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using AsyncFn = SHORT(WINAPI*)(int);
StateFn nextState = nullptr;
AsyncFn nextAsync = nullptr;
uintptr_t gameBase = 0;
std::atomic<HWND> inputWindow{nullptr};
std::atomic<WNDPROC> nextWindow{nullptr};
std::atomic<PanelWindowMessage> windowCallback{nullptr};
std::atomic<bool> open{false}, controller{false}, keyboardTail{false};
std::atomic<uint64_t> lastSuccessfulFrame{0}, inputGeneration{1};
std::atomic<bool> captureAvailable{false}, resetPending{true};
std::atomic<bool> guiKeyboardArmed{false};
std::atomic<bool> windowFocused{true};
std::atomic<LPARAM> lastMousePosition{static_cast<LPARAM>(-1)};
std::atomic<int> navigationDevice{-1};
uint64_t keyboardGeneration = 0, padGeneration[4]{};
panelinput::Pad rawPads[4]{};
uint64_t rawPadTimes[4]{};
std::atomic<uint32_t> actions{0}, readyDevices{0};
// 只记录真正交给本面板后端的鼠标按下，用于关闭后配对转发释放；
// 不能把游戏自己的隐藏期 mouse-up 送给 ImGui，否则可能释放游戏的鼠标捕获。
std::atomic<uint32_t> panelMouseButtons{0};
SRWLOCK padLock = SRWLOCK_INIT;
// 窗口意图、捕获恢复和故障释放是一个短事务。此锁内不取 padLock 或
// renderLock，因此游戏输入线程持 padLock 后切窗也不会与 Present 反向等待。
SRWLOCK lifecycleLock = SRWLOCK_INIT;
struct LifecycleGuard {
    LifecycleGuard() noexcept { AcquireSRWLockExclusive(&lifecycleLock); }
    ~LifecycleGuard() { ReleaseSRWLockExclusive(&lifecycleLock); }
};
panelinput::SoloPadPolicy pads[4];
sky2solo::InputLease inputLease{0x50525459}; // PRTY：进程内唯一的队伍窗口所有者。
std::atomic<uint64_t> captureGeneration{0};
uint64_t padCaptureGeneration[4]{};
bool chordHeld[4]{};
uint64_t chordSeen[4]{}, chordGeneration = 0;
sky2solo::HotkeyKeyboardTracker keyboard;
std::atomic<uint64_t> bindingRevision{0};
XINPUT_GAMEPAD outputs[4]{};
DWORD packets[4]{};
DWORD WINAPI GameGetState(DWORD index, XINPUT_STATE* state) noexcept;

bool Foreground() noexcept {
    const auto window = inputWindow.load();
    return window && windowFocused.load() && GetForegroundWindow() == window;
}
void ReleaseCaptureLocked() noexcept {
    // 这里只更新原子状态，不进入 ImGui/业务代码，也不在 XInput 的 padLock
    // 内再取渲染锁。下一帧消费世代变化，统一重武装导航和取消角色确认。
    captureAvailable.store(false);
    inputLease.Release();
    keyboardTail.store(false);
    actions.store(0);
    resetPending.store(true);
    guiKeyboardArmed.store(false);
    inputGeneration.fetch_add(1);
}
void ReleaseCapture() noexcept { LifecycleGuard guard; ReleaseCaptureLocked(); }
sky2solo::HotkeySnapshot PanelBindings() noexcept {
    auto snapshot = sky2solo::ReadHotkeys();
    auto observed = bindingRevision.load();
    while (snapshot.revision > observed) {
        if (bindingRevision.compare_exchange_weak(observed, snapshot.revision)) {
            // 改绑使键盘、四槽手柄及导航缓存同时失效；窗口保持可见，但任何
            // 已按住的新旧组合都须先松开，不能把点击“保存”变成开关窗操作。
            inputGeneration.fetch_add(1); captureGeneration.fetch_add(1);
            resetPending.store(true); guiKeyboardArmed.store(false); actions.store(0);
            break;
        }
    }
    if (snapshot.revision < observed) snapshot = sky2solo::ReadHotkeys();
    return snapshot;
}
bool InputAvailable() noexcept {
    LifecycleGuard guard;
    const bool available = Foreground() && panelinput::FrameHealthy(lastSuccessfulFrame.load(), GetTickCount64());
    if (!available) {
        if (captureAvailable.load()) ReleaseCaptureLocked();
        return false;
    }
    if (open.load() && !inputLease.Owns()) {
        if (sky2solo::InputOwner()) {
            open.store(false); resetPending.store(true); keyboardTail.store(false);
        } else inputLease.Claim();
    }
    if (!captureAvailable.exchange(true)) {
        // 恢复前台或首个健康帧不能消费原先按住的开关键/A/B。
        inputGeneration.fetch_add(1);
        resetPending.store(true);
    }
    return true;
}
panelinput::Pad FromNative(const XINPUT_GAMEPAD& p) noexcept {
    return {p.wButtons, p.bLeftTrigger, p.bRightTrigger, p.sThumbLX, p.sThumbLY, p.sThumbRX, p.sThumbRY};
}
XINPUT_GAMEPAD ToNative(const panelinput::Pad& p) noexcept {
    return {p.buttons, p.lt, p.rt, p.lx, p.ly, p.rx, p.ry};
}
void ApplyActions(uint32_t value) noexcept {
    if (value & panelinput::Toggle) SetPanelOpen(!open.load());
    else if (value & panelinput::Close) SetPanelOpen(false);
    actions.fetch_or(value);
}

DWORD WINAPI GameGetState(DWORD index, XINPUT_STATE* state) noexcept {
    // 构造次序必须在 nextState 前，内层合作 ASI 只聚合请求且不改 state，
    // 所以 Steam Input 映射后的同一个样本会依次被全部观察者读取。
    sky2solo::GamepadCall call;
    const auto error = nextState(index, state);
    if (index >= 4 || !state) return error;
    const auto bindings = PanelBindings();
    const auto binding = bindings.count ? bindings.bindings[0] : sky2solo::HotkeyBinding{};
    const bool available = InputAvailable();
    AcquireSRWLockExclusive(&padLock);
    if (error == ERROR_SUCCESS) {
        const auto generation = inputGeneration.load();
        if (padGeneration[index] != generation) {
            pads[index].Reset(); padGeneration[index] = generation;
        }
        if (chordGeneration != generation) {
            for (auto& held : chordHeld) held = false;
            chordGeneration = generation;
        }
        const auto capture = captureGeneration.load();
        if (padCaptureGeneration[index] != capture) {
            pads[index].BeginCaptureTail(); padCaptureGeneration[index] = capture;
        }
        readyDevices.fetch_or(1u << index);
        rawPads[index] = FromNative(state->Gamepad); rawPadTimes[index] = GetTickCount64();
        int absent = -1;
        navigationDevice.compare_exchange_strong(absent, static_cast<int>(index));
        const bool owns = inputLease.Owns();
        const auto result = pads[index].Update(rawPads[index], open.load() && owns, available,
            sky2solo::InputOwner() != 0 && !owns, binding.pad);
        if (result.activity && available && !panelinput::Neutral(rawPads[index])) {
            navigationDevice.store(static_cast<int>(index)); controller.store(true);
        }
        // 选择最近实际操作的槽，允许槽0仅连接而玩家使用槽1。组合锁存与
        // 导航槽分开：镜像设备换槽仍属于同一次配置组合，不能二次切换窗口。
        const auto now = GetTickCount64();
        bool anyChord = false;
        for (unsigned slot = 0; slot < 4; ++slot)
            anyChord |= chordHeld[slot] && now - chordSeen[slot] <= 250;
        chordHeld[index] = available && sky2solo::HotkeyPadHeld(binding, rawPads[index].buttons);
        chordSeen[index] = now;
        if (result.toggle && !anyChord) ApplyActions(panelinput::Toggle);
        // 关闭后的尾部仍应隔离，但另一个窗口取得所有权后由它负责输入。
        // View 前缀属于共同协议，所有窗口均延迟单按、识别组合后禁止补发。
        const auto owner = sky2solo::InputOwner();
        call.RequestCapture(available && result.capture && (!owner || inputLease.Owns()));
        call.RequestViewSuppression(available && (rawPads[index].buttons & panelinput::View));
        call.RequestViewReplay(available && result.replayView);
    } else {
        pads[index].Reset(); readyDevices.fetch_and(~(1u << index));
        rawPads[index] = {}; rawPadTimes[index] = 0;
        chordHeld[index] = false; chordSeen[index] = 0;
        int lost = static_cast<int>(index); navigationDevice.compare_exchange_strong(lost, -1);
        if (!readyDevices.load()) controller.store(false);
    }
    ReleaseSRWLockExclusive(&padLock);
    if (error == ERROR_SUCCESS) {
        // 内层 Filter 是无操作；最外层才零化或补发 View，并更新输出包号。
        call.Filter(state->Gamepad);
        if (call.Outermost()) {
            AcquireSRWLockExclusive(&padLock);
            if (std::memcmp(&outputs[index], &state->Gamepad, sizeof(state->Gamepad))) {
                outputs[index] = state->Gamepad; ++packets[index];
            }
            state->dwPacketNumber = packets[index];
            ReleaseSRWLockExclusive(&padLock);
        }
    }
    return error;
}

SHORT WINAPI GameAsyncKey(int key) noexcept {
    const SHORT value = nextAsync(key);
    const auto bindings = PanelBindings();
    // 仅在游戏询问本模块当前主键时才采样修饰键，避免每个无关 VK 查询都
    // 再遍历键盘。实体状态来自原生 user32，不依赖其它 Mod 已过滤的结果。
    const bool shortcut = bindings.count && key == bindings.bindings[0].key &&
        sky2solo::HotkeyKeyHeld(bindings.bindings[0], sky2solo::ReadHotkeyKeyboardState());
    if (InputAvailable() && ((open.load() && inputLease.Owns()) ||
        (keyboardTail.load() && !sky2solo::InputOwner()) || shortcut)) return 0;
    return value;
}
bool InputMessage(UINT message) noexcept {
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
        (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_INPUT;
}
uint32_t MouseButton(UINT message, WPARAM wparam, bool down) noexcept {
    if (message == static_cast<UINT>(down ? WM_LBUTTONDOWN : WM_LBUTTONUP) || (down && message == WM_LBUTTONDBLCLK)) return 1;
    if (message == static_cast<UINT>(down ? WM_RBUTTONDOWN : WM_RBUTTONUP) || (down && message == WM_RBUTTONDBLCLK)) return 2;
    if (message == static_cast<UINT>(down ? WM_MBUTTONDOWN : WM_MBUTTONUP) || (down && message == WM_MBUTTONDBLCLK)) return 4;
    if (message == static_cast<UINT>(down ? WM_XBUTTONDOWN : WM_XBUTTONUP) || (down && message == WM_XBUTTONDBLCLK))
        return HIWORD(wparam) == XBUTTON1 ? 8 : 16;
    return 0;
}
LRESULT CALLBACK PanelWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_SETFOCUS) windowFocused.store(true);
    const bool foreground = GetForegroundWindow() == window;
    if (foreground && ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(lparam & (1LL << 30))))
        controller.store(false);
    if (foreground && (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MOUSEWHEEL))
        controller.store(false);
    if (message == WM_MOUSEMOVE && lastMousePosition.exchange(lparam) != lparam && foreground)
        controller.store(false); // 实际鼠标移动也切换提示；静止鼠标消息不能抢回设备身份。
    if (message == WM_KILLFOCUS) {
        // 失焦仅放弃输入和确认，保留窗口位置、当前页和只读内容。
        // GetForegroundWindow 可能在 WM_KILLFOCUS 回调内仍返回旧窗口，消息
        // 屏障因此必须先发布，不能在本次过程的尾部立即把捕获重新恢复。
        windowFocused.store(false);
        ReleaseCapture(); controller.store(false);
        const auto held = panelMouseButtons.exchange(0);
        if (auto callback = windowCallback.load(); callback && held) {
            // 失焦后真实释放可能递送给别的窗口，只对本面板见过的按下补齐后端释放。
            // 这是自有后端状态清理，不向游戏或系统发送/模拟鼠标输入。
            const UINT messages[] = {WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP, WM_XBUTTONUP};
            for (uint32_t index = 0; index < 5; ++index) if (held & (1u << index))
                callback(window, messages[index], index < 3 ? 0 : MAKEWPARAM(0, index == 3 ? XBUTTON1 : XBUTTON2), 0);
        }
    }
    const auto released = MouseButton(message, wparam, false);
    const bool pairedMouseUp = (panelMouseButtons.load() & released) != 0;
    const bool available = InputAvailable();
    const bool interactive = available && inputLease.Owns();
    if (auto callback = windowCallback.load(); callback &&
        ((interactive && open.load() && (!InputMessage(message) || guiKeyboardArmed.load())) ||
            message == WM_KILLFOCUS || pairedMouseUp)) {
        panelMouseButtons.fetch_or(MouseButton(message, wparam, true));
        panelMouseButtons.fetch_and(~released);
        callback(window, message, wparam, lparam);
    }
    // 对应的按下从未交给游戏，配对释放也在此消费；Win32 后端因此能清理
    // MouseButtonsDown 并 ReleaseCapture，而不依赖已经清空的 ImGui IO 状态。
    if (pairedMouseUp) return 0;
    if (available && InputMessage(message) && ((open.load() && inputLease.Owns()) ||
        (keyboardTail.load() && !sky2solo::InputOwner()))) {
        // WM_INPUT 的句柄由系统管理，交给 DefWindowProc 完成必要清理，但不递送游戏处理器。
        return message == WM_INPUT ? DefWindowProcW(window, message, wparam, lparam) : 0;
    }
    return CallWindowProcW(nextWindow.load(), window, message, wparam, lparam);
}
bool ReplaceSlot(void** slot, void* replacement, void* expected) noexcept {
    if (!expected) return false;
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    // expected 必须已经写入转发函数。其他插件并发改写时宁可安装失败，不能让
    // 新入口先开始执行、随后才补写其下一层，否则会出现瞬时跳过或循环调用。
    const bool changed = InterlockedCompareExchangePointer(slot, replacement, expected) == expected;
    DWORD ignored = 0; VirtualProtect(slot, sizeof(void*), protection, &ignored);
    return changed;
}
}

bool InstallPanelInput(uintptr_t executableBase) noexcept {
    gameBase = executableBase;
    const unsigned char call[] = {0xFF, 0x15, 0x3A, 0x61, 0x21, 0x00};
    if (!gameBase || std::memcmp(reinterpret_cast<void*>(gameBase + 0x6A55A8), call, sizeof(call))) return false;
    // 必须保留安装时的前一层，既不跳过 Steam 映射，也不重挂形成 IAT 环。
    auto stateSlot = reinterpret_cast<void**>(gameBase + 0x8BB6E8);
    nextState = reinterpret_cast<StateFn>(*stateSlot);
    if (!ReplaceSlot(stateSlot, reinterpret_cast<void*>(&GameGetState), reinterpret_cast<void*>(nextState))) return false;
    auto keyboardSlot = reinterpret_cast<void**>(gameBase + 0x8BB5D8);
    nextAsync = reinterpret_cast<AsyncFn>(*keyboardSlot);
    if (!ReplaceSlot(keyboardSlot, reinterpret_cast<void*>(&GameAsyncKey), reinterpret_cast<void*>(nextAsync))) {
        // 仅回退自己仍占有的 IAT 项；若后来者已经串联进来，则保留有效的透传函数。
        ReplaceSlot(stateSlot, reinterpret_cast<void*>(nextState), reinterpret_cast<void*>(&GameGetState));
        return false;
    }
    Log("Panel input installed: cooperative game IAT, shared capture scope, no XInput export hooks.");
    return true;
}

void AttachPanelWindow(HWND window, PanelWindowMessage callback) noexcept {
    windowCallback.store(callback);
    if (inputWindow.load() == window && nextWindow.load()) return;
    if (nextWindow.load()) { Log("Panel input: window changed; retained original chain."); return; }
    inputWindow.store(window);
    const auto previous = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    if (!previous) return;
    nextWindow.store(previous);
    SetLastError(0);
    const auto result = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&PanelWindowProc));
    if (!result && GetLastError()) { nextWindow.store(nullptr); inputWindow.store(nullptr); }
    else if (result) nextWindow.store(reinterpret_cast<WNDPROC>(result));
}

void PumpPanelKeyboard() noexcept {
    const auto bindings = PanelBindings();
    const bool available = InputAvailable();
    const auto generation = inputGeneration.load();
    if (keyboardGeneration != generation) { keyboard = {}; keyboardGeneration = generation; }
    const auto physical = sky2solo::ReadHotkeyKeyboardState();
    const auto events = keyboard.Update(bindings, physical, available);
    // Main 导航、Esc/B 关闭由共同壳处理；这里只有全局开关键，避免一按两次。
    // 共同状态机按真实主键边沿及精确修饰键匹配，松开修饰键不补触发。
    if (events & 1u) {
        controller.store(false); ApplyActions(panelinput::Toggle);
    }
    // 后台按住 Enter/鼠标再回前台时，Win32 可能先递送重复 key-down；只清
    // ImGui 旧键并不足够。等待真实物理键全部松开，再接受本窗口的 GUI 消息。
    if (available && open.load() && !guiKeyboardArmed.load() &&
        !panelinput::AnyPhysicalKeyDown([](int key) { return GetAsyncKeyState(key); })) guiKeyboardArmed.store(true);
    if (!open.load() && keyboardTail.load()) {
        const bool held = panelinput::AnyPhysicalKeyDown([](int key) { return GetAsyncKeyState(key); });
        if (!held) keyboardTail.store(false);
    }
}
uint32_t ConsumePanelActions() noexcept { return actions.exchange(0); }
bool PanelOpen() noexcept { return open.load(); }
void SetPanelOpen(bool value) noexcept {
    LifecycleGuard guard;
    if (value && !inputLease.Claim()) return;
    const bool previous = open.exchange(value);
    if (previous != value) {
        captureGeneration.fetch_add(1); resetPending.store(true); guiKeyboardArmed.store(false);
    }
    if (!value) {
        inputLease.Release();
        if (previous) keyboardTail.store(true);
    }
}
bool PanelInteractive() noexcept { return InputAvailable() && open.load() && inputLease.Owns(); }
void SetPanelFrameHealth(bool healthy) noexcept {
    lastSuccessfulFrame.store(healthy ? GetTickCount64() : 0);
    if (!healthy) ReleaseCapture();
}
bool ConsumePanelReset() noexcept { return resetPending.exchange(false); }
bool ReadPanelPad(panelinput::Pad& sample) noexcept {
    sample = {};
    bool fresh = false;
    AcquireSRWLockShared(&padLock);
    const int index = navigationDevice.load();
    // 游戏暂停 XInput 采样时，不把几秒前的 A/B 或摇杆当成本帧输入。
    // 恢复后必须有本代次的新 IAT 样本。旧中立缓存不能提前武装 A/B，
    // 否则玩家在失焦期间按住的 A 会在下一次真实采样时被误认为新按下。
    if (index >= 0 && index < 4 && padGeneration[index] == inputGeneration.load() &&
        padCaptureGeneration[index] == captureGeneration.load() && GetTickCount64() - rawPadTimes[index] <= 250) {
        sample = rawPads[index]; fresh = true;
    }
    ReleaseSRWLockShared(&padLock);
    return fresh;
}
bool PanelUsingController() noexcept { return controller.load(); }
bool PanelControllerReady() noexcept { return readyDevices.load() != 0; }
const char* PanelInputStatus() noexcept {
    return PanelControllerReady() ? "" : Tr(Text::InputUnavailable);
}
}
