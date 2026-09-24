// 使用已审计原生 Equip 与 LearnByLevel 接口补缺项；不重放 Sora_SetStatus 剧情脚本。
// 不升降等级、不清已有战技、不替换已有装备/回路/孔，也不修改任务旗标或背包数量。
#include "roster_prepare.h"
#include "runtime.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>

namespace sky2party {
namespace {
uintptr_t gameBase = 0;
using Equip = bool(*)(uintptr_t, uint32_t, uint32_t, uint32_t, bool);
using CanEquip = bool(*)(uintptr_t, uint32_t, uint32_t, uint32_t);
using LearnByLevel = bool(*)(uintptr_t, uintptr_t);
// F9EE0 的第六参数位于调用方栈 +0x28；第五参数必须占位，不能误声明成五参。
// keepSeparate=true 只跳过升级链替换，仍检查重复与16槽容量。本插件仅以此补回
// 存档原先选定的那一个 S 战技，避免为了保留旧选择而覆盖新学的高级战技。
using AddOneCraft = bool(*)(uintptr_t, uint32_t, uintptr_t, bool, uintptr_t, bool);
using SelectSCraft = void(*)(uintptr_t, uint32_t);
using RecordBytes = std::array<uint8_t, 0x2A0>;

template<class T> T At(uintptr_t address) noexcept {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}
template<class T> T Field(const RecordBytes& record, size_t offset) noexcept {
    T value{};
    std::memcpy(&value, record.data() + offset, sizeof(value));
    return value;
}
bool HasCraft(const RecordBytes& record) noexcept {
    // 这里实际是16个WORD战技ID，而非位图；只判断有无内容，不篡改排序或升级链。
    for (size_t at = 0x16C; at < 0x18C; at += 2)
        if (Field<uint16_t>(record, at)) return true;
    return false;
}
bool ContainsCraft(const RecordBytes& record, uint16_t id) noexcept {
    if (!id || id == 0xFFFF) return false;
    for (size_t at = 0x16C; at < 0x18C; at += 2)
        if (Field<uint16_t>(record, at) == id) return true;
    return false;
}
bool KeepsLearnedCrafts(const RecordBytes& learned, const RecordBytes& current) noexcept {
    // 原生添加会排序列表，所以按技能 ID 比较成员，不把槽位顺序变化误报成丢失。
    for (size_t at = 0x16C; at < 0x18C; at += 2) {
        const auto id = Field<uint16_t>(learned, at);
        if (id && !ContainsCraft(current, id)) return false;
    }
    return true;
}
bool CaptureExact(uint32_t id, uintptr_t exactRecord, RecordBytes& bytes, RosterRecordFacts& facts) noexcept {
    const auto manager = At<uintptr_t>(gameBase + 0xC60E50);
    if (manager < 0x10000) return false;
    const auto first = manager + 0x1142F8;
    if (exactRecord < first || exactRecord >= first + 100 * 0x2A0 || (exactRecord - first) % 0x2A0) return false;
    uint32_t matches = 0;
    for (size_t index = 0; index < 100; ++index)
        matches += At<uint32_t>(first + index * 0x2A0) == id;
    if (matches != 1 || At<uint32_t>(exactRecord) != id) return false;
    std::memcpy(bytes.data(), reinterpret_cast<const void*>(exactRecord), bytes.size());
    facts.requestedId = id;
    facts.storedId = Field<uint32_t>(bytes, 0);
    facts.level = Field<uint32_t>(bytes, 4);
    facts.maxHp = Field<uint32_t>(bytes, 0x10);
    facts.maxEp = Field<uint32_t>(bytes, 0x18);
    facts.maxCp = Field<uint32_t>(bytes, 0x20);
    facts.weapon = Field<uint32_t>(bytes, 0x264);
    facts.hasCraft = HasCraft(bytes);
    facts.duplicate = false;
    return true;
}

struct SkillFacts {
    uint16_t owner = 0;
    int16_t requiredLevel = 0;
    uint8_t kind = 0;
};
struct SkillCatalog {
    uintptr_t first = 0;
    uint32_t stride = 0, count = 0;
};
bool CaptureSkillCatalog(SkillCatalog& catalog) noexcept {
    // 复现0FA220的只读规则：t_skill的SkillParam、角色ID、正学习等级、不高于当前
    // 等级、类型1..3。预检先于任何配装，避免没有可学战技却先产生一件武器。
    const auto tables = At<uintptr_t>(gameBase + 0xC5D778);
    if (tables < 0x10000) return false;
    const auto wrapper = At<uintptr_t>(tables + 0x30);
    if (wrapper < 0x10000) return false;
    const auto table = At<uintptr_t>(wrapper + 8);
    if (table < 0x10000) return false;
    const auto sectionIndex = At<uint32_t>(table + 0x28);
    const auto descriptors = At<uintptr_t>(table + 0x20);
    const auto data = At<uintptr_t>(table + 0x10);
    if (sectionIndex >= 64 || descriptors < 0x10000 || data < 0x10000) return false;
    const auto descriptor = descriptors + sectionIndex * 80;
    const auto offset = At<uint32_t>(descriptor + 0x44);
    const auto stride = At<uint32_t>(descriptor + 0x48);
    const auto count = At<uint32_t>(descriptor + 0x4C);
    if (stride < 0x8C || stride > 4096 || count == 0 || count > 65536 || offset > 128 * 1024 * 1024) return false;
    catalog = {data + offset, stride, count};
    return true;
}
bool HasLearnableCraft(const SkillCatalog& catalog, uint32_t id, uint32_t level) noexcept {
    for (size_t index = 0; index < catalog.count; ++index) {
        const auto row = catalog.first + index * catalog.stride;
        if (At<uint16_t>(row + 2) != id) continue;
        const auto required = At<int16_t>(row + 0x8A);
        const auto kind = At<uint8_t>(row + 0x10);
        if (required > 0 && static_cast<uint32_t>(required) <= level && kind >= 1 && kind <= 3 &&
            At<uint16_t>(row) != 0 && At<uint16_t>(row) != 0xFFFF) return true;
    }
    return false;
}
bool FindUniqueSkill(const SkillCatalog& catalog, uint16_t skill, SkillFacts& facts) noexcept {
    if (!skill || skill == 0xFFFF) return false;
    uint32_t matches = 0;
    for (size_t index = 0; index < catalog.count; ++index) {
        const auto row = catalog.first + index * catalog.stride;
        if (At<uint16_t>(row) != skill) continue;
        ++matches;
        facts = {At<uint16_t>(row + 2), At<int16_t>(row + 0x8A), At<uint8_t>(row + 0x10)};
    }
    // 原生 S 选择函数按技能 ID 找第一条；若存在重号，不能仅筛本人条目掩盖歧义。
    return matches == 1;
}
bool ValidSelectedSCraft(const SkillCatalog& catalog, uint32_t id, uint32_t level, uint16_t skill) noexcept {
    if (!skill) return true;
    SkillFacts facts{};
    if (!FindUniqueSkill(catalog, skill, facts) || facts.owner != id || facts.kind != 3) return false;
    // -1 是已核验的剧情手动习得类型，例如乔丝特的2904。这里只核对存档中已有
    // 选择或补足后的实际选择，绝不遍历并授予其他未记录的剧情 S 战技。
    return facts.requiredLevel == -1 ||
        (facts.requiredLevel > 0 && static_cast<uint32_t>(facts.requiredLevel) <= level);
}

bool PreservedCultivation(const RecordBytes& before, const RecordBytes& after, bool hadCraft) noexcept {
    // 原生装备允许能力重算；角色ID/等级/经验、非武器装备、回路和孔必须完整保留。
    // 已有战技不调用学习接口，因此连其排序与当前S战技选择也应保持原样。
    return std::memcmp(before.data(), after.data(), 12) == 0 &&
        std::memcmp(before.data() + 0x268, after.data() + 0x268, 9 * 4) == 0 &&
        std::memcmp(before.data() + 0x22C, after.data() + 0x22C, 0x38) == 0 &&
        (!hadCraft || (std::memcmp(before.data() + 0x16C, after.data() + 0x16C, 0x20) == 0 &&
            Field<uint16_t>(before, 0x290) == Field<uint16_t>(after, 0x290)));
}

RosterPreparationResult PrepareRaw(uint32_t id, uintptr_t exactRecord, bool& nativeStarted,
    const char*& stage) noexcept {
    if (!gameBase || RosterIndex(id) == kRosterDefinitions.size()) return RosterPreparationResult::Unsupported;
    RecordBytes before{}, after{}, completedCrafts{};
    RosterRecordFacts facts{}, result{};
    stage = "exact-record";
    if (!CaptureExact(id, exactRecord, before, facts) || !HasRosterCoreData(facts))
        return RosterPreparationResult::InvalidData;
    if (IsInitializedRosterRecord(facts)) return RosterPreparationResult::NotNeeded;
    if (!CanPrepareRosterRecord(facts)) return RosterPreparationResult::InvalidData;
    const bool needsWeapon = facts.weapon == 0;
    const bool needsCraft = !facts.hasCraft;
    const uint32_t weapon = DefaultRosterWeapon(id);
    const uint16_t previousSCraft = Field<uint16_t>(before, 0x290);
    SkillCatalog catalog{};
    stage = "weapon-eligibility";
    if (needsWeapon && !reinterpret_cast<CanEquip>(gameBase + 0x3463D0)(0, id, 0, weapon))
        return RosterPreparationResult::Unsupported;
    if (needsCraft) {
        stage = "learnable-craft-catalog";
        if (!CaptureSkillCatalog(catalog) || !HasLearnableCraft(catalog, id, facts.level))
            return RosterPreparationResult::Unsupported;
        // 继承档可清空可用列表而保留原 S 战技选择。非零不等于损坏，必须按本人
        // 技能表核实；身份不符、无效类型、重号或超等级仍在任何培养写入前拒绝。
        stage = "inherited-s-craft-validation";
        if (!ValidSelectedSCraft(catalog, id, facts.level, previousSCraft))
            return RosterPreparationResult::Unsupported;
    }
    stage = "record-changed-before-native";
    if (!CaptureExact(id, exactRecord, after, result) || before != after) return RosterPreparationResult::InvalidData;
    if (needsCraft) {
        stage = "learn-by-level";
        nativeStarted = true;
        // 与chr_status_add_craft(id,0)相同的底层调用：按现有等级补原生可学战技，
        // nullptr不收集升级提示；S战技选择仍由原生0FC330维护，无事件脚本执行。
        const bool learned = reinterpret_cast<LearnByLevel>(gameBase + 0xFA220)(exactRecord, 0);
        if (!CaptureExact(id, exactRecord, after, result)) return RosterPreparationResult::PartialPrepared;
        if (!learned || !result.hasCraft || !PreservedCultivation(before, after, facts.hasCraft))
            return before == after ? RosterPreparationResult::Failed : RosterPreparationResult::PartialPrepared;
        const auto learnedCrafts = after;
        if (previousSCraft) {
            if (!ContainsCraft(after, previousSCraft)) {
                stage = "restore-inherited-s-craft";
                // LearnByLevel 不会学习等级为-1的剧情技；其升级链也可能将旧选择
                // 替换为高级版。只补回操作前已核验的单个 S，不清列表、不降级现有技。
                const bool restored = reinterpret_cast<AddOneCraft>(gameBase + 0xF9EE0)(
                    exactRecord, previousSCraft, 0, false, 0, true);
                if (!CaptureExact(id, exactRecord, after, result) || !restored ||
                    !ContainsCraft(after, previousSCraft)) return RosterPreparationResult::PartialPrepared;
            }
            if (Field<uint16_t>(after, 0x290) != previousSCraft) {
                stage = "select-inherited-s-craft";
                // 0FC330 没有可靠返回值；其原生流程确认技能已学后更新选择，结果靠复读。
                reinterpret_cast<SelectSCraft>(gameBase + 0xFC330)(exactRecord, previousSCraft);
                if (!CaptureExact(id, exactRecord, after, result) ||
                    Field<uint16_t>(after, 0x290) != previousSCraft)
                    return RosterPreparationResult::PartialPrepared;
            }
        }
        const auto selected = Field<uint16_t>(after, 0x290);
        stage = "craft-postcondition";
        if ((selected && !ContainsCraft(after, selected)) ||
            !ValidSelectedSCraft(catalog, id, facts.level, selected) ||
            !KeepsLearnedCrafts(learnedCrafts, after) ||
            !PreservedCultivation(before, after, facts.hasCraft))
            return RosterPreparationResult::PartialPrepared;
        completedCrafts = after;
    }
    if (needsWeapon) {
        stage = "equip-default-weapon";
        nativeStarted = true;
        // 第五参数true是原生模板配装：不检查/扣除背包，不卸下/返还旧武器。
        // 仅在原槽为0且已预检角色适配时调用；绝不覆盖任何现有武器。
        const bool equipped = reinterpret_cast<Equip>(gameBase + 0x3460C0)(0, id, 0, weapon, true);
        if (!CaptureExact(id, exactRecord, after, result)) return RosterPreparationResult::PartialPrepared;
        if (!equipped || result.weapon != weapon)
            return before == after ? RosterPreparationResult::Failed : RosterPreparationResult::PartialPrepared;
    }
    stage = "final-postcondition";
    if (!CaptureExact(id, exactRecord, after, result) || !IsInitializedRosterRecord(result) ||
        !PreservedCultivation(before, after, facts.hasCraft) || (!needsWeapon && result.weapon != facts.weapon) ||
        (needsCraft && (std::memcmp(completedCrafts.data() + 0x16C, after.data() + 0x16C, 0x20) != 0 ||
            Field<uint16_t>(completedCrafts, 0x290) != Field<uint16_t>(after, 0x290))) ||
        (needsCraft && previousSCraft && (Field<uint16_t>(after, 0x290) != previousSCraft ||
            !ContainsCraft(after, previousSCraft))))
        return RosterPreparationResult::PartialPrepared;
    return RosterPreparationResult::Prepared;
}

struct Signature { uint32_t rva; std::array<uint8_t, 16> bytes; size_t length; };
constexpr std::array<Signature, 9> signatures{{
    {0x3460C0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x41},16},
    {0x3463D0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57},16},
    {0xFA220,{0x40,0x55,0x56,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x48,0x48},16},
    {0xF9EE0,{0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57},16},
    {0xFC330,{0x48,0x89,0x5C,0x24,0x20,0x56,0x48,0x83,0xEC,0x30,0x48,0x8B,0x05,0x37,0x14,0xB6},16},
    {0xFB210,{0x44,0x88,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4C,0x24,0x08,0x53},16},
    // 三处templateMode分支分别跳过库存检查、旧装备卸下以及库存扣除。
    {0x34613B,{0x80,0x7C,0x24,0x60,0x00,0x48,0x8B,0x35,0x09,0xAD,0x91,0x00,0x75,0x49},14},
    {0x3461A7,{0x80,0x7C,0x24,0x60,0x00,0x75,0x18},7},
    {0x3461EA,{0x80,0x7C,0x24,0x60,0x00,0x75,0x14},7}
}};
bool MatchSignatures(uintptr_t base) noexcept {
    __try {
        for (const auto& entry : signatures)
            if (std::memcmp(reinterpret_cast<const void*>(base + entry.rva), entry.bytes.data(), entry.length)) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

bool ConfigureRosterPreparation(uintptr_t base) noexcept {
    if (!base || !MatchSignatures(base)) {
        Log("Roster missing-data preparation rejected mismatched native signatures.");
        return false;
    }
    gameBase = base;
    return true;
}
RosterPreparationResult PrepareRosterRecordOnGameThread(uint32_t id, uintptr_t exactRecord) noexcept {
    bool nativeStarted = false;
    const char* stage = "native-signatures";
    auto result = RosterPreparationResult::Unsupported;
    __try {
        if (gameBase && MatchSignatures(gameBase)) result = PrepareRaw(id, exactRecord, nativeStarted, stage);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // 原生调用开始后异常可能留下局部变化；不伪造原子成功或用整条结构盲目覆盖。
        result = nativeStarted ? RosterPreparationResult::PartialPrepared : RosterPreparationResult::InvalidData;
    }
    // 一次明确请求只记一条结果，保留具体失败阶段，避免只得到笼统“加入失败”。
    char message[192]{};
    std::snprintf(message, sizeof(message), "Roster preparation: id=%u stage=%s result=%u native-started=%u.",
        id, stage, static_cast<unsigned>(result), nativeStarted ? 1u : 0u);
    Log(message);
    return result;
}
}
