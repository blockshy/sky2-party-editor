// 独立窗口的功能区导航。业务页面只登记控件，内部焦点由窗口组件统一管理。
#pragma once
#include <cstdint>
namespace sky2solo::navigation {
enum class Kind { Button, Slider, TextInput, ScrollRegion };
// 每帧包围右侧功能区；顶部页签只消耗 tabStep，不登记为可选择控件。
void Begin(int page,bool reset,bool interactive,int tabStep,bool controller=false);
void End();
void Track(Kind kind=Kind::Button);
// 在固定高度子窗口结束前调用；只有纯展示且可滚动的区域增加独立焦点。
void TrackScrollArea();
void ResetFocus();
int32_t TabBar(const char* id,const char* const* labels,int32_t count,int32_t selected);
}
