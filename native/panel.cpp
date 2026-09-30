// 独立控制面板：Present 线程只绘制值快照、提交命令；实际设置和角色操作由游戏线程复核。
// 与其他 ASI 共享 Present 调用链，但不共享 ImGui 上下文，不持有游戏后缓冲引用。
#include "panel.h"
#include "panel_input.h"
#include "panel_input_policy.h"
#include "control_state.h"
#include "runtime.h"
#include "ui_text.h"
#include "game_language.h"
#include "game_names.h"
#include "panel_fonts.h"
#include "party_page.h"
#include "standalone_ui/ui.h"
#include "standalone_ui/input.h"
#include "standalone_ui/hotkeys.h"
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace sky2party {
namespace {
using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
PresentFn nextPresent = nullptr;
// 安装过程尚未完成时只转发 Present，不能提前接受会打开模态面板的输入。
std::atomic<bool> renderReady{false};
ImGuiContext* context = nullptr;
ID3D11Device* device = nullptr;
ID3D11DeviceContext* deviceContext = nullptr;
bool dx11Ready = false;
HWND gameWindow = nullptr;
std::recursive_mutex renderLock;
ImGuiStyle baseStyle{};
sky2solo::WindowState windowState;
sky2solo::HotkeyEditorState hotkeyEditor;
std::array<bool, kLanguageCount> fontComplete{};

int32_t SKY2_CALL LocalLanguage() noexcept {
    constexpr int values[]{0, 2, 3, 1, 4, 5, 6, 7};
    const auto current = static_cast<unsigned>(CurrentLanguage());
    return current < std::size(values) ? values[current] : 0;
}
void CheckFontCoverage() {
    for (unsigned index = 0; index < kLanguageCount; ++index) {
        const auto language = static_cast<Language>(index);
        bool complete = true;
        for (unsigned text = 0; text < static_cast<unsigned>(Text::Count); ++text)
            complete &= PanelFontCoversText(TextFor(language, static_cast<Text>(text)));
        for (const auto& role : kRosterDefinitions)
            if (const auto* name = CharacterNameFor(language, role.id)) complete &= PanelFontCoversText(name);
        for (unsigned term = 0; term < static_cast<unsigned>(GameTerm::Count); ++term)
            if (const auto* name = GameTermFor(language, static_cast<GameTerm>(term))) complete &= PanelFontCoversText(name);
        fontComplete[index] = complete;
        if (!complete) {
            char diagnostic[128]{};
            std::snprintf(diagnostic, sizeof(diagnostic), "Party font coverage incomplete for language index %u.", index);
            Log(diagnostic);
        }
    }
}

// 每条进入 ImGui 的路径均恢复调用者上下文，包括 Win32 消息和绘制失败的早退路径。
// recursive_mutex 允许 Win32 后端在同线程同步派发窗口消息，其他线程仍串行访问本上下文。
struct ContextScope {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ~ContextScope() { ImGui::SetCurrentContext(previous); }
};
struct OutputTargets {
    ID3D11RenderTargetView* original[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* depth = nullptr;
    explicit OutputTargets(ID3D11RenderTargetView* target) {
        deviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, original, &depth);
        deviceContext->OMSetRenderTargets(1, &target, nullptr);
    }
    ~OutputTargets() {
        deviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, original, depth);
        for (auto* target : original) if (target) target->Release();
        if (depth) depth->Release();
    }
};
struct RenderView {
    ID3D11RenderTargetView* value = nullptr;
    ~RenderView() { if (value) value->Release(); }
};

void WindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    try {
        std::lock_guard<std::recursive_mutex> guard(renderLock);
        ContextScope restore;
        if (context && window == gameWindow) {
            ImGui::SetCurrentContext(context);
            ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
        }
    } catch (...) {
        // 不让可选界面的异常越过游戏的窗口过程；输入层仍负责模态吞键。
    }
}

bool InitializeGui(IDXGISwapChain* swap) {
    DXGI_SWAP_CHAIN_DESC description{};
    if (FAILED(swap->GetDesc(&description)) || !description.OutputWindow) return false;
    DWORD owner = 0;
    RECT client{};
    GetWindowThreadProcessId(description.OutputWindow, &owner);
    if (owner != GetCurrentProcessId() || !GetClientRect(description.OutputWindow, &client) ||
        client.right < 320 || client.bottom < 240) return false;
    if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) return false;
    device->GetImmediateContext(&deviceContext);
    gameWindow = description.OutputWindow;
    IMGUI_CHECKVERSION();
    context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    // 手柄和键盘统一由自有按下沿状态机驱动。禁止后端再轮询 XInput，避免消费第二份状态。
    io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
    // 一次合并八语所需本机字库；热切换只选择文本，不重建图集或重置当前行。
    LoadPanelFonts(io);
    CheckFontCoverage();
    sky2solo::ConfigureTheme();
    baseStyle = ImGui::GetStyle();
    const bool win32 = ImGui_ImplWin32_Init(gameWindow);
    dx11Ready = win32 && ImGui_ImplDX11_Init(device, deviceContext);
    if (!dx11Ready) {
        if (win32) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(context); context = nullptr;
        deviceContext->Release(); deviceContext = nullptr;
        device->Release(); device = nullptr;
        gameWindow = nullptr;
        Log("Party panel: ImGui backend initialization failed.");
        return false;
    }
    AttachPanelWindow(gameWindow, &WindowMessage);
    Log("Party panel: independent DX11/ImGui context ready.");
    return true;
}

// Header 和 Main 使用独立业务页面；页面只提交原有安全队列，不能在 Present
// 线程写入角色或游戏指令。独立壳的第二侧栏集中说明，避免业务页重复堆文案。
void DrawHeader(void*, const Sky2Frame& frame, int section) {
    if (section == 0) { auto page = frame; page.page_active = 1; DrawPartyHeader(*sky2solo::UiApi(), page); }
}
void DrawContent(void*, const Sky2Frame& frame, int section) {
    if (section == 0) { auto page = frame; page.page_active = 1; DrawPartyPage(*sky2solo::UiApi(), page); return; }
    if (section == 2) { sky2solo::DrawHotkeySettings(hotkeyEditor, LocalLanguage()); return; }
    const auto* ui = sky2solo::UiApi();
    ui->text_wrapped(Tr(Text::RoleUsageNote));
    ui->separator();
    ui->text_wrapped(Tr(Text::SaveNote));
    ui->text_wrapped(Tr(Text::FixedRestoreNote));
    if (!PanelControllerReady()) ui->text_wrapped(PanelInputStatus());
}
void SectionChanged(void*, int) {
    // 切离业务页取消角色确认，切离快捷键页丢弃尚未保存的候选；保存只调整
    // Mod 自己的输入规则，不经过角色写入队列，也不修改游戏的原生键位。
    PartyPageVisibilityChanged(false); sky2solo::ResetHotkeyEditor(hotkeyEditor);
}
void DrawPanel(float scale) {
    const char* sections[]{
        Localize("队伍编辑", "パーティー編集", "Party editor", "隊伍編輯", "Gruppe bearbeiten",
            "Éditeur d’équipe", "Editor de grupo", "파티 편집"),
        Localize("使用说明", "使い方", "Instructions", "使用說明", "Anleitung", "Instructions", "Instrucciones", "사용 안내"),
        Localize("快捷键", "ショートカット", "Shortcuts", "快捷鍵", "Tastenkürzel", "Raccourcis", "Atajos", "단축키")};
    const char* description = Localize("调整编成功能，安全管理角色后备。", "編成機能と控えメンバーを管理します。",
        "Configure party features and manage reserve members.", "調整編成功能，安全管理角色後備。",
        "Gruppenfunktionen und Reservemitglieder verwalten.", "Réglez les fonctions d’équipe et gérez les réservistes.",
        "Configura las funciones de grupo y gestiona los miembros de reserva.", "편성 기능과 대기 멤버를 관리합니다.");
    const auto display = ImGui::GetIO().DisplaySize;
    Sky2Frame frame{sizeof(Sky2Frame), display.x, display.y, scale, GetTickCount64(),
        PanelInteractive() ? 1 : 0, PanelOpen() ? 1 : 0, windowState.section == 0 ? 1 : 0,
        PanelUsingController() ? 1 : 0, 0};
    TickPartyPage(frame);
    if (!PanelOpen()) return;
    sky2solo::WindowSpec spec{};
    spec.id = "Sky2PartyEditorPanel"; spec.title = sections[0]; spec.description = description;
    spec.sections = sections; spec.sectionCount = 3; spec.header = &DrawHeader;
    spec.draw = &DrawContent; spec.changed = &SectionChanged; spec.language = LocalLanguage();
    if (!sky2solo::DrawWindow(windowState, spec, frame)) {
        SetPanelOpen(false); PartyPageVisibilityChanged(false);
    }
}

// 同一窗口可能在片头后换用新的 D3D11 设备。ImGui 上下文和页面选择继续保留，
// 仅重建 DX11 后端；不得用旧设备为新设备的后缓冲创建 RTV。
bool BindSwapDevice(IDXGISwapChain* swap) {
    ID3D11Device* current = nullptr;
    if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&current))) || !current) return false;
    if (current == device) {
        current->Release();
        // 上次重绑可能在后端分配阶段失败，不能只因设备指针相同就宣告恢复。
        if (!dx11Ready) dx11Ready = ImGui_ImplDX11_Init(device, deviceContext);
        return dx11Ready;
    }
    ID3D11DeviceContext* next = nullptr;
    current->GetImmediateContext(&next);
    if (!next) { current->Release(); return false; }
    if (dx11Ready) ImGui_ImplDX11_Shutdown();
    dx11Ready = false;
    deviceContext->Release(); device->Release();
    device = current; deviceContext = next;
    const bool initialized = dx11Ready = ImGui_ImplDX11_Init(device, deviceContext);
    Log(initialized ? "Party panel: rebound replacement D3D11 device." : "Party panel: replacement D3D11 backend failed.");
    return initialized;
}

HRESULT WINAPI Present(IDXGISwapChain* swap, UINT interval, UINT options) {
    if (renderReady.load(std::memory_order_acquire) && !(options & DXGI_PRESENT_TEST)) {
        std::lock_guard<std::recursive_mutex> guard(renderLock);
        ContextScope restore;
        bool healthy = false;
        bool relevant = false;
        try {
            if (context || InitializeGui(swap)) {
                DXGI_SWAP_CHAIN_DESC description{};
                if (SUCCEEDED(swap->GetDesc(&description)) && description.OutputWindow == gameWindow) {
                    relevant = true;
                    ImGui::SetCurrentContext(context);
                    RefreshGameLanguage();
                    // 输入清理位于 GetBuffer/RTV 之前。渲染失败时也不能跳过关闭
                    // 尾部检查；失焦先取消确认，后台可继续显示相同只读快照。
                    PumpPanelKeyboard();
                    ConsumePanelActions();
                    if (ConsumePanelReset()) {
                        PartyPageVisibilityChanged(false); windowState.resetFocus = true;
                        sky2solo::ResetHotkeyEditor(hotkeyEditor);
                        auto& io = ImGui::GetIO();
                        io.ClearEventsQueue(); io.ClearInputKeys(); io.ClearInputMouse();
                        sky2solo::FeedGamepad(nullptr, false);
                    }
                    ID3D11Texture2D* buffer = nullptr;
                    if (BindSwapDevice(swap) && SUCCEEDED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer))) && buffer) {
                        D3D11_TEXTURE2D_DESC size{}; buffer->GetDesc(&size);
                        RenderView target;
                        const auto result = device->CreateRenderTargetView(buffer, nullptr, &target.value);
                        buffer->Release();
                        if (SUCCEEDED(result) && target.value) {
                            ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame();
                            auto& io = ImGui::GetIO();
                            io.DisplayFramebufferScale = ImVec2(size.Width / std::max(io.DisplaySize.x, 1.0f),
                                size.Height / std::max(io.DisplaySize.y, 1.0f));
                            io.MouseDrawCursor = PanelInteractive() && !PanelUsingController();
                            const float scale = std::clamp(io.DisplaySize.y / 1080.0f, 0.7f, 2.5f);
                            // 三个独立壳共享物理像素边框/间距；仅正文走字体 DPI，
                            // 不再把同一缩放同时作用于样式尺寸与壳的布局参数。
                            ImGui::GetStyle() = baseStyle;
                            ImGui::GetStyle().FontScaleDpi = scale;
                            panelinput::Pad sample{};
                            const bool freshPad = ReadPanelPad(sample);
                            XINPUT_GAMEPAD pad{sample.buttons, sample.lt, sample.rt, sample.lx, sample.ly, sample.rx, sample.ry};
                            // 禁止后端自行轮询；只把游戏既有调用链的值快照送到导航。
                            sky2solo::FeedGamepad(freshPad ? &pad : nullptr, PanelInteractive());
                            ImGui::NewFrame(); DrawPanel(scale); ImGui::Render();
                            {
                                OutputTargets targets(target.value);
                                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                            }
                            healthy = true;
                        }
                    }
                }
            }
        } catch (...) {
            PartyPageVisibilityChanged(false);
            // 不让异常越过游戏 Present；公共 UI 的正常流程不依赖异常恢复。
            Log("Party panel: drawing exception contained; input capture released.");
        }
        // 辅助窗口的交换链不属于本面板，不能把另一个窗口的 Present 当作
        // 主窗口绘制失败。真实主窗口停帧仍由 500ms 心跳在输入边界自动放行。
        if (relevant) SetPanelFrameHealth(healthy);
        if (relevant && !healthy) {
            PartyPageVisibilityChanged(false);
            static uint64_t reportedAt = 0;
            const auto now = GetTickCount64();
            if (now - reportedAt >= 5000) { reportedAt = now; Log("Party panel: frame unavailable; game input passes through."); }
        }
    }
    return nextPresent(swap, interval, options);
}
}

bool InstallPanel(HMODULE module, uintptr_t executableBase) noexcept {
    // 隐藏临时窗口仅查询本进程 DXGI 虚表，不把第二个加载器或代理 DLL 写入游戏目录。
    const wchar_t* className = L"Sky2PartyEditorBootstrap";
    WNDCLASSW cls{};
    cls.lpfnWndProc = DefWindowProcW; cls.hInstance = module; cls.lpszClassName = className;
    if (!RegisterClassW(&cls)) return false;
    HWND window = CreateWindowExW(0, className, L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, module, nullptr);
    if (!window) { UnregisterClassW(className, module); return false; }
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferDesc.Width = 64; description.BufferDesc.Height = 64;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 1; description.OutputWindow = window;
    description.Windowed = TRUE; description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* swap = nullptr;
    ID3D11Device* bootstrapDevice = nullptr;
    const auto result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &description, &swap, &bootstrapDevice, nullptr, nullptr);
    void* target = SUCCEEDED(result) ? (*reinterpret_cast<void***>(swap))[8] : nullptr;
    if (swap) swap->Release();
    if (bootstrapDevice) bootstrapDevice->Release();
    DestroyWindow(window); UnregisterClassW(className, module);
    if (!target) return false;
    // 三个独立 ASI 各自静态链接 MinHook，必须把读取原字节到启用跳板的
    // 整段安装串行化。只检查 IAT 顶层归属会被第三个插件遮住，不能作为就绪信号。
    sky2solo::PresentInstallGuard installGuard;
    if (!installGuard) { Log("Party panel: Present installation lock unavailable."); return false; }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    unsigned char baseline[16]{};
    std::memcpy(baseline, target, sizeof(baseline));
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Present), reinterpret_cast<void**>(&nextPresent)) != MH_OK)
        return false;
    // 保留 Steam 和其他已完成的前置跳转链；创建期间若入口又被改写，则不启用过期
    // trampoline。此时尚未安装输入过滤层，不会留下无法显示但仍吞键的面板。
    if (std::memcmp(baseline, target, sizeof(baseline))) {
        MH_RemoveHook(target);
        Log("Party panel disabled: Present changed while creating the trampoline; existing chain retained.");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); return false; }
    if (!InstallPanelInput(executableBase)) {
        // 已经启用后，其他插件可能把本层写入自己的 trampoline。保持只透传
        // 比撤销全局入口更安全；renderReady 仍为 false，本层不会绘制或处理输入。
        Log("Party panel disabled: input installation failed; retaining transparent Present forwarding.");
        return false;
    }
    renderReady.store(true, std::memory_order_release);
    Log("Party panel: DXGI Present chain installed; configured window shortcuts ready.");
    return true;
}
}
