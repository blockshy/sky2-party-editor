// 控制面板与游戏线程之间的命令边界：界面只提交期望设置或角色请求，不直接操作游戏对象。
#pragma once
#include "roster_service.h"
#include "fixed_member_guard.h"
#include <array>
#include <cstdint>

namespace sky2party {
enum FeatureMask : uint32_t {
    FeatureFixedMembers = 1u << 0,
    FeatureAnywhere = 1u << 1,
    FeatureUnavailable = 1u << 2,
    FeatureUnjoined = 1u << 3,
};
inline constexpr uint32_t kAllFeatures = 15;
inline constexpr uint32_t kDefaultFeatures = 7;

struct ControlSnapshot {
    uint32_t requestedFeatures = kDefaultFeatures;
    uint32_t appliedFeatures = 0;
    bool appliedStateKnown = true;
    bool waitingForSafeState = true;
    std::array<char, 192> message{};
    RosterSnapshot roster{};
    // 游戏线程采样的四队固定成员兼容性，不向界面暴露可解引用的游戏地址。
    // 超过半秒未更新时 fresh=false；界面不能用旧的“零风险”宣称卸载安全。
    FixedMemberGuardResult fixedGuard{};
    bool fixedGuardFresh = false;
};

// 返回值为完整副本，不把游戏指针或内部锁的所有权交给绘制线程。
ControlSnapshot ReadControlSnapshot() noexcept;
// 设置会在正常探索且原生菜单关闭时统一应用；允许关掉全部功能而保留控制面板。
void RequestFeatureMask(uint32_t features) noexcept;
// 启动时一次配置：参数来自本插件自己的INI，而非角色存档。只创建已核对的常驻协调挂钩。
bool InstallControlService(uintptr_t executableBase, uint32_t initialFeatures) noexcept;
}
