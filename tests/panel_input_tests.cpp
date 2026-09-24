// 控制面板输入的隔离回归：只执行纯状态机，不加载游戏、不连接实际手柄或改写存档。
#include "panel_input_policy.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace sky2party::panelinput;
static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static Pad Buttons(uint16_t value) { Pad p{}; p.buttons = value; return p; }

// 参照已发布 Chest 0.6.0 的 View 延迟补发、组合消耗和残留按钮过滤约定建立模型。
// 不把这个模型称作真实 DLL 共存验证；这里验证的是两条调用顺序下双方的协议行为。
// Chest 的任意已占用组合都会令 actions 非零；未占用的 LS 只把 View 标记为已消费。
struct ChestContract {
    uint16_t previous = 0, blocked = 0;
    bool pending = false, used = false, rearm = true;
    uint32_t actions = 0;
    Pad Update(Pad raw) {
        if (rearm) { rearm = !Neutral(raw); previous = raw.buttons; return raw; }
        const auto pressed = static_cast<uint16_t>(raw.buttons & ~previous);
        blocked &= raw.buttons;
        auto game = raw;
        if (raw.buttons & View) {
            if (!pending) { pending = true; used = false; }
            auto rest = raw; rest.buttons &= static_cast<uint16_t>(~View);
            used |= !Neutral(rest);
            actions |= pressed & 0xF38F; // ABXY、LB/RB、RS、十字键；LS 不属于 Chest 操作。
            if (raw.lt > 30 || raw.rt > 30) actions |= 0x10000;
            blocked |= static_cast<uint16_t>(raw.buttons & ~View);
            game = {};
        } else {
            if (pending && !used) game.buttons |= View;
            pending = false;
            game.buttons &= static_cast<uint16_t>(~blocked);
        }
        previous = raw.buttons;
        return game;
    }
};
struct ChainHarness {
    PadPolicy party;
    ChestContract chest;
    Chain order;
    bool open = false;
    uint32_t partyActions = 0;
    explicit ChainHarness(Chain value) : order(value) { Step({}); }
    Pad Step(Pad raw) {
        const auto result = party.Update(raw, open, true, order);
        partyActions = result.actions;
        if (result.actions & Toggle) open = !open;
        else if (result.actions & Close) open = false;
        // 导出钩子总在取得 raw 后处理；区别在于 Chest 的过滤器位于 Party IAT 的内外。
        if (order == Chain::ChestInside)
            return FinalGamePad(chest.Update(result.downstream), result, order, open);
        if (order == Chain::ChestOutside)
            return chest.Update(FinalGamePad(result.downstream, result, order, open));
        return FinalGamePad(result.downstream, result, order, open);
    }
};

int main() {
    unsigned scenarios = 0;
    {
        PadPolicy p;
        Require(p.Update(Buttons(View | LeftStick), false, true, Chain::Standalone).actions == 0,
            "initial held chord must wait for release");
        p.Update({}, false, true, Chain::Standalone);
        Require(p.Update(Buttons(View | LeftStick), false, true, Chain::Standalone).actions == Toggle,
            "new chord after release opens exactly once");
        Require(p.Update(Buttons(View | LeftStick), true, true, Chain::Standalone).actions == 0,
            "held chord cannot repeat toggle");
        ++scenarios;
    }
    {
        ChainHarness h(Chain::Standalone);
        Require(h.Step(Buttons(View)).buttons == 0, "standalone View press is delayed");
        Require(h.Step({}).buttons == View, "standalone single View replays on release");
        Require(h.Step({}).buttons == 0, "single View only replays one frame");
        ++scenarios;
    }
    {
        ChainHarness h(Chain::Standalone);
        Pad analog = Buttons(View); analog.lx = 20000;
        h.Step(analog);
        Require(h.Step({}).buttons == 0, "View plus analog is not a single View press");
        ++scenarios;
    }
    {
        ChainHarness h(Chain::Standalone);
        h.Step(Buttons(LeftStick)); h.Step(Buttons(View | LeftStick));
        Require(!h.open, "holding LS before View must not activate chord");
        h.Step({}); h.Step(Buttons(View)); h.Step(Buttons(View | LeftStick));
        Require(h.open, "View then newly pressed LS activates chord");
        ++scenarios;
    }
    {
        ChainHarness h(Chain::Standalone);
        h.Step(Buttons(View | LeftStick));
        h.Step(Buttons(LeftStick | 0x1000));
        Require(h.partyActions == 0, "opening chord requires full release before A");
        h.Step({}); h.Step(Buttons(0x1000));
        Require(h.partyActions == Activate, "A navigates only after release");
        h.Step(Buttons(0x1000)); Require(h.partyActions == 0, "holding A has no repeat");
        ++scenarios;
    }
    {
        ChainHarness h(Chain::Standalone);
        h.Step(Buttons(View | LeftStick)); h.Step({});
        Require(Neutral(h.Step(Buttons(0x2000))) && !h.open, "B closes and is swallowed");
        Require(Neutral(h.Step(Buttons(0x2000 | 0x1000))), "close tail consumes all held buttons");
        h.Step({}); Require(h.Step(Buttons(0x1000)).buttons == 0x1000, "release restores native A");
        ++scenarios;
    }
    {
        ChainHarness h(Chain::Standalone);
        h.Step(Buttons(View | LeftStick)); h.Step({});
        h.Step(Buttons(1)); Require(h.partyActions == Previous, "Dpad up selects previous");
        h.Step({}); h.Step(Buttons(2)); Require(h.partyActions == Next, "Dpad down selects next");
        h.Step({}); h.Step(Buttons(3)); Require(h.partyActions == 0, "opposed directions cancel");
        h.Step({}); h.Step(Buttons(0x3000)); Require(h.partyActions == Close, "B wins over simultaneous A");
        ++scenarios;
    }
    {
        PadPolicy p; p.Update({}, false, true, Chain::Standalone);
        p.Update(Buttons(View | LeftStick), false, false, Chain::Standalone);
        Require(p.Update(Buttons(View | LeftStick), false, true, Chain::Standalone).actions == 0,
            "focus return cannot activate an old held chord");
        p.Update({}, false, true, Chain::Standalone);
        Require(p.Update(Buttons(View | LeftStick), false, true, Chain::Standalone).actions == Toggle,
            "focus return rearms after neutral");
        ++scenarios;
    }
    {
        PadPolicy p; p.Update({}, false, true, Chain::Standalone); p.Reset();
        Require(p.Update(Buttons(View | LeftStick), false, true, Chain::Standalone).actions == 0,
            "disconnect/reconnect requires neutral");
        p.Update({}, false, true, Chain::Standalone);
        Require(p.Update(Buttons(View | LeftStick), false, true, Chain::Standalone).actions == Toggle,
            "reconnected controller operates after release");
        ++scenarios;
    }
    {
        PadPolicy p; p.Update({}, false, true, Chain::Standalone);
        p.Update(Buttons(View), false, true, Chain::Standalone);
        Require(p.Update(Buttons(View | LeftStick), false, true, Chain::ChestInside).actions == 0,
            "newly loaded Chest chain requires release");
        p.Update({}, false, true, Chain::ChestInside);
        Require(p.Update(Buttons(View | LeftStick), false, true, Chain::ChestInside).actions == Toggle,
            "new chain rearms after neutral");
        ++scenarios;
    }
    {
        ChainHarness h(Chain::Unknown);
        Require(h.Step(Buttons(View | LeftStick)).buttons == (View | LeftStick) && !h.open,
            "unknown raw chain must not pretend controller support");
        h.open = true;
        Require(Neutral(h.Step(Buttons(0x1000))), "keyboard-open unknown chain still consumes controller");
        h.open = false;
        Require(Neutral(h.Step(Buttons(0x1000))), "unknown chain still has closing release guard");
        h.Step({}); Require(h.Step(Buttons(0x1000)).buttons == 0x1000, "unknown chain recovers after neutral");
        ++scenarios;
    }
    {
        Result absent{};
        Require(Neutral(FinalGamePad(Buttons(0x1000), absent, Chain::Unknown, true)),
            "final IAT capture does not depend on seeing raw export");
        absent.capture = true;
        Require(Neutral(FinalGamePad(Buttons(0x2000), absent, Chain::Unknown, false)),
            "final IAT accepts fallback release-tail capture");
        ++scenarios;
    }
    for (const auto order : {Chain::ChestInside, Chain::ChestOutside}) {
        {
            ChainHarness h(order); h.open = true;
            Require(Neutral(h.Step({})) && h.chest.previous == 0 && !h.chest.pending,
                "pure keyboard opening does not synthesize controller activity in Chest");
            ++scenarios;
        }
        {
            ChainHarness h(order);
            Require(Neutral(h.Step(Buttons(View))), "Chest single View prefix is neutral");
            Require(Neutral(h.Step(Buttons(View | LeftStick))) && h.open,
                "opening chord is consumed in either installation order");
            Require(Neutral(h.Step({})), "seed prevents Chest replay of a pending View");
            Require(h.chest.actions == 0, "View LS does not trigger Chest action");
            h.Step(Buttons(2)); Require(h.partyActions == Next, "panel Dpad works in either order");
            h.Step({}); Require(Neutral(h.Step(Buttons(0x2000))) && !h.open, "B close consumed in either order");
            Require(Neutral(h.Step(Buttons(0x2000))), "B held tail consumed in either order");
            h.Step({}); Require(h.Step(Buttons(0x1000)).buttons == 0x1000, "native A restored in either order");
            Require(h.chest.actions == 0, "panel A B and Dpad do not operate Chest");
            ++scenarios;
        }
        {
            ChainHarness h(order);
            h.Step(Buttons(View));
            h.open = true; // F11 打开时物理 View 仍未松开，也必须消费 Chest 的 pending。
            h.Step(Buttons(View));
            Require(Neutral(h.Step({})) && h.chest.actions == 0, "keyboard opening seeds pending Chest View");
            h.open = false; h.Step({});
            h.Step(Buttons(View)); Require(h.Step({}).buttons == View, "Chest single View restored after closing");
            ++scenarios;
        }
        {
            ChainHarness h(order);
            h.Step(Buttons(View));
            h.open = true;
            // F11 打开与松开 View 落在同一采样帧，原始样本已中立；仍需消费外层 pending。
            Require(Neutral(h.Step({})) && h.chest.actions == 0,
                "keyboard opening on View release cannot replay map in outer Chest");
            h.open = false; h.Step({});
            h.Step(Buttons(View)); Require(h.Step({}).buttons == View,
                "same-frame opening seed does not break later native View");
            ++scenarios;
        }
        {
            ChainHarness h(order); h.Step(Buttons(View));
            h.party.Reset(); h.chest = {}; h.open = true;
            Require(Neutral(h.Step({})) && h.chest.actions == 0,
                "reconnected Chest receives neutral before any opening seed");
            ++scenarios;
        }
        {
            ChainHarness h(order); h.party.Reset(); h.chest = {};
            h.Step(Buttons(0x1000)); h.open = true;
            Require(Neutral(h.Step(Buttons(0x1000))), "rearm held A is captured when F11 opens");
            h.open = false;
            Require(Neutral(h.Step(Buttons(0x1000))), "rearm held A remains captured after F11 closes");
            Require(Neutral(h.Step({})), "rearm close tail releases without map replay");
            Require(h.Step(Buttons(0x1000)).buttons == 0x1000, "new A after rearm tail works normally");
            ++scenarios;
        }
        {
            ChainHarness h(order);
            h.Step(Buttons(View)); h.Step(Buttons(View | 0x1000));
            Require(h.chest.actions != 0 && !h.open, "unrelated Chest View A stays usable when Party hidden");
            ++scenarios;
        }
        {
            // 枚举 65536 个按钮组合，确保打开帧只向 Chest 种下 View+LS，绝不携带其已占用键。
            for (uint32_t mask = 0; mask <= 0xFFFF; ++mask) {
                ChainHarness h(order); h.Step(Buttons(View));
                auto raw = Buttons(static_cast<uint16_t>(mask | View | LeftStick));
                raw.lt = 255; raw.rt = 255; raw.lx = 32767; raw.ry = -32768;
                Require(Neutral(h.Step(raw)) && h.open && h.chest.actions == 0,
                    "opening seed strips every unrelated button and analog input");
                Require(Neutral(h.Step({})) && h.chest.actions == 0,
                    "exhaustive opening seed never replays map or Chest command");
            }
            ++scenarios;
        }
    }
    {
        KeyboardPolicy p;
        Require(p.Update(1, false, true) == 0, "initial held F11 is ignored");
        p.Update(0, false, true);
        Require(p.Update(1, false, true) == Toggle, "new F11 toggles");
        Require(p.Update(1, true, true) == 0, "held F11 does not repeat");
        Require(p.Update(9, true, true) == 0, "opening F11 requires release before Enter");
        p.Update(0, true, true);
        Require(p.Update(8, true, true) == Activate, "Enter activates after release");
        p.Update(0, true, true);
        Require(p.Update(24, true, true) == Close, "Escape wins over Enter");
        ++scenarios;
    }
    {
        KeyboardPolicy p; p.Update(0, true, true); p.Update(0, true, true); p.Update(0, true, true);
        Require(p.Update(2, true, true) == Previous, "keyboard up selects previous");
        p.Update(0, true, true); Require(p.Update(4, true, true) == Next, "keyboard down selects next");
        p.Update(0, true, true); Require(p.Update(6, true, true) == 0, "keyboard opposite directions cancel");
        p.Update(1, true, false); Require(p.Update(1, true, true) == 0, "focus return ignores old F11");
        p.Update(0, true, true); Require(p.Update(1, true, true) == Toggle, "focus rearm restores F11");
        ++scenarios;
    }
    {
        Confirmation c;
        Require(!c.Press(119, 3, 100) && c.Armed(119, 3, 101), "first confirm only arms selected role");
        Require(c.Press(119, 3, 200) && !c.Armed(119, 3, 200), "second confirm consumes authorization");
        c.Press(119, 3, 300);
        Require(!c.Press(4, 3, 301), "different role cannot consume previous authorization");
        Require(!c.Press(4, 4, 302), "save generation change cannot consume previous authorization");
        Require(!c.Press(4, 4, 9000), "expired authorization requires a fresh first confirmation");
        c.Cancel(); Require(!c.Press(4, 4, 9001), "close or selection cancel requires fresh confirmation");
        ++scenarios;
    }
    std::printf("PASS: %u panel input/confirmation scenarios; both Chest orders x 65536 opening combinations.\n", scenarios);
}
