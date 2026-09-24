// 三组原生菜单代码修改的实际状态。未入队角色由独立 roster 服务管理，
// 不混入这份仅描述代码门槛的状态，也不通过它增加或删除真实名单成员。
#pragma once
#include "unavailable_members.h"

namespace sky2party {
struct NativeFeatureState {
    // 默认全关表示进程原字节；用户默认开启的偏好由控制层另行提交。
    bool fixedMembers = false;
    bool anywhere = false;
    bool unlockUnavailable = false;
};

constexpr bool operator==(const NativeFeatureState& left, const NativeFeatureState& right) noexcept {
    return left.fixedMembers == right.fixedMembers && left.anywhere == right.anywhere &&
        left.unlockUnavailable == right.unlockUnavailable;
}
constexpr bool operator!=(const NativeFeatureState& left, const NativeFeatureState& right) noexcept {
    return !(left == right);
}

// 计划顺序与 kUnlockedPartyEdits 一致：四处固定成员、六处探索入口、五处不可选成员。
// 明确逐项映射可防止新增功能时误将“是否开启”套到相邻但无关的修改点。
constexpr std::array<bool, 15> NativeFeatureSites(const NativeFeatureState& state) noexcept {
    return {{state.fixedMembers, state.fixedMembers, state.fixedMembers, state.fixedMembers,
        state.anywhere, state.anywhere, state.anywhere,
        state.anywhere, state.anywhere, state.anywhere,
        state.unlockUnavailable, state.unlockUnavailable, state.unlockUnavailable,
        state.unlockUnavailable, state.unlockUnavailable}};
}

// 初始化只准备常驻挂钩和原字节基线。真正开关由安全游戏线程调用后一个接口。
bool InstallPartyHooks(uintptr_t executableBase) noexcept;
bool ApplyNativeFeaturesOnGameThread(const NativeFeatureState& requested) noexcept;
NativeFeatureState GetAppliedNativeFeatures() noexcept;
// 外部代码冲突或完整回滚无法确认时为 false；此时上接口只表示最近确认状态。
bool NativeFeatureStateKnown() noexcept;
}
