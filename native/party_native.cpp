// 逐组切换已审计的编成条件，只在控制层确认的安全游戏帧操作代码字节。
// 面板只提交请求；此层不读取控件、不改角色 flags/装备/等级，也不增减真实名单。
#include "runtime.h"
#include "feature_state.h"
#include "patch_toggle_policy.h"
#include "anywhere_menu.h"
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace sky2party {
namespace {
constexpr auto& kPlan = kUnlockedPartyEdits;
uintptr_t executableBase = 0;
size_t executableSize = 0;
PatchOwnership<kPlan.size()> ownership;
std::atomic<bool> initialized{false};
std::atomic<bool> stateKnown{false};
std::atomic<uint8_t> appliedMask{0};

uint8_t Encode(const NativeFeatureState& state) noexcept {
    return static_cast<uint8_t>((state.fixedMembers ? 1 : 0) |
        (state.anywhere ? 2 : 0) | (state.unlockUnavailable ? 4 : 0));
}
NativeFeatureState Decode(uint8_t mask) noexcept {
    return {(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0};
}

bool ReadMemory(uintptr_t address, void* output, size_t size) noexcept {
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct NativeMemory {
    uintptr_t base;
    bool Read(uint32_t rva, void* output, size_t size) noexcept { return ReadMemory(base + rva, output, size); }
    bool Exchange(uint32_t rva, uint8_t expected, uint8_t replacement) noexcept {
        // 比较交换只写单字节立即数；不替换跳转或指针，不接管未知原值。
        // 外部修改器可能在准备页保护后再次改页属性；异常也按写失败走回滚流程。
        __try {
            return static_cast<uint8_t>(_InterlockedCompareExchange8(
                reinterpret_cast<volatile char*>(base + rva), static_cast<char>(replacement),
                static_cast<char>(expected))) == expected;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool Flush() noexcept { return FlushInstructionCache(GetCurrentProcess(), nullptr, 0) != FALSE; }
};

struct Page { void* address = nullptr; DWORD protection = 0; };
template<size_t N>
bool RestorePages(const std::array<Page, N>& pages, size_t count, size_t pageSize) noexcept {
    bool ok = true;
    for (size_t i = count; i > 0; --i) {
        DWORD ignored = 0;
        ok &= VirtualProtect(pages[i - 1].address, pageSize, pages[i - 1].protection, &ignored) != FALSE;
    }
    return ok;
}

bool ImageSize(uintptr_t base, size_t& size) noexcept {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!base || !ReadMemory(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0 || dos.e_lfanew > 0x100000 ||
        !ReadMemory(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return false;
    size = nt.OptionalHeader.SizeOfImage;
    return size > 0x200000 && size <= 128 * 1024 * 1024;
}
}

bool InstallPartyHooks(uintptr_t base) noexcept {
    // 启动线程只建立可信原字节基线和常驻挂钩。所有用户开关初始均为未应用，
    // 默认开启项也必须等安全游戏帧提交，不能在这里直接写功能操作数。
    if (initialized.load(std::memory_order_acquire)) return false;
    size_t imageSize = 0, badIndex = 0;
    NativeMemory memory{base};
    if (!ImageSize(base, imageSize) || !ValidPlan(kPlan, imageSize) ||
        !MatchesPlan(memory, kPlan, badIndex)) {
        Log("Feature initialization rejected unknown code bytes; no feature operands changed.");
        return false;
    }
    if (!PrepareAnywhereMenu(base)) return false;
    if (!EnableAnywhereMenu()) { DiscardAnywhereMenu(); return false; }
    executableBase = base;
    executableSize = imageSize;
    ownership = {};
    appliedMask.store(0, std::memory_order_release);
    stateKnown.store(true, std::memory_order_release);
    initialized.store(true, std::memory_order_release);
    Log("Native feature switches prepared; awaiting a safe game-thread apply.");
    return true;
}

bool ApplyNativeFeaturesOnGameThread(const NativeFeatureState& requested) noexcept {
    // 此接口不猜测过场、战斗或 Camp 生命周期；调用方须在唯一游戏线程中，
    // 确认无不安全菜单/场景切换后串行调用。渲染线程或面板回调都不得直接调用。
    if (!initialized.load(std::memory_order_acquire)) return false;
    NativeMemory memory{executableBase};
    size_t badIndex = 0;
    if (!MatchesOwnedPlan(memory, kPlan, ownership, badIndex)) {
        stateKnown.store(false, std::memory_order_release);
        Log("Feature toggle rejected a foreign code change; last confirmed state is no longer authoritative.");
        return false;
    }
    const auto desired = NativeFeatureSites(requested);
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const size_t pageSize = system.dwPageSize;
    if (!pageSize || (pageSize & (pageSize - 1))) return false;
    std::array<Page, kPlan.size()> pages{};
    size_t pageCount = 0;
    for (size_t index = 0; index < kPlan.size(); ++index) {
        if (desired[index] == ownership.owned[index]) continue;
        const auto& edit = kPlan[index];
        const auto address = executableBase + edit.rva + edit.operand;
        const auto page = reinterpret_cast<void*>(address & ~(static_cast<uintptr_t>(pageSize) - 1));
        bool known = false;
        for (size_t i = 0; i < pageCount; ++i) known |= pages[i].address == page;
        if (known) continue;
        MEMORY_BASIC_INFORMATION info{};
        DWORD previous = 0;
        if (!VirtualQuery(page, &info, sizeof(info)) || info.AllocationBase != reinterpret_cast<void*>(executableBase) ||
            info.Type != MEM_IMAGE || info.State != MEM_COMMIT ||
            !(info.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ||
            (info.Protect & PAGE_GUARD) || !VirtualProtect(page, pageSize, PAGE_EXECUTE_READWRITE, &previous)) {
            if (!RestorePages(pages, pageCount, pageSize) && !RestorePages(pages, pageCount, pageSize))
                Log("Could not restore prepared code-page protection; restart recommended.");
            return false;
        }
        pages[pageCount++] = {page, previous};
    }
    const auto toggled = ToggleOwnedPlan(memory, kPlan, ownership, desired, executableSize);
    const bool restored = RestorePages(pages, pageCount, pageSize);
    if (!restored && !RestorePages(pages, pageCount, pageSize))
        Log("Code-page protection restore failed; the byte transaction result below remains authoritative.");
    if (toggled.status != ToggleStatus::Applied) {
        // 只有完整回滚且缓存刷新成功时，最近状态仍可用于显示；否则明确标记未知。
        const bool confirmedPrevious = toggled.status == ToggleStatus::WriteConflict &&
            MatchesOwnedPlan(memory, kPlan, ownership, badIndex) &&
            ownership.owned == NativeFeatureSites(GetAppliedNativeFeatures());
        stateKnown.store(confirmedPrevious, std::memory_order_release);
        char message[192]{};
        sprintf_s(message, "Feature toggle failed: status=%u site=%zu state-known=%u; foreign bytes were preserved.",
            static_cast<unsigned>(toggled.status), toggled.index, confirmedPrevious ? 1u : 0u);
        Log(message);
        return false;
    }
    // 字节及指令缓存完整提交后才改变常驻 Top hook 门控，防止两条入口状态分裂。
    SetAnywhereMenuEnabled(requested.anywhere);
    appliedMask.store(Encode(requested), std::memory_order_release);
    stateKnown.store(true, std::memory_order_release);
    return true;
}

NativeFeatureState GetAppliedNativeFeatures() noexcept {
    return Decode(appliedMask.load(std::memory_order_acquire));
}
bool NativeFeatureStateKnown() noexcept { return stateKnown.load(std::memory_order_acquire); }
}
