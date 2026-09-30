// Hub 模块第三入口：ASI 和 Standalone 继续保留各自加载行为；本入口不注入图形/输入。
#include "runtime.h"
#include "hub_panel.h"
#include "control_state.h"
#include "ui_text.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <mutex>

// 公共 MinHook 适配头使用此指针，把本模块游戏挂钩交给 Hub 的单一管理器。
const Sky2HostApi* Sky2Hub_Host = nullptr;

namespace {
int32_t SKY2_CALL RequestEnabled(int32_t enabled) noexcept {
    if(!sky2party::RequestModuleActivity(enabled!=0))return 0;
    sky2party::HubVisibilityChanged(0);return 1;
}
int32_t SKY2_CALL ActivityState() noexcept {return sky2party::ModuleActivityState();}
const char* SKY2_CALL ActivityMessage() noexcept {
    using namespace sky2party;
    // 复用已有八语语义文案；查询只读取原子状态，不保存、不访问游戏对象。
    switch(ModuleActivityMessage()){
    case ControlMessage::FixedMembersNeedRestoring:return Tr(Text::ControlFixedBlocked);
    case ControlMessage::CannotVerifyParties:return Tr(Text::ControlFixedUnknown);
    case ControlMessage::ApplyFailed:return Tr(Text::ControlApplyFailed);
    case ControlMessage::ServiceError:return Tr(Text::ControlException);
    case ControlMessage::AppliedNotSaved:return Tr(Text::ControlSaveFailed);
    case ControlMessage::WaitingForExploration:return Tr(Text::ControlWaiting);
    default:return "";
    }
}
int32_t SKY2_CALL InitializeModule(const Sky2HostApi* host) noexcept {
    // 在读取后续字段之前先校验结构大小。ABI v1 的必需前缀止于
    // is_any_item_active；卡片/分栏是可选尾部，旧宿主通过 SDK 线性回退。
    constexpr size_t minimumUiSize = offsetof(Sky2UiApi, is_any_item_active) + sizeof(Sky2UiApi::is_any_item_active);
    if (!host || host->size < sizeof(Sky2HostApi) || host->abi != SKY2_HUB_ABI ||
        !host->owner || !host->module_handle || !host->game_base || !host->data_directory ||
        !host->ui || host->ui->size < minimumUiSize || !host->log || !host->register_action ||
        !host->open_page || !host->language || !host->create_hook || !host->enable_hook ||
        !host->disable_hook || !host->remove_hook || !host->ui->text || !host->ui->text_wrapped ||
        !host->ui->separator || !host->ui->checkbox || !host->ui->button ||
        !host->ui->selectable || !host->ui->begin_disabled || !host->ui->end_disabled ||
        !host->ui->begin_child || !host->ui->end_child) return 0;
    static std::once_flag once;
    static bool initialized = false;
    try {
        std::call_once(once, [host] {
            Sky2Hub_Host = host;
            // 先完成全部动作注册，随后才启动可能被游戏线程观察到的业务协调器。
            // 任一注册失败都不能留下正在应用代码修改但没有可用页面的模块。
            // 若后续版本/设置校验失败，宿主会丢弃该 owner 的未发布动作。
            initialized = sky2party::RegisterHubActions() &&
                sky2party::InitializeHubRuntime(static_cast<HMODULE>(host->module_handle), host->data_directory);
            host->log(host->owner, initialized ? "Party module initialized." :
                "Party module initialization rejected; see Sky2PartyEditor diagnostics.");
        });
    } catch (...) {
        return 0; // 异常不得越过模块 ABI；宿主继续持有已经映射的代码。
    }
    return initialized && Sky2Hub_Host == host;
}
}

extern "C" __declspec(dllexport) int32_t SKY2_CALL Sky2Module_Query(
    uint32_t requestedAbi, Sky2ModuleApi* output) noexcept {
    constexpr size_t minimumModuleSize=offsetof(Sky2ModuleApi,request_enabled);
    if (requestedAbi != SKY2_HUB_ABI || !output || output->size < minimumModuleSize) return 0;
    // 查询阶段仅写静态元信息，不访问游戏、读配置或启动任何工作线程。
    const uint32_t capacity=output->size;
    const Sky2ModuleApi result{static_cast<uint32_t>(std::min<size_t>(capacity,sizeof(Sky2ModuleApi))), SKY2_HUB_ABI, "party", "队伍编辑", SKY2_PARTY_VERSION,
        "Sky2PartyEditor.asi", "Sky2PartyEditor", &InitializeModule,
        &sky2party::TickHubPanel, &sky2party::DrawHubPanel, nullptr, &sky2party::HubVisibilityChanged,
        &RequestEnabled,&ActivityState,&ActivityMessage,&sky2party::DrawHubHeader};
    std::memcpy(output,&result,std::min<size_t>(capacity,sizeof(result)));
    return 1;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
