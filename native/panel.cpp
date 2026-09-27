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
HWND gameWindow = nullptr;
std::recursive_mutex renderLock;
ImGuiStyle baseStyle{};
panelinput::Confirmation confirmation;
size_t selected = 0;
uint64_t previousGeneration = 0;
bool wasOpen = false, scrollSelection = false, submissionRejected = false;
constexpr size_t featureCount = 4;
constexpr ImVec4 accent{0.43f, 0.87f, 0.78f, 1.0f};
constexpr ImVec4 warning{1.0f, 0.76f, 0.38f, 1.0f};
constexpr Text featureTexts[] = {Text::FeatureFixedMembers, Text::FeatureAnywhere,
    Text::FeatureUnavailable, Text::FeatureUnjoined};
std::array<bool, kLanguageCount> fontComplete{};

// 所有角色标签均从当前语言的原生表按稳定 ID 取得。资源不可用时明确显示编号，
// 不使用另一语言的姓名、不缩写军衔或称谓，也不影响名单和二次确认所绑定的身份。
std::string DisplayCharacter(uint32_t id) {
    if (const auto* name = CharacterNameFor(CurrentLanguage(), id)) return name;
    char buffer[96]{};
    std::snprintf(buffer, sizeof(buffer), Tr(Text::CharacterIdFormat), id);
    return buffer;
}

const char* ControlMessageText(ControlMessage message) noexcept {
    switch (message) {
    case ControlMessage::None: return "";
    case ControlMessage::FixedMembersNeedRestoring: return Tr(Text::ControlFixedBlocked);
    case ControlMessage::CannotVerifyParties: return Tr(Text::ControlFixedUnknown);
    case ControlMessage::Applied: return Tr(Text::ControlApplied);
    case ControlMessage::AppliedNotSaved: return Tr(Text::ControlSaveFailed);
    case ControlMessage::ApplyFailed: return Tr(Text::ControlApplyFailed);
    case ControlMessage::ServiceError: return Tr(Text::ControlException);
    case ControlMessage::WaitingForExploration: return Tr(Text::ControlWaiting);
    case ControlMessage::WaitingForSave: return Tr(Text::ControlLoadWait);
    }
    return "";
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

bool ChestExpected(HMODULE module) {
    if (GetModuleHandleW(L"Sky2ChestTracker.asi")) return true;
    wchar_t path[MAX_PATH]{};
    const auto length = GetModuleFileNameW(module, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    const std::wstring ownPath(path, length);
    const auto separator = ownPath.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return false;
    // 独立版不是 ASI Loader；旁边残留的未加载 ASI 文件不应让它等待超时。
    // 已实际加载的宝箱模块在上方始终被识别。正常多 Mod 安装使用同目录 ASI，
    // 此时仍在宝箱尚未映射前检测文件，保留两套 Present 挂钩的严格就绪顺序。
    const auto extension = ownPath.find_last_of(L'.');
    if (extension == std::wstring::npos || _wcsicmp(ownPath.c_str() + extension, L".asi") != 0) return false;
    const auto chest = ownPath.substr(0, separator + 1) + L"Sky2ChestTracker.asi";
    const auto attributes = GetFileAttributesW(chest.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool WaitForChestPresent(HMODULE module, uintptr_t executableBase) noexcept {
    try {
        if (!ChestExpected(module)) return true;
        Log("Party panel: waiting for Chest's completed Present installation before creating our trampoline.");
        const uint64_t start = GetTickCount64();
        for (;;) {
            const auto chest = GetModuleHandleW(L"Sky2ChestTracker.asi");
            const void* current = *reinterpret_cast<void* const volatile*>(executableBase + 0x8BB6E8);
            MEMORY_BASIC_INFORMATION information{};
            if (chest && VirtualQuery(current, &information, sizeof(information)) == sizeof(information) &&
                information.AllocationBase == chest) {
                // 已发布 Chest 0.6.0 严格在 InstallOverlay(Create+Enable) 完成后安装此 IAT。
                // 以这个实际完成状态作为就绪信号，避免两个独立 MinHook 库同时从旧原字节
                // 建 trampoline、随后后安装者绕过前一个面板。不能用固定睡眠猜测初始化完成。
                Log("Party panel: Chest input bridge confirms its Present hook is fully installed.");
                return true;
            }
            if (GetTickCount64() - start >= 10000) {
                Log("Party panel disabled: Chest initialization did not reach the verified ready state; no Present/input hooks installed.");
                return false;
            }
            Sleep(10); // 只限制查询频率，是否继续由上面的实际就绪条件决定。
        }
    } catch (...) {
        Log("Party panel disabled: could not verify companion initialization state.");
        return false;
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
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 9.0f;
    style.WindowPadding = ImVec2(18, 14);
    style.ItemSpacing = ImVec2(8, 7);
    style.CellPadding = ImVec2(6, 5);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.07f, 0.085f, 0.97f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.3f, 0.65f, 0.61f, 0.85f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.12f, 0.35f, 0.33f, 0.9f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.43f, 0.39f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.2f, 0.49f, 0.44f, 1.0f);
    baseStyle = style;
    const bool win32 = ImGui_ImplWin32_Init(gameWindow);
    if (!win32 || !ImGui_ImplDX11_Init(device, deviceContext)) {
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

void ChangeSelection(size_t value) {
    if (selected == value) return;
    selected = value;
    confirmation.Cancel();
    submissionRejected = false;
    scrollSelection = true;
}
const char* RoleAction(const RosterEntry& member) {
    if (member.needsPreparation) return Tr(Text::PrepareAndAdd);
    return Tr(member.hidden ? Text::RevealReserve : Text::AddReserve);
}
const char* RoleState(const RosterEntry& member) {
    if (member.hidden) return Tr(Text::HiddenReserve);
    if (member.inParty) return Tr(member.unavailable ? Text::Unavailable : Text::InRoster);
    if (!member.initialized && !member.needsPreparation) return Tr(Text::DataNotReady);
    return Tr(Text::Unjoined);
}
bool CanAdd(const ControlSnapshot& snapshot, const RosterEntry& member) {
    return snapshot.appliedStateKnown && (snapshot.appliedFeatures & FeatureUnjoined) &&
        snapshot.roster.ready && snapshot.roster.canEdit && member.canAdd &&
        snapshot.roster.pendingId == kNoRosterId;
}
void Activate(const ControlSnapshot& snapshot, uint64_t now) {
    if (selected < featureCount) {
        confirmation.Cancel();
        submissionRejected = false;
        RequestFeatureMask(snapshot.requestedFeatures ^ (1u << selected));
        return;
    }
    const auto index = selected - featureCount;
    if (index >= snapshot.roster.members.size()) return;
    const auto& member = snapshot.roster.members[index];
    if (!CanAdd(snapshot, member)) { confirmation.Cancel(); return; }
    // 二次确认同时绑定角色 ID、场景/存档代次和八秒期限；不会跨读档沿用授权。
    if (confirmation.Press(member.id, snapshot.roster.generation, now))
        submissionRejected = !QueueAddMember(member.id);
}
void Hint(const char* key, const char* label) {
    ImGui::TextColored(accent, "%s", key);
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
}

bool DrawSelectionRow(const char* label, bool isSelected) {
    // ImGui 默认也把未选中但被鼠标悬停的 Selectable 涂成绿色。手柄/键盘换行或
    // 列表滚动时，静止鼠标会留在另一行，形成两条看似都能接收 A/Enter 的选中条。
    // 绿色仅表示唯一的 selected；未选中的悬停/按压用中性灰提示可点击性。
    // 这里只改变局部绘制样式，不让悬停修改选择，更不会取消或消费角色二次确认。
    const auto selection = ImGui::GetStyleColorVec4(ImGuiCol_Header);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
        isSelected ? selection : ImVec4(0.14f, 0.17f, 0.19f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,
        isSelected ? selection : ImVec4(0.20f, 0.23f, 0.25f, 1.0f));
    // ### 后的标识恒定，配合调用方 PushID 保持每行身份；切换显示语言不会
    // 令活动控件换成另一个角色。名单次序始终由稳定角色 ID 决定。
    const auto widget = std::string(label) + "###selection";
    const bool pressed = ImGui::Selectable(widget.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns);
    ImGui::PopStyleColor(2);
    return pressed;
}

void DrawFixedMemberWarning(const ControlSnapshot& snapshot) {
    const auto& guard = snapshot.fixedGuard;
    // 只展示游戏线程刚完成的有效风险快照。过期、失败或零风险不额外占用面板，
    // 也不据此宣称可以安全卸载；关闭功能时的最终复核仍由控制服务执行。
    if (!snapshot.fixedGuardFresh || !guard.valid || guard.riskCount == 0) return;
    std::string summary = Tr(Text::FixedWarningPrefix);
    const auto count = std::min<size_t>({guard.memberCount, guard.members.size(), 4});
    for (size_t index = 0; index < count; ++index) {
        const auto& member = guard.members[index];
        char identity[256]{};
        const auto name = DisplayCharacter(member.id);
        std::snprintf(identity, sizeof(identity), Tr(Text::FixedMemberFormat), member.partyIndex + 1, name.c_str());
        if (index) summary += Tr(Text::ListSeparator);
        summary += identity;
    }
    // 显示条数有意限制为四条，避免多队伍记录挤掉操作区；总风险数仍明确保留。
    if (guard.riskCount > count) {
        char remainder[48]{};
        std::snprintf(remainder, sizeof(remainder), Tr(Text::FixedMoreFormat), guard.riskCount);
        summary += remainder;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, warning);
    ImGui::TextWrapped("%s", summary.c_str());
    ImGui::PopStyleColor();
    ImGui::TextWrapped("%s", Tr(Text::FixedRestoreNote));
}

const char* LevelHeading() noexcept {
    if (const auto* term = GameTermFor(CurrentLanguage(), GameTerm::Level)) return term;
    return Tr(Text::LevelColumn);
}

std::string FeatureStateLabel(const ControlSnapshot& snapshot, size_t index) {
    const bool requested = (snapshot.requestedFeatures & (1u << index)) != 0;
    const bool applied = (snapshot.appliedFeatures & (1u << index)) != 0;
    char buffer[512]{};
    if (!snapshot.appliedStateKnown)
        std::snprintf(buffer, sizeof(buffer), Tr(Text::UnknownStateFormat), Tr(requested ? Text::Enable : Text::Disable));
    else if (requested != applied)
        std::snprintf(buffer, sizeof(buffer), Tr(Text::PendingStateFormat), Tr(applied ? Text::On : Text::Off),
            Tr(requested ? Text::Enable : Text::Disable));
    else return Tr(applied ? Text::On : Text::Off);
    return buffer;
}

struct PanelLayout {
    float width = 0, name = 0, level = 0, state = 0, featureState = 0;
};

PanelLayout MeasurePanel(const ControlSnapshot& snapshot, float scale) {
    // 使用当前字体的实际宽度，而不是“中文字数”估算。中日韩名称和带军衔的
    // 欧洲语言名称、手柄键位提示都会计入；切换语言时只调整尺寸，不清空选中行。
    const auto widthOf = [](const char* text) { return ImGui::CalcTextSize(text).x; };
    const auto widest = [&](std::initializer_list<Text> texts) {
        float width = 0;
        for (const auto text : texts) width = std::max(width, widthOf(Tr(text)));
        return width;
    };
    PanelLayout result;
    result.name = widthOf(Tr(Text::CharacterColumn));
    for (const auto& definition : kRosterDefinitions)
        result.name = std::max(result.name, widthOf(DisplayCharacter(definition.id).c_str()));
    result.level = std::max(widthOf(LevelHeading()), widthOf("999"));
    result.state = widest({Text::StatusColumn, Text::HiddenReserve, Text::Unavailable,
        Text::InRoster, Text::DataNotReady, Text::Unjoined, Text::WaitingData});
    const float action = widest({Text::ActionColumn, Text::PrepareAndAdd, Text::RevealReserve,
        Text::AddReserve, Text::WaitingExecution, Text::ConfirmAgain, Text::AdjustInGame});
    float feature = 0;
    for (size_t index = 0; index < featureCount; ++index) {
        feature = std::max(feature, widthOf(Tr(featureTexts[index])));
        result.featureState = std::max(result.featureState, widthOf(FeatureStateLabel(snapshot, index).c_str()));
    }
    const auto& style = ImGui::GetStyle();
    const float cell = style.CellPadding.x * 2 + 10 * scale;
    result.name += cell; result.level += cell; result.state += cell; result.featureState += cell;
    const bool pad = PanelUsingController();
    const float shortcutLeft = std::max(widthOf(pad ? "View + LS" : "F11") + widthOf(Tr(Text::ShowHide)),
        widthOf(pad ? "A" : "Enter") + widthOf(Tr(Text::ToggleConfirm)));
    const float shortcutRight = std::max(widthOf(pad ? Tr(Text::DpadUpDown) : "↑ / ↓") + widthOf(Tr(Text::Select)),
        widthOf(pad ? "B" : "Esc") + widthOf(Tr(Text::Close)));
    const float padding = style.WindowPadding.x * 2 + style.ScrollbarSize + 24 * scale;
    // 名单位于独立 child 内，除了外窗还需再扣一组 WindowPadding。遗漏此项会
    // 让字体较大的法语动作列少约26px，即便整扇窗口没有横向溢出仍会裁掉末尾。
    const float rosterPadding = style.WindowPadding.x * 2;
    const float content = std::max({feature + result.featureState + cell,
        result.name + result.level + result.state + action + cell + rosterPadding,
        2 * std::max(shortcutLeft, shortcutRight) + style.ItemSpacing.x * 4});
    result.width = std::min(std::max(700 * scale, content + padding), ImGui::GetIO().DisplaySize.x - 28);
    return result;
}

std::string FooterMessage(const ControlSnapshot& snapshot, size_t roleIndex, uint64_t now) {
    if (roleIndex < snapshot.roster.members.size() &&
        confirmation.Armed(snapshot.roster.members[roleIndex].id, snapshot.roster.generation, now)) {
        const auto& member = snapshot.roster.members[roleIndex];
        char text[640]{};
        const auto name = DisplayCharacter(member.id);
        std::snprintf(text, sizeof(text), Tr(Text::ConfirmRoleFormat), name.c_str(), RoleAction(member));
        return text;
    }
    if (submissionRejected) return Tr(Text::StateChanged);
    if (snapshot.roster.lastResult != RosterResult::None) return RosterResultText(snapshot.roster.lastResult);
    if (!(snapshot.appliedFeatures & FeatureUnjoined)) return Tr(Text::EnableUnjoinedNote);
    if (!snapshot.roster.canEdit) return RosterBlockReasonText(snapshot.roster.blockReason);
    return Tr(Text::RoleUsageNote);
}

void DrawPanel(uint32_t actions, const ControlSnapshot& snapshot, float scale) {
    const uint64_t now = GetTickCount64();
    const bool visible = PanelOpen();
    const bool generationChanged = previousGeneration != snapshot.roster.generation;
    if (!visible || !wasOpen || generationChanged || !snapshot.roster.canEdit)
        confirmation.Cancel();
    if (generationChanged) submissionRejected = false;
    previousGeneration = snapshot.roster.generation;
    wasOpen = visible;
    if (!visible) return;
    constexpr size_t rowCount = featureCount + kRosterDefinitions.size();
    if ((actions & (panelinput::Previous | panelinput::Next)) == panelinput::Previous)
        ChangeSelection((selected + rowCount - 1) % rowCount);
    if ((actions & (panelinput::Previous | panelinput::Next)) == panelinput::Next)
        ChangeSelection((selected + 1) % rowCount);
    if (actions & panelinput::Activate) Activate(snapshot, now);

    auto& io = ImGui::GetIO();
    const auto layout = MeasurePanel(snapshot, scale);
    const float width = layout.width;
    const float height = std::min(870.0f * scale, io.DisplaySize.y - 28.0f);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - width) / 2, (io.DisplaySize.y - height) / 2), ImGuiCond_Always);
    const auto flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Sky2PartyEditorPanel", nullptr, flags)) {
        ImGui::TextColored(accent, Tr(Text::TitleFormat), SKY2_PARTY_VERSION);
        const float closeWidth = ImGui::CalcTextSize(Tr(Text::Close)).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeWidth);
        const auto closeLabel = std::string(Tr(Text::Close)) + "###close";
        if (ImGui::SmallButton(closeLabel.c_str())) SetPanelOpen(false);
        const auto language = static_cast<unsigned>(CurrentLanguage());
        if (language < kLanguageCount && !fontComplete[language])
            ImGui::TextWrapped("Font glyphs are missing. Install the matching Windows supplemental fonts and restart.");
        if (!GameNamesReady(CurrentLanguage())) ImGui::TextWrapped("%s", Tr(Text::GameNamesUnavailable));
        ImGui::Separator();
        if (ImGui::BeginTable("features", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(Tr(Text::FeatureColumn), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(Tr(Text::StatusColumn), ImGuiTableColumnFlags_WidthFixed, layout.featureState);
            for (size_t index = 0; index < featureCount; ++index) {
                const auto bit = 1u << index;
                const bool requested = (snapshot.requestedFeatures & bit) != 0;
                const bool applied = (snapshot.appliedFeatures & bit) != 0;
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(index));
                if (DrawSelectionRow(Tr(featureTexts[index]), selected == index)) {
                    ChangeSelection(index); Activate(snapshot, now);
                }
                ImGui::PopID(); ImGui::TableSetColumnIndex(1);
                const auto color = !snapshot.appliedStateKnown || requested != applied ? warning :
                    (applied ? accent : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextColored(color, "%s", FeatureStateLabel(snapshot, index).c_str());
            }
            ImGui::EndTable();
        }
        ImGui::TextWrapped("%s", snapshot.message != ControlMessage::None ?
            ControlMessageText(snapshot.message) : Tr(Text::ControlDefault));
        DrawFixedMemberWarning(snapshot);
        ImGui::Separator();
        const auto* reserveLabel = GameTermFor(CurrentLanguage(), GameTerm::ReserveMembers);
        const char* roleHeading = reserveLabel ? reserveLabel : Tr(Text::RoleSection);
        ImGui::TextColored(accent, "%s", roleHeading);
        if (ImGui::CalcTextSize(roleHeading).x + ImGui::CalcTextSize(Tr(Text::RoleSectionNote)).x +
            ImGui::GetStyle().ItemSpacing.x < ImGui::GetContentRegionAvail().x) ImGui::SameLine();
        ImGui::TextWrapped("%s", Tr(Text::RoleSectionNote));

        // 列表单独滚动，底部操作说明保持可见；键盘/手柄选择自动滚动到当前行。
        const auto roleIndex = selected >= featureCount ? selected - featureCount : snapshot.roster.members.size();
        const bool preparationSelected = roleIndex < snapshot.roster.members.size() &&
            snapshot.roster.members[roleIndex].needsPreparation;
        const auto footerMessage = FooterMessage(snapshot, roleIndex, now);
        const float wrap = ImGui::GetContentRegionAvail().x;
        const auto wrappedHeight = [&](const char* text) {
            return ImGui::CalcTextSize(text, nullptr, false, wrap).y + ImGui::GetStyle().ItemSpacing.y;
        };
        // 为实际译文预留高度。长德/法文与当前确认提示不再依赖中文的固定行数，
        // 列表独立滚动，底部操作说明和高亮快捷键保持可见。
        const float footer = wrappedHeight(footerMessage.c_str()) + wrappedHeight(Tr(Text::SaveNote)) +
            (preparationSelected ? wrappedHeight(Tr(Text::PrepareWeaponNote)) + wrappedHeight(Tr(Text::PrepareCraftNote)) : 0) +
            (!PanelControllerReady() ? wrappedHeight(PanelInputStatus()) : 0) +
            2 * ImGui::GetTextLineHeightWithSpacing() + 2 * ImGui::GetStyle().CellPadding.y * 2 + 20 * scale;
        const float listHeight = std::max(90.0f * scale, ImGui::GetContentRegionAvail().y - footer);
        if (ImGui::BeginChild("members", ImVec2(0, listHeight), ImGuiChildFlags_Borders)) {
            if (ImGui::BeginTable("roster", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn(Tr(Text::CharacterColumn), ImGuiTableColumnFlags_WidthFixed, layout.name);
                ImGui::TableSetupColumn(LevelHeading(), ImGuiTableColumnFlags_WidthFixed, layout.level);
                ImGui::TableSetupColumn(Tr(Text::StatusColumn), ImGuiTableColumnFlags_WidthFixed, layout.state);
                ImGui::TableSetupColumn(Tr(Text::ActionColumn), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (size_t index = 0; index < snapshot.roster.members.size(); ++index) {
                    const auto& member = snapshot.roster.members[index];
                    const size_t position = featureCount + index;
                    const bool available = CanAdd(snapshot, member);
                    const bool armed = confirmation.Armed(member.id, snapshot.roster.generation, now);
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(static_cast<int>(position));
                    const auto name = DisplayCharacter(kRosterDefinitions[index].id);
                    if (DrawSelectionRow(name.c_str(), selected == position)) {
                        ChangeSelection(position); Activate(snapshot, now);
                    }
                    if (selected == position && scrollSelection) { ImGui::SetScrollHereY(0.5f); scrollSelection = false; }
                    ImGui::PopID(); ImGui::TableSetColumnIndex(1);
                    if (member.level) ImGui::Text("%u", member.level); else ImGui::TextDisabled("—");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextDisabled("%s", snapshot.roster.ready ? RoleState(member) : Tr(Text::WaitingData));
                    ImGui::TableSetColumnIndex(3);
                    if (snapshot.roster.pendingId == member.id && member.id != kNoRosterId)
                        ImGui::TextColored(warning, "%s", Tr(Text::WaitingExecution));
                    else if (armed) ImGui::TextColored(warning, "%s", Tr(Text::ConfirmAgain));
                    else if (available) ImGui::TextColored(accent, "%s", RoleAction(member));
                    else if (member.inParty && !member.hidden) ImGui::TextDisabled("%s", Tr(Text::AdjustInGame));
                    else ImGui::TextDisabled("—");
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        const bool needsAttention = submissionRejected || (roleIndex < snapshot.roster.members.size() &&
            confirmation.Armed(snapshot.roster.members[roleIndex].id, snapshot.roster.generation, now));
        ImGui::PushStyleColor(ImGuiCol_Text, needsAttention ? warning : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::TextWrapped("%s", footerMessage.c_str());
        ImGui::PopStyleColor();
        if (preparationSelected) {
            ImGui::TextWrapped("%s", Tr(Text::PrepareWeaponNote));
            ImGui::TextWrapped("%s", Tr(Text::PrepareCraftNote));
        }
        ImGui::TextWrapped("%s", Tr(Text::SaveNote));
        ImGui::Separator();
        if (ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_SizingStretchSame)) {
            const bool pad = PanelUsingController();
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            Hint(pad ? "View + LS" : "F11", Tr(Text::ShowHide));
            ImGui::TableSetColumnIndex(1); Hint(pad ? Tr(Text::DpadUpDown) : "↑ / ↓", Tr(Text::Select));
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            Hint(pad ? "A" : "Enter", Tr(Text::ToggleConfirm));
            ImGui::TableSetColumnIndex(1); Hint(pad ? "B" : "Esc", Tr(Text::Close));
            ImGui::EndTable();
        }
        if (!PanelControllerReady()) ImGui::TextWrapped("%s", PanelInputStatus());
    }
    ImGui::End();
    if (!PanelOpen()) { confirmation.Cancel(); wasOpen = false; }
}

HRESULT WINAPI Present(IDXGISwapChain* swap, UINT interval, UINT options) {
    if (renderReady.load(std::memory_order_acquire) && !(options & DXGI_PRESENT_TEST)) {
        std::lock_guard<std::recursive_mutex> guard(renderLock);
        ContextScope restore;
        try {
            if (context || InitializeGui(swap)) {
                DXGI_SWAP_CHAIN_DESC description{};
                if (SUCCEEDED(swap->GetDesc(&description)) && description.OutputWindow == gameWindow) {
                    ImGui::SetCurrentContext(context);
                    // 读取游戏文字语言并在本帧统一使用；不根据手柄或系统区域猜测。
                    RefreshGameLanguage();
                    PumpPanelKeyboard();
                    const auto actions = ConsumePanelActions();
                    const auto snapshot = ReadControlSnapshot();
                    // 隐藏时不提交绘制，但仍泵键盘并取消确认；不能停止 F11 的入口检测。
                    if (!PanelOpen()) {
                        confirmation.Cancel(); wasOpen = false;
                        // 隐藏期间不接收游戏的输入消息，故不能等待其 mouse-up/key-up
                        // 来清理旧状态。只清本插件独立上下文，避免重开时延续旧鼠标按压。
                        auto& io = ImGui::GetIO();
                        io.ClearEventsQueue(); io.ClearInputKeys(); io.ClearInputMouse();
                        io.MouseDrawCursor = false;
                    }
                    else {
                        ID3D11Texture2D* buffer = nullptr;
                        if (SUCCEEDED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer)))) {
                            D3D11_TEXTURE2D_DESC size{};
                            buffer->GetDesc(&size);
                            RenderView target;
                            const auto targetResult = device->CreateRenderTargetView(buffer, nullptr, &target.value);
                            buffer->Release();
                            if (SUCCEEDED(targetResult) && target.value) {
                                ImGui_ImplDX11_NewFrame();
                                ImGui_ImplWin32_NewFrame();
                                auto& io = ImGui::GetIO();
                                // 鼠标坐标使用 Win32 客户区逻辑单位，实际后缓冲只决定渲染密度。
                                io.DisplayFramebufferScale = ImVec2(size.Width / std::max(io.DisplaySize.x, 1.0f),
                                    size.Height / std::max(io.DisplaySize.y, 1.0f));
                                io.MouseDrawCursor = !PanelUsingController();
                                const float scale = std::clamp(io.DisplaySize.y / 1080.0f, 0.78f, 1.25f);
                                ImGui::GetStyle() = baseStyle;
                                ImGui::GetStyle().ScaleAllSizes(scale);
                                ImGui::GetStyle().FontScaleMain = scale;
                                ImGui::NewFrame();
                                DrawPanel(actions, snapshot, scale);
                                ImGui::Render();
                                {
                                    // DX11 后端还原管线状态；此层补齐全部颜色/深度输出目标的还原。
                                    OutputTargets targets(target.value);
                                    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                                }
                            }
                        }
                    }
                }
            }
        } catch (...) {
            SetPanelOpen(false); confirmation.Cancel();
            static bool reported = false;
            if (!reported) { Log("Party panel: drawing exception contained; panel closed."); reported = true; }
        }
    }
    // 必须在释放自有渲染锁并恢复上下文后进入下一层 Present，避免和 Chest 的锁交叉。
    return nextPresent(swap, interval, options);
}
}

bool InstallPanel(HMODULE module, uintptr_t executableBase) noexcept {
    // 必须在创建自己的 Present trampoline 之前等待 Chest，而非 Create 之后才等待。
    // 超时只放弃本面板，不覆盖 Chest 已在安装或尚未完成的入口。
    if (!WaitForChestPresent(module, executableBase)) return false;
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
    Log("Party panel: DXGI Present chain installed; F11 / View+LS.");
    return true;
}
}
