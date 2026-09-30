// 控制面板输入的纯状态机：只计算动作和过滤结果，不依赖 Win32、ImGui 或游戏内存。
#pragma once
#include <cstdint>
#include <cstdlib>
#include <array>

namespace sky2party::panelinput {
// GetAsyncKeyState 对保留 VK 没有物理按键语义，不能以遍历 1..255 的方式
// 判断“全部松开”。现场的 0x07 持续为高位曾令关闭后的 keyboardTail 永久
// 吞掉键鼠；仅纳入已分配的实体键，排除 XInput/Unicode/IME 注入占位值。
inline bool IsPhysicalVirtualKey(unsigned key) noexcept {
    return (key >= 0x01 && key <= 0x06) || key == 0x08 || key == 0x09 || key == 0x0C || key == 0x0D ||
        (key >= 0x10 && key <= 0x39) || (key >= 0x41 && key <= 0x5D) || (key >= 0x5F && key <= 0x87) ||
        (key >= 0x90 && key <= 0x96) || (key >= 0xA0 && key <= 0xB7) || (key >= 0xBA && key <= 0xC0) ||
        (key >= 0xDB && key <= 0xDF) || (key >= 0xE1 && key <= 0xE4) || key == 0xE6 ||
        (key >= 0xE9 && key <= 0xFB) || key == 0xFD || key == 0xFE;
}
template<class ReadKey> bool AnyPhysicalKeyDown(ReadKey read) noexcept {
    for (unsigned key = 1; key < 256; ++key)
        if (IsPhysicalVirtualKey(key) && (read(static_cast<int>(key)) & 0x8000)) return true;
    return false;
}
// 可见窗口不等于拥有输入。停止成功绘制后最多 500ms 自动放行，即使游戏
// 不再调用 Present，IAT/WndProc 仍可独立判定超时，避免不可见面板锁住游戏。
inline bool FrameHealthy(uint64_t lastSuccess, uint64_t now) noexcept {
    return lastSuccess != 0 && now >= lastSuccess && now - lastSuccess <= 500;
}
enum Action : uint32_t { Toggle = 1, Previous = 2, Next = 4, Activate = 8, Close = 16 };
enum class Chain { Standalone, ChestOutside, ChestInside, Unknown };
inline constexpr uint16_t View = 0x20, LeftStick = 0x40;
// 当前独立窗口使用 View + 十字键左。LeftStick 仅供下方保留的旧协议
// 回归模型使用，不能再把旧 View + LS 当作生产窗口的别名。
inline constexpr uint16_t PanelToggleButton = 0x04;
inline bool PanelChordDown(uint16_t buttons) noexcept {
    // 三个窗口分别占用十字键方向；斜方向不得同时命中两个面板。
    // 其它按钮、摇杆和扳机沿用既有规则，不据此扩大输入拦截范围。
    return (buttons & (View | 0x0F)) == (View | PanelToggleButton);
}
struct Pad {
    uint16_t buttons = 0;
    uint8_t lt = 0, rt = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};
inline bool Neutral(const Pad& p) noexcept {
    return p.buttons == 0 && p.lt <= 15 && p.rt <= 15 &&
        std::abs(static_cast<int>(p.lx)) <= 7849 && std::abs(static_cast<int>(p.ly)) <= 7849 &&
        std::abs(static_cast<int>(p.rx)) <= 8689 && std::abs(static_cast<int>(p.ry)) <= 8689;
}
inline bool HasChest(Chain chain) noexcept { return chain == Chain::ChestOutside || chain == Chain::ChestInside; }
struct Result {
    Pad downstream{};
    uint32_t actions = 0;
    bool capture = false, seedChest = false, activity = false;
};

class PadPolicy {
    Pad previous_{};
    Chain chain_ = Chain::Unknown;
    bool rearm_ = true, panelArmed_ = false, previousOpen_ = false, tail_ = false;
    bool viewPending_ = false, viewUsed_ = false;
    bool chestArmed_ = false;
    void ObserveChestNeutral(const Pad& sent) noexcept {
        if (HasChest(chain_) && Neutral(sent)) chestArmed_ = true;
    }
public:
    void Reset() noexcept { *this = PadPolicy{}; }
    Result Update(const Pad& raw, bool open, bool foreground, Chain chain) noexcept {
        Result r{raw};
        if (!foreground) { Reset(); previous_ = raw; return r; }
        if (chain != chain_) {
            // 安装顺序变化时不沿用上一条链的 View 待补发状态，必须松键后重新开始。
            chain_ = chain; rearm_ = true; viewPending_ = false; viewUsed_ = false; chestArmed_ = false;
        }
        const auto pressed = static_cast<uint16_t>(raw.buttons & ~previous_.buttons);
        const bool viewSeen = ((raw.buttons | previous_.buttons) & View) != 0;
        r.activity = pressed != 0 || (!Neutral(raw) && Neutral(previous_));
        if (rearm_) {
            rearm_ = !Neutral(raw);
            previous_ = raw; previousOpen_ = open; panelArmed_ = Neutral(raw);
            // 初装/重连按键未松期间也可能用 F11 开关面板；关闭后的尾部保护
            // 必须覆盖这一分支，否则长按 A 会在等待重新武装时泄漏回游戏。
            if (open) tail_ = true;
            else if (Neutral(raw)) tail_ = false;
            r.capture = open || tail_;
            if (r.capture) {
                r.downstream = {};
                // Chest 初装/重连先等中立，其 rearm 分支会原样透传。必须先给它
                // 一帧中立完成武装，之后才能发送 View+LS，避免种子自己漏给游戏。
                r.seedChest = HasChest(chain) && chestArmed_ && viewSeen;
                if (r.seedChest) r.downstream.buttons = View | LeftStick;
            }
            ObserveChestNeutral(r.downstream);
            return r;
        }
        const bool chord = chain != Chain::Unknown && (raw.buttons & View) && (pressed & LeftStick);
        if (chord) r.actions = Toggle;
        if (open && !previousOpen_) panelArmed_ = false;
        if (open && !panelArmed_ && Neutral(raw)) panelArmed_ = true;
        if (open && panelArmed_ && !chord && !(raw.buttons & View)) {
            if (pressed & 0x2000) r.actions |= Close;
            else {
                if ((pressed & 3) == 1) r.actions |= Previous;
                if ((pressed & 3) == 2) r.actions |= Next;
                if (pressed & 0x1000) r.actions |= Activate;
            }
        }
        const bool nextOpen = (r.actions & Close) ? false : (chord ? !open : open);
        // 组合键打开的这一帧尚未经历一次完全松键，不能借用关闭时留下的导航资格。
        if (!open && nextOpen) panelArmed_ = false;
        // 关闭后仍过滤未松开的 A/B/摇杆等，防止同一次按压落入游戏探索操作。
        if (open || nextOpen) tail_ = true;
        else if (Neutral(raw)) tail_ = false;
        r.capture = open || nextOpen || tail_;
        if (r.capture) {
            r.downstream = {};
            // Chest 先收到单独 View 时会等待补发。交给它一次未占用的 View+LS，
            // 把 pending 标记为“已使用”，避免随后中立状态被误认为单按 View 松开。
            // 纯键盘打开且前后均未按 View 时无需种子，避免让 Chest 的提示伪切为手柄。
            r.seedChest = HasChest(chain) && chestArmed_ && (chord || (open && !previousOpen_ && viewSeen));
            if (r.seedChest) r.downstream.buttons = View | LeftStick;
            viewPending_ = false; viewUsed_ = true;
        } else if (chain == Chain::Standalone) {
            // 没有 Chest 时自行延迟单独 View；其短按原功能在松开后补发一次。
            if (raw.buttons & View) {
                if (!viewPending_) { viewPending_ = true; viewUsed_ = false; }
                auto withoutView = raw;
                withoutView.buttons &= static_cast<uint16_t>(~View);
                viewUsed_ |= !Neutral(withoutView);
                r.downstream.buttons &= static_cast<uint16_t>(~View);
            } else {
                if (viewPending_ && !viewUsed_) r.downstream.buttons |= View;
                viewPending_ = false;
            }
        }
        ObserveChestNeutral(r.downstream);
        previous_ = raw; previousOpen_ = nextOpen;
        return r;
    }
};

// 新独立窗口使用合作 IAT 调用域：每层读取同一个真实样本，只聚合捕获请求，
// 最外层才清零。这里不再伪造 Chest 种子，也不修改 XInput 导出返回值。
// 原 PadPolicy 保留给历史串联协议回归；新入口只使用此策略。
class SoloPadPolicy {
    Pad previous_{};
    bool rearm_ = true, tail_ = false, pendingView_ = false, usedView_ = false;
public:
    struct Output {
        bool toggle = false, capture = false, replayView = false, activity = false;
    };
    void Reset() noexcept { *this = SoloPadPolicy{}; }
    void BeginCaptureTail() noexcept { tail_ = true; }
    Output Update(const Pad& raw, bool open, bool available, bool anotherWindowOpen = false,
        uint16_t toggleButton = PanelToggleButton) noexcept {
        Output result{};
        if (!available) { Reset(); previous_ = raw; return result; }
        const uint16_t pressed = static_cast<uint16_t>(raw.buttons & ~previous_.buttons);
        const bool moved = std::abs(int(raw.lx) - previous_.lx) > 2048 || std::abs(int(raw.ly) - previous_.ly) > 2048 ||
            std::abs(int(raw.rx) - previous_.rx) > 2048 || std::abs(int(raw.ry) - previous_.ry) > 2048 ||
            std::abs(int(raw.lt) - previous_.lt) > 5 || std::abs(int(raw.rt) - previous_.rt) > 5;
        result.activity = pressed != 0 || (!Neutral(raw) && (Neutral(previous_) || moved));
        if (rearm_) {
            rearm_ = !Neutral(raw);
            if (open) tail_ = true;
            else if (Neutral(raw)) tail_ = false;
            result.capture = open || tail_;
            previous_ = raw;
            return result;
        }
        // 生产入口从配置快照传入按钮；精确匹配数字按钮，避免改绑后多按钮或
        // 斜方向分别被两个 Mod 命中。轴仅参与活动/松键判定，不用于全局绑定。
        result.toggle = toggleButton && raw.buttons == static_cast<uint16_t>(View | toggleButton) && (pressed & toggleButton);
        const bool nextOpen = result.toggle ? !open : open;
        if (open || nextOpen) tail_ = true;
        else if (Neutral(raw)) tail_ = false;
        result.capture = open || nextOpen || tail_;
        if (raw.buttons & View) {
            if (!pendingView_) { pendingView_ = true; usedView_ = false; }
            auto remainder = raw; remainder.buttons &= static_cast<uint16_t>(~View);
            usedView_ |= !Neutral(remainder) || result.capture || anotherWindowOpen;
            // View 是三个独立面板的共同前缀；按下阶段暂不交给游戏。
            result.capture = true;
        } else {
            result.replayView = pendingView_ && !usedView_ && !result.capture && !anotherWindowOpen;
            pendingView_ = false;
        }
        previous_ = raw;
        return result;
    }
};

// IAT 是最后一道防线：Chest 在外层时保留一次安全种子让其消费，否则直接中立。
inline Pad FinalGamePad(const Pad& received, const Result& result, Chain chain, bool open) noexcept {
    if (chain == Chain::ChestOutside && result.seedChest) {
        Pad seed{}; seed.buttons = View | LeftStick; return seed;
    }
    return result.capture || open ? Pad{} : received;
}

class KeyboardPolicy {
    uint32_t previous_ = 0;
    bool rearm_ = true, previousOpen_ = false, panelArmed_ = false;
public:
    // bits 0..4 分别为窗口开关键、上、下、Enter、Escape；当前入口映射 F8。
    // 一次真实按下只发一个动作；修饰键由生产调用方在消费动作时核验。
    uint32_t Update(uint32_t down, bool open, bool foreground) noexcept {
        const auto pressed = down & ~previous_; previous_ = down;
        if (!foreground) { rearm_ = true; previousOpen_ = false; panelArmed_ = false; return 0; }
        if (rearm_) { rearm_ = down != 0; return 0; }
        if (open && !previousOpen_) panelArmed_ = false;
        previousOpen_ = open;
        if (pressed & 1) { panelArmed_ = false; return Toggle; }
        if (!open) return 0;
        if (!panelArmed_) { panelArmed_ = down == 0; return 0; }
        if (pressed & 16) return Close;
        uint32_t actions = 0;
        if ((pressed & 6) == 2) actions |= Previous;
        if ((pressed & 6) == 4) actions |= Next;
        if (pressed & 8) actions |= Activate;
        return actions;
    }
};

// 两次确认绑定角色、存档代次和时间；切换选择、关窗和读档均由调用者取消。
class Confirmation {
    uint32_t id_ = UINT32_MAX;
    uint64_t generation_ = 0, expires_ = 0;
public:
    void Cancel() noexcept { id_ = UINT32_MAX; expires_ = 0; }
    bool Armed(uint32_t id, uint64_t generation, uint64_t now) const noexcept {
        return id == id_ && generation == generation_ && now <= expires_ && expires_ != 0;
    }
    bool Press(uint32_t id, uint64_t generation, uint64_t now) noexcept {
        if (Armed(id, generation, now)) { Cancel(); return true; }
        id_ = id; generation_ = generation; expires_ = now + 8000; return false;
    }
};
}
