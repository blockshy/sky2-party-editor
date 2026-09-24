// 控制面板输入的纯状态机：只计算动作和过滤结果，不依赖 Win32、ImGui 或游戏内存。
#pragma once
#include <cstdint>
#include <cstdlib>

namespace sky2party::panelinput {
enum Action : uint32_t { Toggle = 1, Previous = 2, Next = 4, Activate = 8, Close = 16 };
enum class Chain { Standalone, ChestOutside, ChestInside, Unknown };
inline constexpr uint16_t View = 0x20, LeftStick = 0x40;
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
    // bits 0..4 分别为 F11、上、下、Enter、Escape；一次真实按下只发一个动作。
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
