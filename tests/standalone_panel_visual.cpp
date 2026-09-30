// Party 真实业务页 + 独立窗口壳的离屏截图夹具。所有队伍数据均为合成快照，
// WARP 在内存纹理绘制，不创建游戏进程、不安装输入/Present 挂钩或读取存档。
#include "party_page.h"
#include "control_state.h"
#include "game_names.h"
#include "panel_fonts.h"
#include "standalone_ui/ui.h"
#include "standalone_ui/hotkeys.h"
#include <d3d11.h>
#include <wincodec.h>
#include <imgui_impl_dx11.h>
#include <imgui_internal.h>
#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
sky2party::ControlSnapshot snapshot;
Sky2UiApi ui{};
bool roster = false, armOnce = false;
unsigned writes = 0;
ImGuiID confirmId = 0;
bool confirmVisible = false;
sky2solo::HotkeyEditorState hotkeyEditor;
void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int32_t SKY2_CALL Tabs(const char* id, const char* const* labels, int32_t count, int32_t) {
    return sky2solo::UiApi()->tab_bar(id, labels, count, roster ? 1 : 0);
}
int32_t SKY2_CALL Button(const char* id, const char* label) {
    const bool pressed = sky2solo::UiApi()->button(id, label) != 0;
    if (std::strcmp(id, "role.confirm") == 0) {
        confirmId = GImGui->LastItemData.ID;
        const auto& rect = GImGui->LastItemData.Rect;
        const auto& clip = GImGui->CurrentWindow->ClipRect;
        confirmVisible = rect.Min.y >= clip.Min.y && rect.Max.y <= clip.Max.y;
    }
    if (armOnce && std::strcmp(id, "role.confirm") == 0) { armOnce = false; return 1; }
    return pressed;
}
void Header(void*, const Sky2Frame& frame, int section) { if(!section)sky2party::DrawPartyHeader(ui, frame); }
void Page(void*, const Sky2Frame& frame, int section) {
    if(section==2)sky2solo::DrawHotkeySettings(hotkeyEditor,0);
    else sky2party::DrawPartyPage(ui, frame);
}
void SavePng(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture,
    const std::filesystem::path& path) {
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "staging texture");
    context->CopyResource(staging, texture);
    D3D11_MAPPED_SUBRESOURCE pixels{};
    Require(SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &pixels)), "read rendered pixels");
    IWICImagingFactory* factory = nullptr; IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr; IWICBitmapFrameEncode* output = nullptr;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))), "WIC factory");
    Require(SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)), "PNG stream");
    Require(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)), "PNG encoder");
    Require(SUCCEEDED(encoder->CreateNewFrame(&output, nullptr)) && SUCCEEDED(output->Initialize(nullptr)) &&
        SUCCEEDED(output->SetSize(desc.Width, desc.Height)), "PNG frame");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Require(SUCCEEDED(output->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA, "PNG pixel format");
    Require(SUCCEEDED(output->WritePixels(desc.Height, pixels.RowPitch, pixels.RowPitch * desc.Height,
        static_cast<BYTE*>(pixels.pData))) && SUCCEEDED(output->Commit()) && SUCCEEDED(encoder->Commit()), "PNG write");
    output->Release(); encoder->Release(); stream->Release(); factory->Release();
    context->Unmap(staging, 0); staging->Release();
}
void Capture(const std::filesystem::path& path, unsigned width, unsigned height, bool showRoster, bool showHotkeys=false) {
    roster = showRoster; sky2party::PartyPageVisibilityChanged(false);
    ID3D11Device* device = nullptr; ID3D11DeviceContext* context = nullptr;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context)), "WARP device");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* texture = nullptr; ID3D11RenderTargetView* target = nullptr;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
        SUCCEEDED(device->CreateRenderTargetView(texture, nullptr, &target)), "offscreen target");
    auto* gui = ImGui::CreateContext(); auto& io = ImGui::GetIO();
    sky2party::LoadPanelFonts(io); sky2solo::ConfigureTheme();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    const float scale = std::clamp(height / 1080.0f, .7f, 2.5f);
    ImGui::GetStyle().FontScaleDpi = scale;
    Require(ImGui_ImplDX11_Init(device, context), "DX11 backend");
    ui = *sky2solo::UiApi(); ui.tab_bar = &Tabs; ui.button = &Button;
    sky2solo::WindowState window;
    window.section=showHotkeys?2:0;sky2solo::ResetHotkeyEditor(hotkeyEditor);
    const char* sections[]{"队伍编辑", "使用说明", "快捷键"};
    sky2solo::WindowSpec spec{"PartyVisualFixture", "队伍编辑", "调整编成功能，安全管理角色后备。",
        sections, 3, nullptr, &Header, &Page, nullptr, 0};
    Sky2Frame frame{sizeof(Sky2Frame), float(width), float(height), scale, 1000, 1, 1, 1, 1, 0};
    for (unsigned iteration = 0; iteration < 5; ++iteration) {
        // 确认快照只注入一次控件激活，必须保持未提交状态；没有假造确认文本。
        armOnce = showRoster && iteration == 3;
        // 从左侧名单真实向右导航，确认按钮即使起初在 Main 可视区外也必须
        // 能接收焦点并带动滚动；不能只在测试里直接设置 ScrollY 假造可达性。
        if (showRoster && iteration == 1) io.AddKeyEvent(ImGuiKey_GamepadDpadRight, true);
        if (showRoster && iteration == 2) io.AddKeyEvent(ImGuiKey_GamepadDpadRight, false);
        if (showRoster && iteration == 3) io.AddKeyEvent(ImGuiKey_GamepadDpadUp, true);
        if (showRoster && iteration == 4) io.AddKeyEvent(ImGuiKey_GamepadDpadUp, false);
        ImGui_ImplDX11_NewFrame(); io.DisplaySize = {float(width), float(height)}; io.DeltaTime = 1.0f / 60;
        frame.time_ms = 1000 + iteration;
        ImGui::NewFrame(); sky2party::TickPartyPage(frame); sky2solo::DrawWindow(window, spec, frame); ImGui::Render();
        const float background[]{.025f, .04f, .06f, 1}; context->OMSetRenderTargets(1, &target, nullptr);
        context->ClearRenderTargetView(target, background); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    Require(writes == 0, "single confirmation cannot enqueue a character write");
    if (showRoster) Require(GImGui->NavId == confirmId && confirmVisible,
        "720p directional navigation reaches and fully reveals the confirmation button");
    SavePng(device, context, texture, path);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    ImGui_ImplDX11_Shutdown(); ImGui::DestroyContext(gui);
    target->Release(); texture->Release(); context->Release(); device->Release();
}
}
namespace sky2party {
ControlSnapshot ReadControlSnapshot() noexcept { return snapshot; }
void RequestFeatureMask(uint32_t) noexcept { ++writes; }
bool QueueAddMember(uint32_t) noexcept { ++writes; return true; }
const char* CharacterNameFor(Language, uint32_t) noexcept { return nullptr; }
const char* GameTermFor(Language, GameTerm) noexcept { return nullptr; }
bool GameNamesReady(Language) noexcept { return false; }
const char* RosterResultText(RosterResult) noexcept { return "测试结果"; }
const char* RosterBlockReasonText(RosterBlockReason) noexcept { return "测试阻止原因"; }
void Log(const char*) noexcept {}
}
int wmain(int argc, wchar_t** argv) {
    Require(argc == 2, "provide an output directory");
    const std::filesystem::path output(argv[1]); std::filesystem::create_directories(output);
    const sky2solo::HotkeyDefinition hotkeys[]{{"party.open","Party window",{VK_F8,0,XINPUT_GAMEPAD_DPAD_LEFT},true}};
    Require(sky2solo::InitializeHotkeys(0x50525459,"Party Editor",output.c_str(),hotkeys,1),"visual shortcut defaults");
    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "COM initialization");
    snapshot.appliedStateKnown = true; snapshot.appliedFeatures = snapshot.requestedFeatures = 15;
    snapshot.roster.ready = snapshot.roster.canEdit = snapshot.roster.allowUnjoined = true;
    snapshot.roster.generation = 1; snapshot.roster.pendingId = sky2party::kNoRosterId;
    for (size_t index = 0; index < snapshot.roster.members.size(); ++index) {
        auto& member = snapshot.roster.members[index]; member.id = sky2party::kRosterDefinitions[index].id;
        member.level = 42; member.initialized = member.canAdd = true;
    }
    Capture(output / "party-standalone-normal.png", 1600, 1000, false);
    Capture(output / "party-standalone-720p.png", 1280, 720, false);
    Capture(output / "party-standalone-roster-720p.png", 1280, 720, true);
    Capture(output / "party-standalone-shortcuts-720p.png", 1280, 720, false, true);
    CoUninitialize(); std::puts("PASS: synthetic standalone Party screenshots rendered.");
}
