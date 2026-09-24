// 直接链接生产 roster_prepare.cpp 的隔离回归，不以同名 mock 替换补足服务。
// 所有地址、角色表、技能表和函数替身都属于本测试进程；不读取游戏文件/进程/存档。
// 原生入口的审核前导字节保持完整，紧接着对称恢复栈并跳到 C++ 替身，因而生产的
// MatchSignatures、精确ID搜索、预检顺序与前后数据核对均真实执行。
#include <Windows.h>
#include "roster_prepare.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sky2party;
namespace sky2party { void Log(const char*) noexcept {} }
namespace {
int checks = 0, failures = 0;
void Check(bool value, const char* description) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAILED: %s\n", description); }
}
template<class T> T Read(uintptr_t address) {
    T value{}; std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value)); return value;
}
template<class T> void Write(uintptr_t address, T value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}

struct Signature { uint32_t rva; std::array<uint8_t, 16> bytes; size_t length; };
// 固定的独立夹具值来自已审核入口，而非读取生产内部 signatures 数组。
// 如果生产更换了调用接口，本测试必须显式重新审核这些字节和下面的栈恢复尾部。
constexpr std::array<Signature, 9> kSignatures{{
    {0x3460C0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x41},16},
    {0x3463D0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57},16},
    {0xFA220,{0x40,0x55,0x56,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x48,0x48},16},
    {0xF9EE0,{0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57},16},
    {0xFC330,{0x48,0x89,0x5C,0x24,0x20,0x56,0x48,0x83,0xEC,0x30,0x48,0x8B,0x05,0x37,0x14,0xB6},16},
    {0xFB210,{0x44,0x88,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4C,0x24,0x08,0x53},16},
    {0x34613B,{0x80,0x7C,0x24,0x60,0x00,0x48,0x8B,0x35,0x09,0xAD,0x91,0x00,0x75,0x49},14},
    {0x3461A7,{0x80,0x7C,0x24,0x60,0x00,0x75,0x18},7},
    {0x3461EA,{0x80,0x7C,0x24,0x60,0x00,0x75,0x14},7}
}};

struct Fixture;
Fixture* active = nullptr;
bool MockCanEquip(uintptr_t ignored, uint32_t id, uint32_t slot, uint32_t weapon);
bool MockEquip(uintptr_t ignored, uint32_t id, uint32_t slot, uint32_t weapon, bool templateMode);
bool MockLearn(uintptr_t record, uintptr_t report);
bool MockAddOne(uintptr_t record, uint32_t skill, uintptr_t output, bool logMissing,
                uintptr_t unused, bool keepUpgradeChain);
void MockSelectS(uintptr_t record, uint32_t skill);

struct Fixture {
    uint8_t* image = nullptr;
    uintptr_t base = 0, manager = 0, record = 0, descriptor = 0;
    uint32_t id = 100;
    // 容量只服务于本测试的固定100槽结构，所有无关字节填充哨兵用于检测越界或覆盖。
    std::vector<uint8_t> records = std::vector<uint8_t>(0x126000);
    std::array<uint8_t, 0x100> tables{}, wrapper{}, table{};
    std::array<uint8_t, 80 * 64 + 80> descriptors{};
    std::array<uint8_t, 0x1000> skillData{};
    std::string calls;
    bool canEquip = true, equipReturn = true, equipWrites = true, learnReturn = true, learnWrites = true;
    bool canEquipRaises = false, learnRaises = false, equipRaises = false;
    bool addReturn = true, addWrites = true, selectWrites = true, addRaises = false;
    bool fillAllCraftSlots = false, dropLearnedOnAdd = false;
    bool mutateDuringPreflight = false, invalidateAfterLearn = false, invalidateAfterEquip = false;
    size_t learnCorruptOffset = 0, equipCorruptOffset = 0;
    uint16_t learnedS = 3105, learnedSkill = 3105, extraLearnedSkill = 0;

    Fixture() {
        image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0xC61000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!image) return;
        base = reinterpret_cast<uintptr_t>(image);
        manager = reinterpret_cast<uintptr_t>(records.data());
        active = this;
        // 首次配置必须拒绝无签名影像，不能凭非零基址接受。
        Check(!ConfigureRosterPreparation(base), "零填充假宿主不能通过准备签名审核");
        for (const auto& signature : kSignatures)
            std::memcpy(image + signature.rva, signature.bytes.data(), signature.length);

        // Equip 的第16字节是 REX 前缀41，补成 PUSH R12 后立即 POP R12。
        // 前面3条仅保存非易失寄存器到调用者预留影子空间，未改变寄存器或参数。
        Tail(0x3460C0, {0x54,0x41,0x5C}, reinterpret_cast<uintptr_t>(&MockEquip));
        // CanEquip 前导完整执行 PUSH RDI；恢复它后 RCX/RDX/R8/R9 仍是原参数。
        Tail(0x3463D0, {0x5F}, reinterpret_cast<uintptr_t>(&MockCanEquip));
        // Learn 前导 PUSH RBP/RSI/R12/R13/R14/R15，SUB RSP,48，最后是REX 48。
        // 补成 ADD RSP,48 并按相反顺序恢复，尾跳转时与原调用前的栈完全一致。
        Tail(0xFA220, {0x83,0xC4,0x48,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5E,0x5D},
            reinterpret_cast<uintptr_t>(&MockLearn));
        // AddOne 前导保存RBX到影子空间并依次PUSH七个非易失寄存器；未执行后续SUB。
        // 对称POP后，第5/6实参仍分别位于函数入口RSP+28/+30，必须全部保留。
        Tail(0xF9EE0, {0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F,0x5E,0x5D},
            reinterpret_cast<uintptr_t>(&MockAddOne));
        // SetS 的末尾是RIP相对读取的第3字节位移；补00后只读本测试的tables全局。
        Tail(0xFC330, {0x00,0x48,0x83,0xC4,0x30,0x5E}, reinterpret_cast<uintptr_t>(&MockSelectS));
        Check(ConfigureRosterPreparation(base), "完整签名的隔离影像能配置生产补足服务");
        Reset();
    }
    ~Fixture() { if (image) VirtualFree(image, 0, MEM_RELEASE); active = nullptr; }
    void Tail(uint32_t rva, std::initializer_list<uint8_t> undo, uintptr_t target) {
        // 多个替身可能共享页；每次先将本测试拥有的页临时设为可写，写完恢复RX。
        DWORD old = 0;
        const auto page = reinterpret_cast<void*>((base + rva) & ~uintptr_t(4095));
        Check(VirtualProtect(page, 4096, PAGE_READWRITE, &old) != FALSE, "隔离函数页准备写入");
        auto* output = image + rva + 16;
        for (const auto byte : undo) *output++ = byte;
        *output++ = 0x48; *output++ = 0xB8; // MOV RAX,目标地址；RAX为调用者易失寄存器。
        std::memcpy(output, &target, sizeof(target)); output += sizeof(target);
        *output++ = 0xFF; *output = 0xE0; // JMP RAX，不新增栈帧或改变第5参数位置。
        Check(VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old) != FALSE, "隔离函数页改为只读可执行");
        Check(FlushInstructionCache(GetCurrentProcess(), page, 4096) != FALSE, "隔离函数页刷新指令缓存");
    }
    void Reset(uint32_t role = 100, bool missingWeapon = true, bool missingCraft = true) {
        id = role; calls.clear();
        canEquip = equipReturn = equipWrites = learnReturn = learnWrites = true;
        addReturn = addWrites = selectWrites = true; addRaises = false;
        fillAllCraftSlots = dropLearnedOnAdd = false;
        canEquipRaises = learnRaises = equipRaises = mutateDuringPreflight = false;
        invalidateAfterLearn = invalidateAfterEquip = false;
        learnCorruptOffset = equipCorruptOffset = 0; learnedS = learnedSkill = 3105; extraLearnedSkill = 0;
        std::fill(records.begin(), records.end(), uint8_t(0x55));
        for (size_t slot = 0; slot < 100; ++slot) Write<uint32_t>(manager + 0x1142F8 + slot * 0x2A0, 0xFFFF);
        record = manager + 0x1142F8 + 5 * 0x2A0;
        std::memset(reinterpret_cast<void*>(record), 0, 0x2A0);
        Write<uint32_t>(record, role); Write<uint32_t>(record + 4, 50); Write<uint32_t>(record + 8, 123456);
        Write<uint32_t>(record + 0x10, 4500); Write<uint32_t>(record + 0x18, 300); Write<uint32_t>(record + 0x20, 200);
        for (size_t offset = 0x22C; offset < 0x264; ++offset) Write<uint8_t>(record + offset, static_cast<uint8_t>(offset));
        for (size_t offset = 0x268; offset < 0x28C; ++offset) Write<uint8_t>(record + offset, static_cast<uint8_t>(offset + 7));
        if (!missingWeapon) Write<uint32_t>(record + 0x264, DefaultRosterWeapon(id) + 1);
        if (!missingCraft) {
            Write<uint16_t>(record + 0x16C, 1200); Write<uint16_t>(record + 0x16E, 3105);
            Write<uint16_t>(record + 0x290, 3105);
        }
        tables.fill(0); wrapper.fill(0); table.fill(0); descriptors.fill(0); skillData.fill(0);
        Write(base + 0xC60E50, manager); Write(base + 0xC5D778, reinterpret_cast<uintptr_t>(tables.data()));
        Write(reinterpret_cast<uintptr_t>(tables.data()) + 0x30, reinterpret_cast<uintptr_t>(wrapper.data()));
        Write(reinterpret_cast<uintptr_t>(wrapper.data()) + 8, reinterpret_cast<uintptr_t>(table.data()));
        Write(reinterpret_cast<uintptr_t>(table.data()) + 0x10, reinterpret_cast<uintptr_t>(skillData.data()));
        Write(reinterpret_cast<uintptr_t>(table.data()) + 0x20, reinterpret_cast<uintptr_t>(descriptors.data()));
        Write<uint32_t>(reinterpret_cast<uintptr_t>(table.data()) + 0x28, 3);
        descriptor = reinterpret_cast<uintptr_t>(descriptors.data()) + 3 * 80;
        Write<uint32_t>(descriptor + 0x44, 0x40); Write<uint32_t>(descriptor + 0x48, 0x90);
        Write<uint32_t>(descriptor + 0x4C, 3);
        // 表包装及80字节描述符布局来自原生0FA261..0FA2C1，非直接喂布尔预检结果。
        Skill(0, 1100, static_cast<uint16_t>(id + 1), 1, 1);
        Skill(1, 1200, static_cast<uint16_t>(id), 1, 1);
        Skill(2, 3105, static_cast<uint16_t>(id), 3, 5);
    }
    uintptr_t Row(size_t index) { return reinterpret_cast<uintptr_t>(skillData.data()) + 0x40 + index * 0x90; }
    void Skill(size_t index, uint16_t skill, uint16_t role, uint8_t kind, int16_t level) {
        const auto row = Row(index);
        Write(row, skill); Write(row + 2, role); Write(row + 0x10, kind); Write(row + 0x8A, level);
    }
    void InheritedS(int16_t learningLevel = 5, bool oldAlreadyLearned = false) {
        // 模拟原生学习时自动选中更高阶S；继承选择3105仍是玩家原来选定的技能。
        // 这里只给本测试生成技能行，生产预检仍必须从表中独立核验它们。
        Write<uint16_t>(record + 0x290, 3105);
        Skill(2, 3105, static_cast<uint16_t>(id), 3, learningLevel);
        Skill(3, 3205, static_cast<uint16_t>(id), 3, 20);
        Write<uint32_t>(descriptor + 0x4C, 4);
        learnedS = 3205;
        learnedSkill = oldAlreadyLearned ? 3105 : 3205;
        extraLearnedSkill = oldAlreadyLearned ? 3205 : 0;
    }
    RosterPreparationResult Run() { return PrepareRosterRecordOnGameThread(id, record); }
    void RejectWithoutWrites(RosterPreparationResult expected, const char* message) {
        const auto before = records;
        Check(Run() == expected, message);
        Check(records == before && calls.find('L') == std::string::npos && calls.find('E') == std::string::npos,
            "预检拒绝不学习战技、不生成装备且不修改角色表");
    }
    void SignatureByte(const Signature& signature, uint8_t value) {
        DWORD old = 0;
        auto* address = image + signature.rva;
        Check(VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &old) != FALSE, "测试改签名页保护");
        *address = value;
        DWORD ignored = 0;
        VirtualProtect(address, 1, old, &ignored); FlushInstructionCache(GetCurrentProcess(), address, 1);
    }
};

bool MockCanEquip(uintptr_t ignored, uint32_t id, uint32_t slot, uint32_t weapon) {
    auto& f = *active; f.calls += 'C';
    Check(ignored == 0 && id == f.id && slot == 0 && weapon == DefaultRosterWeapon(id), "CanEquip真实x64参数正确");
    if (f.canEquipRaises) RaiseException(0xE1234567, 0, 0, nullptr);
    if (f.mutateDuringPreflight) Write<uint32_t>(f.record + 8, 999);
    return f.canEquip;
}
bool MockLearn(uintptr_t record, uintptr_t report) {
    auto& f = *active; f.calls += 'L';
    Check(record == f.record && report == 0, "Learn传精确记录及空提示容器");
    if (f.learnRaises) RaiseException(0xE1234567, 0, 0, nullptr);
    if (f.learnWrites) {
        Write<uint16_t>(record + 0x16C, 1200); Write<uint16_t>(record + 0x16E, f.learnedSkill);
        if (f.extraLearnedSkill) Write<uint16_t>(record + 0x170, f.extraLearnedSkill);
        if (f.fillAllCraftSlots) {
            Write<uint16_t>(record + 0x16C, f.learnedSkill);
            for (size_t index = 1; index < 16; ++index)
                Write<uint16_t>(record + 0x16C + index * 2, static_cast<uint16_t>(4000 + index));
        }
        if (f.learnedS) Write<uint16_t>(record + 0x290, f.learnedS);
    }
    if (f.learnCorruptOffset) Write<uint8_t>(record + f.learnCorruptOffset, 0xF3);
    if (f.invalidateAfterLearn) Write<uint32_t>(record, 0xFFFF);
    return f.learnReturn;
}
bool MockEquip(uintptr_t ignored, uint32_t id, uint32_t slot, uint32_t weapon, bool templateMode) {
    auto& f = *active; f.calls += 'E';
    Check(ignored == 0 && id == f.id && slot == 0 && weapon == DefaultRosterWeapon(id) && templateMode,
        "Equip真实x64第五参数为template=true且只装备目标角色武器槽");
    if (f.equipRaises) RaiseException(0xE1234567, 0, 0, nullptr);
    if (f.equipWrites) { Write<uint32_t>(f.record + 0x264, weapon); Write<uint32_t>(f.record + 0x24, 321); }
    if (f.equipCorruptOffset) Write<uint8_t>(f.record + f.equipCorruptOffset, 0xF3);
    if (f.invalidateAfterEquip) Write<uint32_t>(f.record, 0xFFFF);
    return f.equipReturn;
}
bool MockAddOne(uintptr_t record, uint32_t skill, uintptr_t output, bool logMissing,
                uintptr_t unused, bool keepUpgradeChain) {
    auto& f = *active; f.calls += 'A';
    Check(record == f.record && output == 0 && !logMissing && unused == 0 && keepUpgradeChain,
        "AddOne六参ABI完整：精确记录、空输出、不记录缺失、第5占位0、第6保留升级链true");
    Check(skill == 3105, "只补回本次已核验继承S，不能任意补其他技能");
    if (f.addRaises) RaiseException(0xE1234567, 0, 0, nullptr);
    if (f.addWrites) {
        for (size_t index = 0; index < 16; ++index) {
            const auto address = record + 0x16C + index * 2;
            if (Read<uint16_t>(address) == skill) break;
            if (Read<uint16_t>(address) == 0) { Write<uint16_t>(address, static_cast<uint16_t>(skill)); break; }
        }
    }
    // 模拟错误地走升级链替换：虽然旧S回来了，新学的高级S却被移除。
    // 生产后置条件应拒绝这种“看似加入成功但损失技能”的情况。
    if (f.dropLearnedOnAdd) Write<uint16_t>(record + 0x16E, 0);
    return f.addReturn;
}
void MockSelectS(uintptr_t record, uint32_t skill) {
    auto& f = *active; f.calls += 'S';
    Check(record == f.record && skill <= 0xFFFF, "SetS传精确记录和WORD范围技能ID");
    if (f.selectWrites) Write<uint16_t>(record + 0x290, static_cast<uint16_t>(skill));
}

void CheckPreservedExcept(const std::vector<uint8_t>& before, const Fixture& f,
                         bool weapon, bool crafts, bool sChanged = false) {
    auto expected = before;
    const size_t start = static_cast<size_t>(f.record - f.manager);
    if (weapon) {
        std::memcpy(expected.data() + start + 0x264, f.records.data() + start + 0x264, 4);
        std::memcpy(expected.data() + start + 0x24, f.records.data() + start + 0x24, 4);
    }
    if (crafts) std::memcpy(expected.data() + start + 0x16C, f.records.data() + start + 0x16C, 32);
    if (sChanged) std::memcpy(expected.data() + start + 0x290, f.records.data() + start + 0x290, 2);
    Check(expected == f.records, "整张培养表除明确允许的补缺字段外保持逐字节一致");
}
}

int main() {
    Fixture f;
    if (!f.image) return 2;
    // 先覆盖完整角色和三种缺项；重复调用必须返回NotNeeded，而非反复送武器/重学战技。
    for (const auto& role : kRosterDefinitions) {
        for (unsigned missing = 0; missing < 4; ++missing) {
            const bool weapon = (missing & 1) != 0, craft = (missing & 2) != 0;
            f.Reset(role.id, weapon, craft); const auto before = f.records;
            Check(f.Run() == (missing ? RosterPreparationResult::Prepared : RosterPreparationResult::NotNeeded),
                "十四角色完整/缺武器/缺战技/两者均缺由生产服务正确处理");
            Check(f.calls == (weapon ? (craft ? "CLE" : "CE") : (craft ? "L" : "")),
                "先完成预检，再学习、再装备；已有项不调用原生替身");
            CheckPreservedExcept(before, f, weapon, craft, craft);
            const auto complete = f.records; const auto calls = f.calls;
            Check(f.Run() == RosterPreparationResult::NotNeeded && f.records == complete && f.calls == calls,
                "完整记录再次调用保持幂等且不生成物品");
        }
    }
    for (const auto& signature : kSignatures) {
        f.Reset(); f.SignatureByte(signature, 0xCC);
        f.RejectWithoutWrites(RosterPreparationResult::Unsupported, "任一审核签名变化都在调用前拒绝");
        f.SignatureByte(signature, signature.bytes[0]);
    }
    f.Reset(102); f.RejectWithoutWrites(RosterPreparationResult::Unsupported, "未批准NPC ID不进入补足");
    for (const uintptr_t addressKind : {uintptr_t(0),uintptr_t(1),uintptr_t(2),uintptr_t(3)}) {
        f.Reset();
        if (addressKind == 0) f.record = f.manager;
        if (addressKind == 1) ++f.record;
        if (addressKind == 2) f.record = f.manager + 0x1142F8 + 100 * 0x2A0;
        if (addressKind == 3) f.record += 0x2A0;
        f.RejectWithoutWrites(RosterPreparationResult::InvalidData, "记录地址必须对齐且精确匹配100槽内ID");
    }
    f.Reset(); Write<uint32_t>(f.manager + 0x1142F8 + 6 * 0x2A0, f.id);
    f.RejectWithoutWrites(RosterPreparationResult::InvalidData, "重复精确ID不能擅选其一");
    f.Reset(); Write<uint32_t>(f.record, 0xFFFF);
    f.RejectWithoutWrites(RosterPreparationResult::InvalidData, "FFFF空白槽不伪装为已培养记录");
    for (const auto offset : {4u,0x10u,0x18u,0x20u}) {
        f.Reset(); Write<uint32_t>(f.record + offset, 0);
        f.RejectWithoutWrites(RosterPreparationResult::InvalidData, "核心等级/HP/EP/CP未建立时拒绝");
    }
    for (const auto weapon : {0xFFFFu,0xFFFFFFFFu}) {
        f.Reset(); Write<uint32_t>(f.record + 0x264, weapon);
        f.RejectWithoutWrites(RosterPreparationResult::InvalidData, "未知武器哨兵不能按空槽覆盖");
    }
    f.Reset(); f.canEquip = false;
    f.RejectWithoutWrites(RosterPreparationResult::Unsupported, "原生装备资格拒绝必须先于任何学习");
    f.Reset(); f.mutateDuringPreflight = true;
    Check(f.Run() == RosterPreparationResult::InvalidData && f.calls == "C", "预检期间培养改变时停止，不能继续写旧快照");

    // 用真正多级表指针和描述符触发每项预检；任何失效均不能先生成武器。
    for (unsigned invalid = 0; invalid < 13; ++invalid) {
        f.Reset();
        const auto table = reinterpret_cast<uintptr_t>(f.table.data());
        if (invalid == 0) Write<uintptr_t>(f.base + 0xC5D778, 0);
        if (invalid == 1) Write<uintptr_t>(reinterpret_cast<uintptr_t>(f.tables.data()) + 0x30, 0);
        if (invalid == 2) Write<uintptr_t>(reinterpret_cast<uintptr_t>(f.wrapper.data()) + 8, 0);
        if (invalid == 3) Write<uint32_t>(table + 0x28, 64);
        if (invalid == 4) Write<uintptr_t>(table + 0x20, 0);
        if (invalid == 5) Write<uintptr_t>(table + 0x10, 0);
        if (invalid == 6) Write<uint32_t>(f.descriptor + 0x48, 0x8B);
        if (invalid == 7) Write<uint32_t>(f.descriptor + 0x48, 4097);
        if (invalid == 8) Write<uint32_t>(f.descriptor + 0x4C, 0);
        if (invalid == 9) Write<uint32_t>(f.descriptor + 0x4C, 65537);
        if (invalid == 10) Write<uint32_t>(f.descriptor + 0x44, 128 * 1024 * 1024 + 1);
        if (invalid == 11) { f.Skill(1, 1200, 42, 1, 1); f.Skill(2, 3105, 42, 3, 5); }
        if (invalid == 12) { f.Skill(1, 1200, static_cast<uint16_t>(f.id), 1, 51); f.Skill(2, 3105, static_cast<uint16_t>(f.id), 3, 51); }
        f.RejectWithoutWrites(RosterPreparationResult::Unsupported, "技能表/数量/跨度/角色/学习等级预检拒绝");
    }
    for (const auto level : {int16_t(0),int16_t(-1)}) {
        f.Reset(); f.Skill(1, 1200, static_cast<uint16_t>(f.id), 1, level); f.Skill(2, 3105, static_cast<uint16_t>(f.id), 3, level);
        f.RejectWithoutWrites(RosterPreparationResult::Unsupported, "零或负学习等级不能补入");
    }
    for (const auto kind : {uint8_t(0),uint8_t(4)}) {
        f.Reset(); f.Skill(1, 1200, static_cast<uint16_t>(f.id), kind, 1); f.Skill(2, 3105, static_cast<uint16_t>(f.id), kind, 1);
        f.RejectWithoutWrites(RosterPreparationResult::Unsupported, "非战技类型不可作为可学习证据");
    }
    for (const auto skill : {uint16_t(0),uint16_t(0xFFFF)}) {
        f.Reset(); f.Skill(1, skill, static_cast<uint16_t>(f.id), 1, 1); f.Skill(2, skill, static_cast<uint16_t>(f.id), 3, 1);
        f.RejectWithoutWrites(RosterPreparationResult::Unsupported, "空或FFFF技能ID不可作为可学习证据");
    }

    // 回归实机暴露的空战技+非零继承S选择。不得把合法继承选择直接视为未初始化，
    // 也不能通过清零选择、重放角色模板来规避；当前角色/类型/等级仍须可验证。
    f.Reset(); Write<uint16_t>(f.record + 0x290, 3105); const auto inherited = f.records;
    Check(f.Run() == RosterPreparationResult::Prepared, "空战技但合法本角色继承S选择仍可补足");
    Check(Read<uint16_t>(f.record + 0x290) == 3105, "补足后保留合法继承S选择");
    CheckPreservedExcept(inherited, f, true, true);
    for (const int16_t learningLevel : {int16_t(-1),int16_t(5),int16_t(50)}) {
        f.Reset(106); f.InheritedS(learningLevel); const auto before = f.records;
        Check(f.Run() == RosterPreparationResult::Prepared && f.calls == "CLASE",
            "继承S在当前可学或等级-1剧情专属时：先学习、只补旧S、恢复原选择、最后配装");
        Check(Read<uint16_t>(f.record + 0x290) == 3105 && Read<uint16_t>(f.record + 0x16E) == 3205 &&
            Read<uint16_t>(f.record + 0x170) == 3105,
            "旧选择与新学高级S同时保留，不使用默认升级替换来丢失任一技能");
        CheckPreservedExcept(before, f, true, true);
    }
    f.Reset(); f.InheritedS(5, true);
    Check(f.Run() == RosterPreparationResult::Prepared && f.calls == "CLSE",
        "Learn已含旧S时不重复AddOne，只恢复原选择");
    for (unsigned invalid = 0; invalid < 8; ++invalid) {
        f.Reset(); Write<uint16_t>(f.record + 0x290, 3105);
        if (invalid == 0) Write<uint16_t>(f.record + 0x290, 9999);
        if (invalid == 1) f.Skill(2, 3105, 42, 3, 5);
        if (invalid == 2) f.Skill(2, 3105, static_cast<uint16_t>(f.id), 1, 5);
        if (invalid == 3) f.Skill(2, 3105, static_cast<uint16_t>(f.id), 3, 51);
        if (invalid == 4) f.Skill(2, 3105, static_cast<uint16_t>(f.id), 3, 0);
        if (invalid == 5) f.Skill(2, 3105, static_cast<uint16_t>(f.id), 3, -2);
        if (invalid == 6) { f.Skill(0, 3105, static_cast<uint16_t>(f.id), 3, 5); }
        if (invalid == 7) { f.Skill(0, 3105, 42, 3, 5); }
        f.RejectWithoutWrites(RosterPreparationResult::Unsupported, "未知/外角色/非S/当前不可学的继承选择不能盲目保留");
    }

    f.Reset(); f.InheritedS(-1); f.addReturn = false; f.addWrites = false;
    Check(f.Run() == RosterPreparationResult::PartialPrepared && f.calls == "CLA",
        "已学习后补回继承S失败为PartialPrepared，不选择不存在S也不配装");
    f.Reset(); f.InheritedS(-1); f.addWrites = false;
    Check(f.Run() == RosterPreparationResult::PartialPrepared && f.calls == "CLA",
        "AddOne返回true但未产生继承S，仍拒绝继续");
    f.Reset(); f.InheritedS(-1); f.addReturn = false;
    Check(f.Run() == RosterPreparationResult::PartialPrepared && f.calls == "CLA",
        "AddOne写入后返回false如实反馈部分完成，不假装回滚");
    f.Reset(); f.InheritedS(-1); f.selectWrites = false;
    Check(f.Run() == RosterPreparationResult::PartialPrepared && f.calls == "CLAS",
        "void SetS调用后必须复读选择，未恢复时拒绝配装与成功报告");
    f.Reset(); f.InheritedS(-1); f.dropLearnedOnAdd = true;
    Check(f.Run() == RosterPreparationResult::PartialPrepared && f.calls.find('E') == std::string::npos,
        "补回旧S不能移除原生刚学到的高级技能，发现损失立即停止");
    f.Reset(); f.InheritedS(-1); f.fillAllCraftSlots = true;
    for (size_t index = 1; index < 16; ++index)
        f.Skill(index + 3, static_cast<uint16_t>(4000 + index), static_cast<uint16_t>(f.id), 1, 1);
    Write<uint32_t>(f.descriptor + 0x4C, 19);
    Check(f.Run() == RosterPreparationResult::PartialPrepared && f.calls.find('E') == std::string::npos,
        "学习后16槽已满无法补回继承S，不能挤掉高级技能或继续配装");
    f.Reset(); f.InheritedS(-1); f.addRaises = true;
    Check(f.Run() == RosterPreparationResult::PartialPrepared, "补回继承S接口异常保守报告部分完成");

    // 原生替身返回失败但已经动过数据时，生产必须如实报告PartialPrepared，不强写回滚。
    f.Reset(); f.learnReturn = false; f.learnWrites = false;
    const auto failedLearnBefore = f.records;
    Check(f.Run() == RosterPreparationResult::Failed && f.calls == "CL" && f.records == failedLearnBefore,
        "学习接口已调用但无变更失败，整张角色表保持原样且不继续配装");
    f.Reset(); f.learnReturn = false;
    Check(f.Run() == RosterPreparationResult::PartialPrepared && f.calls == "CL", "学习已部分写入后失败不得配装或伪称回滚");
    f.Reset(); f.learnWrites = false;
    Check(f.Run() == RosterPreparationResult::Failed && f.calls == "CL", "学习返回true但未产生战技也不能继续配装");
    f.Reset(100, true, false); f.equipReturn = false; f.equipWrites = false;
    Check(f.Run() == RosterPreparationResult::Failed && f.calls == "CE", "只有配装且无变更失败为Failed");
    f.Reset(); f.equipReturn = false; f.equipWrites = false;
    Check(f.Run() == RosterPreparationResult::PartialPrepared && f.calls == "CLE", "已经学到战技后配装失败属于部分完成");
    f.Reset(100, true, false); f.equipReturn = false;
    Check(f.Run() == RosterPreparationResult::PartialPrepared, "配装写入后返回false不能伪称未变更");
    f.Reset(); f.invalidateAfterLearn = true;
    Check(f.Run() == RosterPreparationResult::PartialPrepared && f.calls == "CL", "学习后精确记录失效不能继续配装");
    f.Reset(100, true, false); f.invalidateAfterEquip = true;
    Check(f.Run() == RosterPreparationResult::PartialPrepared, "配装后精确记录失效必须报告部分完成");
    for (const size_t offset : {size_t(4),size_t(8),size_t(0x22C),size_t(0x268)}) {
        f.Reset(); f.learnCorruptOffset = offset;
        Check(f.Run() == RosterPreparationResult::PartialPrepared && f.calls == "CL", "学习改变已有等级/经验/回路/装备时停止");
        f.Reset(100, true, false); f.equipCorruptOffset = offset;
        Check(f.Run() == RosterPreparationResult::PartialPrepared, "配装改变已有培养时不能报告成功");
    }
    for (const size_t offset : {size_t(0x16C),size_t(0x290)}) {
        f.Reset(100, true, false); f.equipCorruptOffset = offset;
        Check(f.Run() == RosterPreparationResult::PartialPrepared, "补武器不得改变已有战技或S选择");
        f.Reset(); f.equipCorruptOffset = offset;
        Check(f.Run() == RosterPreparationResult::PartialPrepared, "补武器也不得改变本次已经补好的战技或S选择");
    }
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        f.Reset(); f.learnedS = 3205; f.learnedSkill = 3205;
        if (invalid != 0) {
            f.Skill(3, 3205, static_cast<uint16_t>(invalid == 1 ? 42 : f.id),
                    static_cast<uint8_t>(invalid == 2 ? 1 : 3), static_cast<int16_t>(invalid == 3 ? 51 : 5));
            Write<uint32_t>(f.descriptor + 0x4C, 4);
        }
        Check(f.Run() == RosterPreparationResult::PartialPrepared && f.calls == "CL",
            "原生学习后新选择S仍需核验存在、所属角色、类型与学习等级");
    }
    f.Reset(); f.canEquipRaises = true;
    Check(f.Run() == RosterPreparationResult::InvalidData, "预检异常属于未开始写入的InvalidData");
    f.Reset(); f.learnRaises = true;
    Check(f.Run() == RosterPreparationResult::PartialPrepared, "学习调用开始后异常保守报告部分完成");
    f.Reset(100, true, false); f.equipRaises = true;
    Check(f.Run() == RosterPreparationResult::PartialPrepared, "配装调用开始后异常保守报告部分完成");
    std::printf("%d production roster preparation checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
