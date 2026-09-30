// 执行真实 panel_input.cpp 的隔离回归。系统键盘/前台/时钟替换为合成值，
// 不安装 IAT、不访问游戏、不读取真实设备；合作输入域仍使用生产实现。
#include <Windows.h>
#include <Xinput.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include "standalone_ui/hotkeys.h"

namespace {
std::array<SHORT, 256> fixtureKeys{};
HWND fixtureForeground = reinterpret_cast<HWND>(1);
uint64_t fixtureNow = 1000;
XINPUT_GAMEPAD fixtureSample{};
unsigned forwarded = 0;
unsigned guiEvents = 0;
void GuiMessage(HWND, UINT, WPARAM, LPARAM) noexcept { ++guiEvents; }
SHORT WINAPI FakeAsync(int key) { return key >= 0 && key < 256 ? fixtureKeys[key] : 0; }
HWND WINAPI FakeForeground() { return fixtureForeground; }
ULONGLONG WINAPI FakeTime() { return fixtureNow; }
LRESULT WINAPI FakeWindow(WNDPROC, HWND, UINT, WPARAM, LPARAM) { ++forwarded; return 71; }
DWORD WINAPI FakeState(DWORD, XINPUT_STATE* state) { state->Gamepad = fixtureSample; return ERROR_SUCCESS; }
}

namespace sky2solo {
// 保留真实共同引擎的配置、匹配和状态机，只替换其系统键盘采样边界。
// 用左右修饰键单独置位，覆盖系统没有同时提供通用 VK 的合成情况。
HotkeyKeyboardState FixtureKeyboard() noexcept {
    HotkeyKeyboardState state;
    for (size_t key=0;key<fixtureKeys.size();++key) state.down[key]=(fixtureKeys[key]&0x8000)!=0;
    if(state.down[VK_CONTROL]||state.down[VK_LCONTROL]||state.down[VK_RCONTROL])state.modifiers|=HotkeyCtrl;
    if(state.down[VK_MENU]||state.down[VK_LMENU]||state.down[VK_RMENU])state.modifiers|=HotkeyAlt;
    if(state.down[VK_SHIFT]||state.down[VK_LSHIFT]||state.down[VK_RSHIFT])state.modifiers|=HotkeyShift;
    state.windows=state.down[VK_LWIN]||state.down[VK_RWIN];return state;
}
}

// 只替换输入边界；不复制生产策略，也不把私有假值传给真实 Win32 窗口 API。
#define GetAsyncKeyState FakeAsync
#define GetForegroundWindow FakeForeground
#define GetTickCount64 FakeTime
#define CallWindowProcW FakeWindow
#define ReadHotkeyKeyboardState FixtureKeyboard
#include "../native/panel_input.cpp"
#undef GetAsyncKeyState
#undef GetForegroundWindow
#undef GetTickCount64
#undef CallWindowProcW
#undef ReadHotkeyKeyboardState
namespace sky2party { void Log(const char*) noexcept {} }

namespace {
using namespace sky2party;
void Check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void Reset() {
    inputLease.Release(); open.store(false); keyboardTail.store(false); captureAvailable.store(false);
    fixtureKeys.fill(0); fixtureKeys[7] = static_cast<SHORT>(0x8000); // 现场保留 VK 的异常恒真值。
    fixtureForeground = reinterpret_cast<HWND>(1); inputWindow.store(fixtureForeground);
    windowFocused.store(true); guiKeyboardArmed.store(false); guiEvents = 0;
    nextAsync = &FakeAsync; nextState = &FakeState; nextWindow.store(DefWindowProcW);
    windowCallback.store(nullptr); panelMouseButtons.store(0); fixtureNow += 1000;
    navigationDevice.store(-1); readyDevices.store(0); actions.store(0); fixtureSample = {};
    for (auto& policy : pads) policy.Reset();
    SetPanelFrameHealth(true); PumpPanelKeyboard(); ConsumePanelReset();
}
XINPUT_GAMEPAD Poll(unsigned slot = 0) {
    XINPUT_STATE state{}; Check(GameGetState(slot, &state) == ERROR_SUCCESS, "synthetic poll succeeds");
    return state.Gamepad;
}
bool Neutral(const XINPUT_GAMEPAD& value) {
    return !value.wButtons && !value.bLeftTrigger && !value.bRightTrigger &&
        !value.sThumbLX && !value.sThumbLY && !value.sThumbRX && !value.sThumbRY;
}
}

int main() {
    // 配置只写入构建目录下的进程专用夹具文件，不访问真实 Mod/游戏目录。
    const auto configDirectory=std::filesystem::current_path()/
        (L"party-input-hotkeys-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(configDirectory);
    const sky2solo::HotkeyDefinition hotkeys[]{{"party.open","Party window",{VK_F8,0,XINPUT_GAMEPAD_DPAD_LEFT},true}};
    Check(sky2solo::InitializeHotkeys(0x50525459,"Party Editor",configDirectory.c_str(),hotkeys,1),"initialize real configurable input");
    // 当前独立入口必须使用精确 F8；旧 F11 归还游戏，不能偷偷留下别名。
    Reset(); fixtureKeys[VK_F11] = static_cast<SHORT>(0x8000); PumpPanelKeyboard();
    Check(!PanelOpen() && GameAsyncKey(VK_F11) != 0, "old F11 neither opens Party nor stays reserved");
    fixtureKeys[VK_F11] = 0; PumpPanelKeyboard(); fixtureKeys[VK_F8] = static_cast<SHORT>(0x8000);
    PumpPanelKeyboard(); Check(PanelOpen(), "plain F8 opens the standalone Party window");
    PumpPanelKeyboard(); Check(PanelOpen(), "held F8 never repeats the opening toggle");
    fixtureKeys[VK_CONTROL] = static_cast<SHORT>(0x8000); PumpPanelKeyboard();
    Check(PanelOpen(), "adding a modifier to held F8 cannot toggle again");

    // 除通用修饰键外逐个覆盖左右键：不能假定测试或输入法总会同时报告通用 VK。
    constexpr int modifiers[]{VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU,
        VK_RMENU, VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN};
    for (const int modifier : modifiers) {
        Reset(); fixtureKeys[modifier] = static_cast<SHORT>(0x8000);
        fixtureKeys[VK_F8] = static_cast<SHORT>(0x8000); PumpPanelKeyboard();
        Check(!PanelOpen() && GameAsyncKey(VK_F8) != 0, "modified F8 stays available to the game and other Mods");
        fixtureKeys[modifier] = 0; PumpPanelKeyboard();
        Check(!PanelOpen(), "releasing modifier before F8 cannot synthesize a plain F8 press");
        fixtureKeys[VK_F8] = 0; PumpPanelKeyboard(); fixtureKeys[VK_F8] = static_cast<SHORT>(0x8000);
        PumpPanelKeyboard(); Check(PanelOpen(), "fresh plain F8 works after modified press is released");
    }
    // 独立面板没有隐藏功能热键；旧 View+LS、其它窗口方向和斜方向均不打开队伍页。
    constexpr WORD excludedButtons[]{XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_DPAD_UP,
        XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT,
        XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT,
        XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_DPAD_LEFT};
    for (const WORD button : excludedButtons) {
        Reset(); Poll(); fixtureSample.wButtons = XINPUT_GAMEPAD_BACK | button; Poll();
        Check(!PanelOpen(), "only View plus a single D-pad Left can open Party");
        fixtureSample = {}; Check(Neutral(Poll()), "unused Party chord still consumes combined View replay");
    }
    Reset(); Poll(); fixtureSample.wButtons = XINPUT_GAMEPAD_DPAD_LEFT; Poll();
    fixtureSample.wButtons |= XINPUT_GAMEPAD_BACK; Poll();
    Check(!PanelOpen(), "holding D-pad Left before View does not count as a new opening press");
    fixtureSample = {}; Poll(); fixtureSample.wButtons = XINPUT_GAMEPAD_BACK; Poll();
    fixtureSample.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT; Poll();
    Check(PanelOpen(), "View followed by a new D-pad Left press opens Party");

    Reset();
    SetPanelOpen(true); SetPanelOpen(false); PumpPanelKeyboard();
    Check(!keyboardTail.load(), "VK7 cannot retain closing keyboard tail");
    fixtureKeys['A'] = static_cast<SHORT>(0x8000);
    Check(GameAsyncKey('A') == fixtureKeys['A'], "real game keyboard resumes after close");
    Check(PanelWindowProc(fixtureForeground, WM_MOUSEMOVE, 0, 0) == 71,
        "mouse window messages resume after close");

    Reset(); SetPanelOpen(true); fixtureKeys['A'] = static_cast<SHORT>(0x8000); SetPanelOpen(false);
    PumpPanelKeyboard(); Check(GameAsyncKey('A') == 0, "legitimate held A tail remains isolated");
    fixtureKeys['A'] = 0; PumpPanelKeyboard(); fixtureKeys['A'] = static_cast<SHORT>(0x8000);
    Check(GameAsyncKey('A') != 0, "new A after release reaches game");

    Reset(); SetPanelOpen(true); fixtureForeground = nullptr; PumpPanelKeyboard();
    Check(PanelOpen() && !PanelInteractive() && sky2solo::InputOwner() == 0,
        "focus loss preserves visible intent and releases input ownership");
    fixtureKeys['A'] = static_cast<SHORT>(0x8000);
    Check(GameAsyncKey('A') != 0 && ConsumePanelReset(), "focus loss passes input and requests confirmation cancellation");
    fixtureForeground = inputWindow.load(); fixtureKeys[VK_F8] = static_cast<SHORT>(0x8000);
    PumpPanelKeyboard(); Check(PanelOpen(), "old held F8 does not close on focus return");
    fixtureKeys['A'] = 0; fixtureKeys[VK_F8] = 0; PumpPanelKeyboard(); fixtureKeys[VK_F8] = static_cast<SHORT>(0x8000);
    PumpPanelKeyboard(); Check(!PanelOpen(), "fresh F8 works after focus return release");

    Reset(); SetPanelOpen(true); fixtureNow += 501; fixtureKeys['A'] = static_cast<SHORT>(0x8000);
    Check(GameAsyncKey('A') != 0 && PanelOpen(), "stalled Present releases input without deleting window intent");
    fixtureSample.wButtons = XINPUT_GAMEPAD_A;
    Check(Poll().wButtons == XINPUT_GAMEPAD_A, "render timeout also passes actual game IAT state");
    SetPanelFrameHealth(false); Check(!keyboardTail.load(), "explicit frame failure cannot leave closing tail latched");
    SetPanelFrameHealth(true); Check(PanelInteractive(), "healthy foreground frame restores visible window capture");

    Reset(); Poll(); SetPanelOpen(true);
    panelinput::Pad cached{};
    Check(!ReadPanelPad(cached), "opening rejects a pre-opening neutral cache");
    Poll(); Check(ReadPanelPad(cached), "new game IAT sample permits navigation");
    fixtureNow += 251;
    Check(!ReadPanelPad(cached), "stopped game polling expires cache without polling a physical controller");
    SetPanelFrameHealth(true); Check(PanelInteractive(), "no fresh gamepad does not disable the keyboard window");
    Poll(); fixtureForeground = nullptr; PumpPanelKeyboard(); fixtureForeground = inputWindow.load();
    Check(PanelInteractive() && !ReadPanelPad(cached), "focus restore cannot reuse previous-generation neutral sample");
    fixtureSample.wButtons = XINPUT_GAMEPAD_A; Poll();
    Check(ReadPanelPad(cached) && cached.buttons == XINPUT_GAMEPAD_A,
        "fresh held A remains a value snapshot for the UI release gate");

    Reset(); SetPanelOpen(true); PumpPanelKeyboard(); windowCallback.store(&GuiMessage);
    // WM_KILLFOCUS 期间 GetForegroundWindow 故意仍返回旧窗口，复现消息顺序。
    PanelWindowProc(inputWindow.load(), WM_KILLFOCUS, 0, 0);
    Check(PanelOpen() && !PanelInteractive() && sky2solo::InputOwner() == 0,
        "focus message barrier prevents immediate lease reacquisition");
    PanelWindowProc(inputWindow.load(), WM_SETFOCUS, 0, 0); fixtureKeys[VK_RETURN] = static_cast<SHORT>(0x8000);
    PumpPanelKeyboard(); const auto beforeRepeat = guiEvents;
    PanelWindowProc(inputWindow.load(), WM_KEYDOWN, VK_RETURN, 1LL << 30);
    Check(guiEvents == beforeRepeat, "old autorepeated Enter is not delivered to GUI after focus restore");
    fixtureKeys[VK_RETURN] = 0; PumpPanelKeyboard();
    PanelWindowProc(inputWindow.load(), WM_KEYDOWN, VK_RETURN, 0);
    Check(guiEvents == beforeRepeat + 1, "fresh Enter is delivered after real release");

    Reset(); Poll(0); Poll(1); fixtureSample.wButtons = XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_DPAD_LEFT;
    Check(Neutral(Poll(0)) && PanelOpen(), "real production IAT detects open chord without export hook");
    Check(Neutral(Poll(1)) && PanelOpen(), "mirrored second XInput slot cannot toggle window twice");
    Poll(0); // 显式打开已改变捕获代次，导航必须等本代次的新样本。
    panelinput::Pad observed{};
    Check(ReadPanelPad(observed), "navigation requires a fresh capture-generation sample");
    Check(observed.buttons == fixtureSample.wButtons, "navigation keeps pre-filter game-chain sample");
    sky2solo::InputLease other{0x123456}; other.Claim(); PumpPanelKeyboard();
    Check(!PanelOpen() && other.Owns(), "another window ownership closes old visible window without stealing lease");
    other.Release();

    Reset(); Poll(0); Poll(1);
    fixtureSample.wButtons = XINPUT_GAMEPAD_A; Poll(1);
    Check(navigationDevice.load() == 1, "active slot1 replaces merely connected idle slot0");
    fixtureSample = {}; Poll(1);
    fixtureSample.wButtons = XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_DPAD_LEFT;
    Poll(1); Check(PanelOpen(), "actual controller in slot1 opens Party while slot0 stays idle");
    fixtureSample = {}; Poll(0); Poll(1);
    Check(navigationDevice.load() == 1 && ReadPanelPad(observed), "idle slot0 cannot steal active UI sample");

    Reset(); Poll(); other.Claim(); fixtureSample.wButtons = XINPUT_GAMEPAD_BACK; Poll();
    other.Release(); fixtureSample = {};
    Check(Neutral(Poll()), "View held under another window never replays after that window closes");

    Reset(); Poll(); fixtureSample.wButtons = XINPUT_GAMEPAD_BACK;
    Check(Neutral(Poll()), "single View press is delayed");
    fixtureSample = {}; Check(Poll().wButtons == XINPUT_GAMEPAD_BACK, "single View release replays exactly once");
    Check(Neutral(Poll()), "View replay does not repeat");
    fixtureSample.wButtons = XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_A; Poll(); fixtureSample = {};
    Check(Neutral(Poll()), "another Mod View combination cannot replay native View");

    Reset(); Poll();
    {
        // 模拟外层合作 ASI：Party 内层必须观察完整组合并保持返回值，最外层
        // 统一归零。否则内层改变值会让其他窗口看不到自己的快捷键。
        sky2solo::GamepadCall outer;
        fixtureSample.wButtons = XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_DPAD_LEFT;
        auto returned = Poll();
        Check(returned.wButtons == fixtureSample.wButtons && PanelOpen(), "inner Party IAT must not edit sample");
        outer.Filter(returned); Check(Neutral(returned), "outer IAT consumes combined modal capture");
    }
    inputLease.Release();
    Reset(); Poll(); fixtureSample.sThumbLX = 20000; Poll();
    Check(PanelUsingController(), "new non-neutral axis activity chooses controller hints");
    PanelWindowProc(inputWindow.load(), WM_MOUSEMOVE, 0, MAKELPARAM(100, 120));
    Check(!PanelUsingController(), "real mouse movement restores mouse cursor and keyboard hints");
    Poll(); Check(!PanelUsingController(), "unchanged held controller sample does not steal mouse mode");
    fixtureSample.sThumbLX = 26000; Poll();
    Check(PanelUsingController(), "new axis movement restores controller mode");
    fixtureForeground = nullptr; controller.store(false); fixtureSample.wButtons = XINPUT_GAMEPAD_A; Poll();
    Check(!PanelUsingController(), "background gamepad cannot take input-device identity");
    // 改绑发布新 revision 后，已按住的新主键/手柄不能立即触发；旧入口必须归还。
    Reset(); Poll(); fixtureKeys[VK_F10]=static_cast<SHORT>(0x8000);
    std::string error;
    Check(sky2solo::CommitHotkey(0,{VK_F10,sky2solo::HotkeyCtrl,XINPUT_GAMEPAD_Y},error),"commit dynamic window shortcut");
    fixtureKeys[VK_CONTROL]=static_cast<SHORT>(0x8000); PumpPanelKeyboard();
    Check(!PanelOpen(),"new keyboard shortcut held during commit cannot open");
    fixtureKeys[VK_CONTROL]=0;fixtureKeys[VK_F10]=0;PumpPanelKeyboard();
    fixtureKeys[VK_F8]=static_cast<SHORT>(0x8000);PumpPanelKeyboard();
    Check(!PanelOpen()&&GameAsyncKey(VK_F8)!=0,"old default F8 is neither active nor reserved after rebind");
    fixtureKeys[VK_F8]=0;PumpPanelKeyboard();fixtureKeys[VK_F10]=static_cast<SHORT>(0x8000);
    fixtureKeys[VK_CONTROL]=static_cast<SHORT>(0x8000);PumpPanelKeyboard();
    Check(PanelOpen()&&GameAsyncKey(VK_F10)==0,"fresh configured exact keyboard chord opens and is filtered");
    Reset();fixtureSample.wButtons=XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_Y;
    Poll();Check(!PanelOpen(),"revised pad binding waits for a neutral sample");
    fixtureSample={};Poll();fixtureSample.wButtons=XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_DPAD_LEFT;Poll();
    Check(!PanelOpen(),"old pad shortcut no longer opens");fixtureSample={};Poll(0);Poll(1);
    fixtureSample.wButtons=XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_Y;Poll(0);Poll(1);
    Check(PanelOpen(),"new configured pad chord opens exactly once across mirrored slots");
    inputLease.Release();std::filesystem::remove_all(configDirectory);
    std::puts("PASS: production configurable standalone input release/focus/heartbeat/mirror/cooperative-IAT scenarios.");
}
