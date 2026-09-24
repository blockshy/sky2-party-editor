// 与 Chest 的输入过滤串联：原始样本只在游戏调用经过的 XInput 导出链中观察，
// IAT 层兜底吞键。两层职责分离，不能把已经被 Chest 置零的状态冒充原始手柄输入。
#include "panel_input.h"
#include "panel_input_policy.h"
#include "runtime.h"
#include <Xinput.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>

namespace sky2party {
namespace {
using StateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using AsyncFn = SHORT(WINAPI*)(int);
StateFn nextState = nullptr, nextRaw = nullptr;
AsyncFn nextAsync = nullptr;
uintptr_t gameBase = 0;
std::atomic<HWND> inputWindow{nullptr};
std::atomic<WNDPROC> nextWindow{nullptr};
std::atomic<PanelWindowMessage> windowCallback{nullptr};
std::atomic<bool> open{false}, controller{false}, keyboardTail{false};
std::atomic<uint32_t> actions{0}, readyDevices{0};
// 只记录真正交给本面板后端的鼠标按下，用于关闭后配对转发释放；
// 不能把游戏自己的隐藏期 mouse-up 送给 ImGui，否则可能释放游戏的鼠标捕获。
std::atomic<uint32_t> panelMouseButtons{0};
SRWLOCK padLock = SRWLOCK_INIT;
panelinput::PadPolicy pads[4];
panelinput::KeyboardPolicy keyboard;
XINPUT_GAMEPAD outputs[4]{};
DWORD packets[4]{};
bool fallbackTail[4]{};
struct CallFrame {
    bool active = false, observed = false;
    DWORD index = 0;
    panelinput::Chain chain = panelinput::Chain::Unknown;
    panelinput::Result result{};
};
thread_local CallFrame frame;
DWORD WINAPI GameGetState(DWORD index, XINPUT_STATE* state) noexcept;

bool Foreground() noexcept {
    const auto window = inputWindow.load();
    return window && GetForegroundWindow() == window;
}
panelinput::Pad FromNative(const XINPUT_GAMEPAD& p) noexcept {
    return {p.wButtons, p.bLeftTrigger, p.bRightTrigger, p.sThumbLX, p.sThumbLY, p.sThumbRX, p.sThumbRY};
}
XINPUT_GAMEPAD ToNative(const panelinput::Pad& p) noexcept {
    return {p.buttons, p.lt, p.rt, p.lx, p.ly, p.rx, p.ry};
}
bool InsideModule(void* address, HMODULE module) noexcept {
    MEMORY_BASIC_INFORMATION info{};
    return module && VirtualQuery(address, &info, sizeof(info)) == sizeof(info) && info.AllocationBase == module;
}
panelinput::Chain DetectChain() noexcept {
    const auto chest = GetModuleHandleW(L"Sky2ChestTracker.asi");
    if (!gameBase) return panelinput::Chain::Unknown;
    void* current = *reinterpret_cast<void**>(gameBase + 0x8BB6E8);
    if (current == reinterpret_cast<void*>(&GameGetState)) {
        if (InsideModule(reinterpret_cast<void*>(nextState), chest)) return panelinput::Chain::ChestInside;
        // 模块已装载但过滤层尚未接入时等待，不把加载顺序当作已完成状态。
        return chest ? panelinput::Chain::Unknown : panelinput::Chain::Standalone;
    }
    if (InsideModule(current, chest)) return panelinput::Chain::ChestOutside;
    return panelinput::Chain::Unknown;
}
void ApplyActions(uint32_t value) noexcept {
    if (value & panelinput::Toggle) SetPanelOpen(!open.load());
    else if (value & panelinput::Close) SetPanelOpen(false);
    actions.fetch_or(value);
}

DWORD WINAPI RawGetState(DWORD index, XINPUT_STATE* state) noexcept {
    const auto error = nextRaw(index, state);
    // 只处理游戏 IAT 本次请求；其他库/Steam/ImGui 调用同一导出时不消费输入。
    if (!frame.active || frame.observed || frame.index != index || index >= 4 || !state) return error;
    frame.observed = true;
    AcquireSRWLockExclusive(&padLock);
    if (error == ERROR_SUCCESS) {
        const bool supported = frame.chain != panelinput::Chain::Unknown;
        if (supported) readyDevices.fetch_or(1u << index);
        else readyDevices.fetch_and(~(1u << index));
        frame.result = pads[index].Update(FromNative(state->Gamepad), open.load(), Foreground(), frame.chain);
        if (frame.result.activity && supported && Foreground()) controller.store(true);
        ApplyActions(frame.result.actions);
        state->Gamepad = ToNative(frame.result.downstream);
    } else {
        pads[index].Reset();
        readyDevices.fetch_and(~(1u << index));
        if (!readyDevices.load()) controller.store(false);
    }
    ReleaseSRWLockExclusive(&padLock);
    return error;
}

DWORD WINAPI GameGetState(DWORD index, XINPUT_STATE* state) noexcept {
    if (frame.active) return nextState(index, state);
    const auto saved = frame;
    frame = {}; frame.active = true; frame.index = index; frame.chain = DetectChain();
    const auto error = nextState(index, state);
    if (index < 4 && state) {
        AcquireSRWLockExclusive(&padLock);
        if (error != ERROR_SUCCESS) {
            pads[index].Reset(); readyDevices.fetch_and(~(1u << index));
            fallbackTail[index] = false;
            if (!readyDevices.load()) controller.store(false);
        } else {
            if (!frame.observed) {
                // 不改用另一套物理设备轮询；明确降级为 F11，仍保留模态面板的吞键保护。
                readyDevices.fetch_and(~(1u << index)); pads[index].Reset();
                // 未观测到原始链时也必须吞掉关闭后的剩余按压。只根据本链返回值等待
                // 中立状态，不轮询另一套物理手柄；这条退路不负责识别组合键。
                if (!Foreground()) fallbackTail[index] = false;
                else if (open.load()) fallbackTail[index] = true;
                else if (panelinput::Neutral(FromNative(state->Gamepad))) fallbackTail[index] = false;
                frame.result.capture = Foreground() && (open.load() || fallbackTail[index]);
            }
            const auto final = panelinput::FinalGamePad(FromNative(state->Gamepad), frame.result,
                frame.chain, Foreground() && open.load());
            const auto output = ToNative(final);
            if (std::memcmp(&outputs[index], &output, sizeof(output))) {
                outputs[index] = output; ++packets[index];
            }
            state->Gamepad = output; state->dwPacketNumber = packets[index];
        }
        ReleaseSRWLockExclusive(&padLock);
    }
    frame = saved;
    return error;
}

SHORT WINAPI GameAsyncKey(int key) noexcept {
    const SHORT value = nextAsync(key);
    if (Foreground() && (open.load() || keyboardTail.load() || key == VK_F11)) return 0;
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
    const bool foreground = GetForegroundWindow() == window;
    if (foreground && ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(lparam & (1LL << 30))))
        controller.store(false);
    if (foreground && (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MOUSEWHEEL))
        controller.store(false);
    if (message == WM_KILLFOCUS) {
        SetPanelOpen(false); actions.store(0); controller.store(false);
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
    if (auto callback = windowCallback.load(); callback && (open.load() || message == WM_KILLFOCUS || pairedMouseUp)) {
        panelMouseButtons.fetch_or(MouseButton(message, wparam, true));
        panelMouseButtons.fetch_and(~released);
        callback(window, message, wparam, lparam);
    }
    // 对应的按下从未交给游戏，配对释放也在此消费；Win32 后端因此能清理
    // MouseButtonsDown 并 ReleaseCapture，而不依赖已经清空的 ImGui IO 状态。
    if (pairedMouseUp) return 0;
    if (foreground && InputMessage(message) && (open.load() || keyboardTail.load())) {
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
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (const auto loader = GetModuleHandleW(L"xinput1_4.dll")) {
        const auto target = reinterpret_cast<void*>(GetProcAddress(loader, "XInputGetState"));
        if (target && MH_CreateHook(target, reinterpret_cast<void*>(&RawGetState), reinterpret_cast<void**>(&nextRaw)) == MH_OK) {
            if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); nextRaw = nullptr; }
        }
    }
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
    Log(nextRaw ? "Panel input installed: game IAT plus observed XInput export chain." :
                  "Panel input installed: raw XInput export unavailable; F11 only.");
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
    const int keys[] = {VK_F11, VK_UP, VK_DOWN, VK_RETURN, VK_ESCAPE};
    uint32_t down = 0;
    for (uint32_t i = 0; i < 5; ++i) if (GetAsyncKeyState(keys[i]) & 0x8000) down |= 1u << i;
    const bool foreground = Foreground();
    const auto events = keyboard.Update(down, open.load(), foreground);
    if (events) { controller.store(false); ApplyActions(events); }
    if (!foreground && open.load()) SetPanelOpen(false);
    if (!open.load() && keyboardTail.load()) {
        bool held = false;
        for (int key = 1; key < 256; ++key) held |= (GetAsyncKeyState(key) & 0x8000) != 0;
        if (!held) keyboardTail.store(false);
    }
}
uint32_t ConsumePanelActions() noexcept { return actions.exchange(0); }
bool PanelOpen() noexcept { return open.load(); }
void SetPanelOpen(bool value) noexcept {
    const bool previous = open.exchange(value);
    if (previous && !value) keyboardTail.store(true);
}
bool PanelUsingController() noexcept { return controller.load(); }
bool PanelControllerReady() noexcept { return readyDevices.load() != 0; }
const char* PanelInputStatus() noexcept {
    return PanelControllerReady() ? "" : "手柄输入链尚未确认，请使用 F11 打开；方向键 / Enter 操作。";
}
}
