// 在本测试进程拥有的虚拟内存里构造最小游戏对象，用来验证线程队列边界。
// 不打开真实游戏进程，不调用原生 Join，不读取或写入真实存档。
#include <Windows.h>
#include "roster_service.h"
#include "roster_prepare.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
// 本测试只隔离服务调度，补足逻辑由 mock 显式模拟；真实补足函数另有签名和策略测试。
sky2party::RosterPreparationResult preparationMode = sky2party::RosterPreparationResult::Prepared;
int preparationCalls = 0;
}
namespace sky2party {
void Log(const char*) noexcept {}
bool ConfigureRosterPreparation(uintptr_t) noexcept { return true; }
RosterPreparationResult PrepareRosterRecordOnGameThread(uint32_t, uintptr_t record) noexcept {
    ++preparationCalls;
    if (preparationMode == RosterPreparationResult::Failed) return preparationMode;
    if (*reinterpret_cast<uint32_t*>(record + 0x264) == 0) *reinterpret_cast<uint32_t*>(record + 0x264) = 500;
    if (preparationMode == RosterPreparationResult::PartialPrepared) return preparationMode;
    *reinterpret_cast<uint8_t*>(record + 0x16C) = 1;
    return preparationMode;
}
}
using namespace sky2party;
namespace {
int checks = 0, failed = 0;
int joinCalls = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { ++failed; std::fprintf(stderr, "FAILED: %s\n", message); }
}
template<class T> void Put(uintptr_t address, T value) { std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value)); }
uint32_t ReadFlags(uintptr_t members, size_t i) { return *reinterpret_cast<uint32_t*>(members + i * 8 + 4); }
void __fastcall FakeNativeJoin(uintptr_t party, uint32_t id, uint32_t flags) {
    // 这是测试自己的函数，模拟标准加入的最小名单效果；不会执行任何游戏代码。
    ++joinCalls;
    auto& count = *reinterpret_cast<uint32_t*>(party + 0x208);
    const auto members = *reinterpret_cast<uintptr_t*>(party + 0x200);
    Put(members + count * 8, id); Put(members + count * 8 + 4, flags); ++count;
}
}
int main() {
    auto image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0xC61000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!image) return 2;
    const auto base = reinterpret_cast<uintptr_t>(image);
    constexpr uint8_t joinSignature[]{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,
        0x57,0x48,0x83,0xEC,0x20,0x41,0x8B,0xF0,0x8B,0xFA,0x48,0x8B,0xD9,
        0x81,0xFA,0xFF,0xFF,0x00,0x00,0x75,0x30};
    std::memcpy(image + 0x2C8ED0, joinSignature, sizeof(joinSignature));
    std::vector<uint8_t> fieldData(0x2000), storyData(0x12000), statusData(0x130000), runtimeData(0x3000),
        environmentData(0x400), fieldStateData(0x400), inputData(0x400), partyData(0x218 * 4), membersData(64 * 8);
    const auto field = reinterpret_cast<uintptr_t>(fieldData.data());
    const auto story = reinterpret_cast<uintptr_t>(storyData.data());
    const auto status = reinterpret_cast<uintptr_t>(statusData.data());
    const auto input = reinterpret_cast<uintptr_t>(inputData.data());
    const auto party = reinterpret_cast<uintptr_t>(partyData.data());
    const auto members = reinterpret_cast<uintptr_t>(membersData.data());
    Put(base + 0xC60E08, field); Put(base + 0xC60E58, story); Put(base + 0xC60E50, status);
    Put(base + 0xC5D768, reinterpret_cast<uintptr_t>(runtimeData.data()));
    Put(base + 0xC60E68, reinterpret_cast<uintptr_t>(environmentData.data()));
    Put(field + 0x650, party); Put(field + 0x648, field + 0x1000);
    Put(field + 0x6A8, reinterpret_cast<uintptr_t>(fieldStateData.data()));
    std::memcpy(reinterpret_cast<void*>(field + 0x170), "mp0000", 7);
    Put(input + 0x60, field + 0x1100); Put<uint8_t>(input + 0x340, 1); Put<uint32_t>(input + 0x320, 0xFFFF);
    Put(party + 0x200, members); Put<uint32_t>(party + 0x208, 14); Put<uint32_t>(party + 0x214, 8);
    for (size_t i = 0; i < 14; ++i) {
        const auto id = kRosterDefinitions[i].id;
        Put(members + i * 8, id); Put<uint32_t>(members + i * 8 + 4, i == 0 ? 0x20 : 0x8C0);
        const auto record = status + 0x1142F8 + (i + 1) * 0x2A0;
        Put(record, id); Put<uint32_t>(record + 4, 79); Put<uint32_t>(record + 0x10, 7000);
        Put<uint32_t>(record + 0x18, 500); Put<uint32_t>(record + 0x20, 200);
        Put<uint32_t>(record + 0x264, 500); Put<uint8_t>(record + 0x16C, 1);
    }
    // 未使用的槽必须像游戏空白槽一样写 FFFF，避免测试自己制造第二条 ID 0。
    Put<uint32_t>(status + 0x1142F8, 0xFFFF);
    for (size_t slot = 15; slot < 100; ++slot) Put<uint32_t>(status + 0x1142F8 + slot * 0x2A0, 0xFFFF);
    Check(ConfigureRosterService(base), "只校验函数签名即可配置，无原生调用");
    // 签名验证以后，只在测试进程自有的一页里安装到 FakeNativeJoin 的绝对跳转。
    // 这样检验服务真正传出的 x64 参数，而不复制或运行游戏 Join 的机器码。
    DWORD oldProtection = 0;
    if (!VirtualProtect(image + 0x2C8000, 0x2000, PAGE_EXECUTE_READWRITE, &oldProtection)) return 3;
    const auto mockJoin = reinterpret_cast<uintptr_t>(&FakeNativeJoin);
    const uint8_t jumpBegin[]{0x48, 0xB8}, jumpEnd[]{0xFF, 0xE0};
    std::memcpy(image + 0x2C8ED0, jumpBegin, 2);
    std::memcpy(image + 0x2C8ED2, &mockJoin, 8);
    std::memcpy(image + 0x2C8EDA, jumpEnd, 2);
    FlushInstructionCache(GetCurrentProcess(), image + 0x2C8ED0, 12);
    Check(IsRosterEditingSafe(input, 0x1BF, false), "普通探索条件被接受");
    Check(!IsRosterEditingSafe(input, 0x100, false), "动作组未允许编成时拒绝");
    Check(!IsRosterEditingSafe(input, 0x1BF, true), "原生已处理输入的帧拒绝");
    Put<uintptr_t>(input + 0xB8, field);
    Check(!IsRosterEditingSafe(input, 0x1BF, false), "受限动作上下文拒绝");
    Put<uintptr_t>(input + 0xB8, 0);
    for (const auto [offset, bit] : std::array<std::pair<uint32_t, uint32_t>, 4>{{{0x100,2},{0x101,1},{0x103,0x40},{0x10E,0x80}}}) {
        Put<uint8_t>(story + offset, static_cast<uint8_t>(bit));
        Check(!IsRosterEditingSafe(input, 0x1BF, false), "剧情/菜单禁用位必须保留");
        const bool formationOnly = offset == 0x103 || offset == 0x10E;
        Check(IsFeatureEditingSafe(input, 0x1BF, false) == formationOnly,
            "编成专属30/119不阻止应用设置，输入/菜单1/8仍阻止");
        Check(IsRosterEditingSafe(input, 0x1BF, false, true) == (offset == 0x10E),
            "随处编成仅覆盖119入口位，不能覆盖30/输入/菜单禁用");
        if (formationOnly) {
            TickRosterOnGameThread(input, 0x1BF, false, true);
            const auto locked = ReadRosterSnapshot();
            Check(locked.ready && !locked.canEdit && !locked.members[3].canAdd,
                "编成锁定时仍可只读查看角色，但不能加入");
            Check(locked.blockReason == (offset == 0x103 ? RosterBlockReason::FormationStoryLock : RosterBlockReason::NativeEntryLock),
                "界面能准确区分剧情编成锁与原生入口锁");
            Check(!QueueAddMember(3), "锁定时服务拒绝直接提交角色请求");
        }
        Put<uint8_t>(story + offset, 0);
    }
    TickRosterOnGameThread(input, 0x1BF, false, true);
    auto snapshot = ReadRosterSnapshot();
    Check(snapshot.ready && snapshot.canEdit && snapshot.members[3].canAdd, "游戏线程发布可加入快照");
    const auto beforeStatus = statusData;
    Check(QueueAddMember(3), "显式单人请求排队");
    Check(!QueueAddMember(101), "尚未执行时不接受第二条请求");
    Check(ReadFlags(members, 3) == 0x8C0, "队列和界面线程尚未改动数据");
    TickRosterOnGameThread(input, 0x1BF, false, true);
    snapshot = ReadRosterSnapshot();
    Check(ReadFlags(members, 3) == 0xC0, "只有游戏线程清除请求角色的隐藏位");
    Check(ReadFlags(members, 10) == 0x8C0, "其他隐藏角色保持不变");
    Check(statusData == beforeStatus, "已有培养完整字节保持不变");
    Check(snapshot.lastResult == RosterResult::RevealedReserve, "明确反馈已显示后备");
    Check(!QueueAddMember(3), "已显示成员不能再次加入或改主力位置");

    Check(QueueAddMember(101), "编成锁到来前的角色请求先排队");
    Put<uint8_t>(story + 0x10E, 0x80);
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(ReadFlags(members, 10) == 0x8C0 && ReadRosterSnapshot().ready &&
        ReadRosterSnapshot().lastResult == RosterResult::UnsafeState,
        "119锁出现取消排队，同时保留可查看的角色数据");
    Put<uint8_t>(story + 0x10E, 0);
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(ReadFlags(members, 10) == 0x8C0, "剧情解除锁后不执行遗留请求");

    Check(QueueAddMember(101), "第二个角色可单独请求");
    Put<uint32_t>(field + 0x1BC8, 1);
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Put<uint32_t>(field + 0x1BC8, 0);
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(ReadFlags(members, 10) == 0x8C0, "换图取消旧请求，恢复探索不重试");
    Check(QueueAddMember(101), "重新确认可重新排队");
    std::memcpy(reinterpret_cast<void*>(field + 0x170), "mp0010", 7);
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(ReadFlags(members, 10) == 0x8C0, "同一个地图结构改为新场景时取消请求");
    Check(ReadRosterSnapshot().lastResult == RosterResult::StaleRequest, "上下文变化有明确反馈");
    Check(QueueAddMember(101), "当前新场景可以重新确认");
    TickRosterOnGameThread(input, 0x1BF, false, false);
    Check(ReadFlags(members, 10) == 0x8C0 && !QueueAddMember(101), "关闭未入队功能取消排队并阻止新请求");
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(QueueAddMember(101), "重新开启后允许新的明确请求");
    Put<uint32_t>(members + 10 * 8 + 4, 0x840);
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(ReadFlags(members, 10) == 0x840, "剧情在提交后改名单时拒绝，不写回旧 flags");

    const auto emptyWeapon = status + 0x1142F8 + 12 * 0x2A0; // ID 106 在白名单下标 11。
    Put<uint32_t>(emptyWeapon + 0x264, 0);
    Put<uint32_t>(members + 11 * 8 + 4, 0x840);
    Put<uint8_t>(story + 0x10E, 0x80);
    TickRosterOnGameThread(input, 0x1BF, false, true, false);
    Check(ReadRosterSnapshot().ready && !ReadRosterSnapshot().canEdit && !QueueAddMember(106),
        "真实840隐藏后备与119组合：随处关闭时仍可查看，但不能加入");
    TickRosterOnGameThread(input, 0x1BF, false, true, true);
    snapshot = ReadRosterSnapshot();
    Check(snapshot.members[11].needsPreparation && snapshot.members[11].canAdd, "空武器但可补足者有明确状态");
    Check(snapshot.blockReason == RosterBlockReason::None, "随处已应用后119不再误报角色不可操作");
    Check(QueueAddMember(106), "补足需求也只能逐人显式确认");
    Check(preparationCalls == 0, "排队阶段不调用补足函数");
    TickRosterOnGameThread(input, 0x1BF, false, true, true);
    Check(preparationCalls == 1 && ReadFlags(members, 11) == 0x40, "119场景下先补足，再将840显示为40后备");
    Check(*reinterpret_cast<uint8_t*>(story + 0x10E) == 0x80, "成功加入不清除实际119剧情位");
    Check(ReadRosterSnapshot().lastResult == RosterResult::PreparedAndRevealed, "反馈区分补足后加入与保持培养加入");
    Check(*reinterpret_cast<uint32_t*>(emptyWeapon + 4) == 79, "补足流程不重置等级");
    Check(!QueueAddMember(106), "成功后重复请求不会再次补物品");

    Check(QueueAddMember(107), "允许时可提交另一名隐藏后备");
    TickRosterOnGameThread(input, 0x1BF, false, true, false);
    Check(ReadFlags(members, 12) == 0x8C0 && !ReadRosterSnapshot().canEdit,
        "关闭随处后119立即恢复并取消尚未执行的加入");
    TickRosterOnGameThread(input, 0x1BF, false, true, true);
    Check(ReadFlags(members, 12) == 0x8C0, "重新开启不复活被取消的请求");
    Put<uint8_t>(story + 0x103, 0x40);
    TickRosterOnGameThread(input, 0x1BF, false, true, true);
    Check(ReadRosterSnapshot().ready && !ReadRosterSnapshot().canEdit &&
        ReadRosterSnapshot().blockReason == RosterBlockReason::FormationStoryLock,
        "119覆盖许可不能绕过30实际编成剧情锁");
    Put<uint8_t>(story + 0x103, 0);
    Check(!IsRosterEditingSafe(input, 0x1BF, true, true), "随处开启仍不能在原生已消费输入帧改队伍");
    Put<uintptr_t>(field + 0x718, field);
    Check(!IsRosterEditingSafe(input, 0x1BF, false, true), "随处开启仍不能在Camp存在时改真实队伍");
    Put<uintptr_t>(field + 0x718, 0);
    Put<uint8_t>(story + 0x10E, 0);

    const auto emptyBoth = status + 0x1142F8 + 14 * 0x2A0; // ID 112。
    Put<uint32_t>(emptyBoth + 0x264, 0); Put<uint8_t>(emptyBoth + 0x16C, 0);
    TickRosterOnGameThread(input, 0x1BF, false, true);
    preparationMode = RosterPreparationResult::PartialPrepared;
    Check(QueueAddMember(112), "部分补足失败的测试请求可排队");
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(ReadFlags(members, 13) == 0x8C0, "部分补足时不能继续放入可用名单");
    Check(ReadRosterSnapshot().lastResult == RosterResult::PartiallyPrepared, "不虚称部分补足已经回滚");
    const int partialCalls = preparationCalls;
    preparationMode = RosterPreparationResult::Prepared;
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(preparationCalls == partialCalls, "失败后不会每帧自动补足或加入");
    Check(QueueAddMember(112), "玩家明确重试后才继续补足");
    preparationMode = RosterPreparationResult::Failed;
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(ReadRosterSnapshot().lastResult == RosterResult::PreparationFailed && ReadFlags(members, 13) == 0x8C0,
        "补足拒绝有单独结果且不清隐藏位");

    Put<uint32_t>(status + 0x1142F8 + 0x2A0 + 0x264, 0); // ID 0 是已可见主力。
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(!ReadRosterSnapshot().members[0].canAdd && !QueueAddMember(0), "可见成员卸武器不会变成加入/生成物品入口");

    // 将已培养的阵从测试名单移除，实际走服务的“调用原生加入”分支。
    std::memmove(reinterpret_cast<void*>(members + 7 * 8), reinterpret_cast<void*>(members + 8 * 8), 6 * 8);
    Put<uint32_t>(party + 0x208, 13);
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(QueueAddMember(7), "已培养但未登记的角色允许排队");
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(joinCalls == 1 && *reinterpret_cast<uint32_t*>(party + 0x208) == 14, "实际通过 x64 函数接口调用测试 Join");
    Check(*reinterpret_cast<uint32_t*>(members + 13 * 8) == 7 && ReadFlags(members, 13) == 0x40,
        "加入调用只传后备 flags40，不增加主力");
    Check(ReadRosterSnapshot().lastResult == RosterResult::AddedReserve, "完整核对加入后的名单后报告成功");
    Check(ReadFlags(members, 0) == 0x20, "加入后原有固定主力保持不变");
    Put<uint32_t>(party + 0x208, 65);
    TickRosterOnGameThread(input, 0x1BF, false, true);
    Check(!ReadRosterSnapshot().ready, "异常容量不越界采样");
    VirtualFree(image, 0, MEM_RELEASE);
    std::printf("%d roster service checks, %d failures\n", checks, failed);
    return failed ? 1 : 0;
}
