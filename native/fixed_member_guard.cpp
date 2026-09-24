// 卸载兼容守卫的生产读取器：仅访问已经校验宿主中的当前队伍对象，不修改游戏或存档。
#include "fixed_member_guard.h"
#include <Windows.h>
#include <atomic>

namespace sky2party {
namespace {
std::atomic<uintptr_t> guardBase{0};
struct ProcessReader {
    bool Read(uintptr_t address, void* output, size_t length) noexcept {
        __try { std::memcpy(output, reinterpret_cast<const void*>(address), length); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
};
}

bool ConfigureFixedMemberGuard(uintptr_t verifiedBase) noexcept {
    uintptr_t ignored = 0;
    if (!verifiedBase || !fixedguard_detail::Add(verifiedBase, kFixedGuardManagerRva, ignored)) {
        guardBase.store(0, std::memory_order_release);
        return false;
    }
    guardBase.store(verifiedBase, std::memory_order_release);
    return true;
}

FixedMemberGuardResult InspectFixedMemberGuard() noexcept {
    ProcessReader reader;
    return InspectFixedMemberGuardFromMemory(reader, guardBase.load(std::memory_order_acquire));
}
}
