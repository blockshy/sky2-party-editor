// 复用主菜单原“编成”操作打开原生完整PartyMenu，不注册窗口或读取XInput。
// 只在原生菜单更新线程中响应，禁止从初始化线程或轮询线程创建游戏UI。
#include "anywhere_menu.h"
#include "runtime.h"
#include "menu_route.h"
#include "menu_signatures.h"
#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstring>

namespace sky2party {
namespace {
uintptr_t gameBase = 0;
using InputHandler = bool(*)(uintptr_t);
using MenuAction = bool(*)(uintptr_t, uint32_t);
using SetState = void(*)(uintptr_t, uint32_t);
InputHandler originalInput = nullptr;
bool hookCreated = false;
std::atomic<bool> routeEnabled{false};

bool ReadBytes(uintptr_t address, void* target, size_t length) noexcept {
    if (address < 0x10000) return false;
    __try { std::memcpy(target, reinterpret_cast<const void*>(address), length); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(uintptr_t address, T& value) noexcept { return ReadBytes(address, &value, sizeof(value)); }

struct NativeRequest {
    uintptr_t camp, top = 0;
    bool OriginalHandled() const noexcept { return originalInput(camp); }
    bool Capture() noexcept {
        uintptr_t field = 0, activeCamp = 0, vtable = 0, savedata = 0;
        uintptr_t parties = 0, party = 0, list = 0, count = 0;
        uint32_t partyIndex = 0, fieldPartyIndex = 0, depth = 0, nextKind = 0;
        uint32_t currentState = 0, pendingState = 0;
        uint8_t storyFlags = 0;
        MenuScope scope;
        if (!Read(gameBase+0xC60E08,field) || !Read(field+0x718,activeCamp) ||
            !Read(camp,vtable) || vtable != gameBase+0xAED4B0 ||
            !Read(camp+0x128,scope.menuKind) || !Read(camp+0x12C,nextKind) ||
            !Read(camp+0x268,scope.environment) || !Read(field+0x1BC8,scope.transition) ||
            !Read(camp+0xF4,depth) || depth >= 4 ||
            !Read(gameBase+0xC60E58,savedata) || !Read(savedata+0x103,storyFlags) ||
            !Read(camp+0x130,partyIndex) || partyIndex >= 4 ||
            !Read(field+0x658,fieldPartyIndex) || !Read(field+0x650,parties) ||
            !Read(camp+0x138,party) || !Read(camp+0x28,list) ||
            !Read(camp+0x30,count) || count == 0 || count > 32) return false;
        scope.ownedByField = activeCamp == camp;
        scope.currentParty = partyIndex == fieldPartyIndex && parties >= 0x10000 && party == parties+partyIndex*0x218;
        scope.storyBlocked = (storyFlags & 0x40) != 0;
        // 仅在父菜单的普通Top状态稳定时响应，防止同帧其它操作已安排换页。
        if (nextKind != 1 || !Read(camp+0xC0+12*depth,currentState) || currentState != 3 ||
            !Read(camp+0xC4+12*depth,pendingState) || pendingState != 3) return false;
        for (uintptr_t i=0; i<count; ++i) {
            uintptr_t child = 0, owner = 0;
            uint32_t id = 0;
            uint32_t currentDepth = 0, pendingDepth = 0;
            uint8_t closing = 0;
            if (!Read(list+i*8,child) || !Read(child+0x10,id)) return false;
            // 已有完整编成页时不能再次创建；同一菜单树只允许一个编成会话。
            if (id == 13) return false;
            if (id != 1) continue;
            if (top || !Read(child,vtable) || vtable != gameBase+0xAFCFC0 ||
                !Read(child+0x48,owner) || owner != camp ||
                !Read(child+0x41,closing) || closing ||
                !Read(child+0xF8,pendingDepth) || pendingDepth >= 4 ||
                !Read(child+0xFC,currentDepth) || currentDepth >= 4) return false;
            top = child;
        }
        scope.validObjects = top != 0;
        return CanRouteFormation(scope);
    }
    bool Allowed() const noexcept { return reinterpret_cast<MenuAction>(gameBase+0x205720)(top,8); }
    bool Pressed() const noexcept { return reinterpret_cast<MenuAction>(gameBase+0x202530)(top,8); }
    bool OpenParty() const noexcept { return reinterpret_cast<MenuAction>(gameBase+0x3580A0)(camp,13); }
    void RecreateTopOnReturn() const noexcept {
        // 清除“关闭后保留此UI实例”位，保留其余标志。游戏会在关闭动画完成后
        // 析构旧TopMenu，返回时重新从真实队伍生成头像和列表，避免显示旧主力。
        // 此字段属于临时UI对象；它不是队伍成员flags，也不写入存档。
        _InterlockedAnd(reinterpret_cast<volatile long*>(top+0x14), static_cast<long>(~1u));
    }
    void WaitForParty() const noexcept {
        reinterpret_cast<SetState>(gameBase+0x138B90)(camp+0x48,11);
        Log("Opened native PartyMenu from ordinary main menu; native return flow armed.");
    }
};

bool HandleTopInput(uintptr_t camp) noexcept {
    // 挂钩常驻以免帧内重建 trampoline；关闭时不捕获UI或额外查询按键。
    if (!routeEnabled.load(std::memory_order_acquire)) return originalInput(camp);
    NativeRequest request{camp};
    return RouteFormation(request);
}
}

bool PrepareAnywhereMenu(uintptr_t base) noexcept {
    // 所有会直接调用的原生入口，以及关闭/返回关键路径均在启用前核对签名。
    // 不接受已经被别的Mod改写的入口，也不根据文件名猜测游戏版本。
    std::array<uint8_t,48> actual{};
    for (const auto& signature : kMenuSignatures) {
        if (!ReadBytes(base+signature.rva,actual.data(),signature.length) ||
            std::memcmp(actual.data(),signature.bytes.data(),signature.length)) {
            Log("Native menu route signature mismatch; anywhere menu was not created.");
            return false;
        }
    }
    gameBase = base;
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    const auto result = MH_CreateHook(reinterpret_cast<void*>(base+0x136E80),
        reinterpret_cast<void*>(&HandleTopInput), reinterpret_cast<void**>(&originalInput));
    hookCreated = result == MH_OK;
    if (!hookCreated) Log("Could not prepare native main-menu input hook.");
    return hookCreated;
}

bool EnableAnywhereMenu() noexcept {
    if (!hookCreated || MH_EnableHook(reinterpret_cast<void*>(gameBase+0x136E80)) != MH_OK) return false;
    Log("Native main-menu route hook resident; feature gate awaits safe game-thread settings.");
    return true;
}

void DiscardAnywhereMenu() noexcept {
    // 只移除本插件创建的一个入口，禁止使用MH_ALL_HOOKS影响其它功能或插件。
    if (hookCreated && MH_RemoveHook(reinterpret_cast<void*>(gameBase+0x136E80)) == MH_OK) hookCreated = false;
}

void SetAnywhereMenuEnabled(bool enabled) noexcept {
    routeEnabled.store(enabled, std::memory_order_release);
}
}
