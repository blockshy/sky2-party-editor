// 面板与游戏线程之间的角色加入服务。界面仅读取值快照、提交单人请求，
// 不获得游戏指针，也不能在 Present/初始化线程调用游戏的 Party::Join。
#pragma once
#include "roster_policy.h"

namespace sky2party {
// 普通探索可以应用 Mod 开关，但当前剧情仍可能禁止改队伍；两种权限必须区分。
enum class RosterBlockReason : uint8_t { None, NotExploring, FormationStoryLock, NativeEntryLock };
struct RosterEntry {
    uint32_t id = kNoRosterId;
    uint32_t level = 0;
    bool guest = false;
    bool inParty = false;
    bool hidden = false;
    bool unavailable = false;
    bool initialized = false;
    bool needsPreparation = false;
    bool canAdd = false;
};
struct RosterSnapshot {
    std::array<RosterEntry, 14> members{};
    bool ready = false;
    bool canEdit = false;
    bool allowUnjoined = false;
    RosterBlockReason blockReason = RosterBlockReason::NotExploring;
    uint64_t capturedAtMs = 0;
    uint64_t generation = 0;
    uint32_t partyIndex = 0;
    uint32_t pendingId = kNoRosterId;
    uint32_t lastId = kNoRosterId;
    RosterResult lastResult = RosterResult::None;
};

// 完整宿主哈希校验之后调用；这里只核对 Join 函数字节、保存基址，不操作游戏数据。
bool ConfigureRosterService(uintptr_t executableBase) noexcept;
RosterSnapshot ReadRosterSnapshot() noexcept;
bool QueueAddMember(uint32_t id) noexcept;
// 停用请求到来时先关闭新请求入口并作废未消费队列。已进入原生调用的操作
// 不强行中断；协调器在同一游戏线程下一次安全帧才确认整个模块已经停用。
void SetRosterRequestsEnabled(bool enabled) noexcept;
bool RosterServiceIdle() noexcept;

// 调用者必须在原生 0x2D4D40(context, mask) 返回之后调用，传入它的原始返回值。
// 任何原生输入已消费的帧均不执行加入。设置队列用独立谓词：编成专属剧情锁
// 不妨碍普通探索时应用代码门槛开关；真实名单变化另按下面的已应用许可判定。
bool IsFeatureEditingSafe(uintptr_t inputContext, uint32_t actionMask, bool nativeHandled) noexcept;
// allowAnywhere必须来自已经完整应用的“随处编成”状态，不能用UI的待应用期望值。
// 开启时与原生入口一致忽略119；30是实际成员选择限制，仍必须保留。
bool IsRosterEditingSafe(uintptr_t inputContext, uint32_t actionMask, bool nativeHandled,
    bool allowAnywhere = false) noexcept;
void TickRosterOnGameThread(uintptr_t inputContext, uint32_t actionMask, bool nativeHandled,
    bool allowUnjoined, bool allowAnywhere = false) noexcept;
const char* RosterResultText(RosterResult result) noexcept;
const char* RosterBlockReasonText(RosterBlockReason reason) noexcept;
}
