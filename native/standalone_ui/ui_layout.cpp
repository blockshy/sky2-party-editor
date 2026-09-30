// 统一卡片、标题、状态和自适应列。控件使用 ImGui 标准按钮/表格导航，
// 装饰只绘入当前窗口 DrawList，不生成额外窗口，不参与游戏输入挂钩。
#include "ui_layout.h"
#include "ui_navigation.h"
#include <imgui.h>
#include <algorithm>
#include <vector>
namespace sky2solo::layout {
namespace {
std::vector<bool> columnScopes;
float Scale(){return ImGui::GetStyle().FontScaleDpi;}
const char* Safe(const char* value){return value?value:"";}
}
void Muted(const char* text){
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(0);ImGui::TextUnformatted(Safe(text));ImGui::PopTextWrapPos();ImGui::PopStyleColor();
}
void Heading(const char* text,float factor){
    // PushFont 的字号是未乘 DPI 的逻辑字号；不能把 GetFontSize 的已缩放值
    // 再乘一次 DPI，否则高分辨率标题会过大、低分辨率标题会过小。
    const auto& style=ImGui::GetStyle();
    ImGui::PushFont(nullptr,ImGui::GetFontSize()/std::max(.01f,style.FontScaleDpi*style.FontScaleMain)*factor);ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(Safe(text));ImGui::PopTextWrapPos();ImGui::PopFont();
}
void Section(const char* title,const char* description){
    ImGui::Spacing();Heading(title,1.08f);
    if(description&&*description)Muted(description);
    ImGui::Spacing();
}
void BeginCard(const char* id){
    // AlwaysAutoResize 保证初次出现和滚动后仍计算真实内容高度，避免短卡片
    // 变成滚动窗。Begin/End 必须配对，调用方无需根据可见性跳过业务控件。
    const auto s=Scale();ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{16*s,14*s});
    // ScaleAllSizes 会将小于 1px 的边框取整为 0；显式保留内边距与细边框，
    // 使 720p、1000p 等缩小显示不会把内容贴到卡片边缘。
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,std::max(1.0f,ImGui::GetStyle().ChildBorderSize));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,{.053f,.076f,.108f,1});
    ImGui::BeginChild(id,{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding|ImGuiChildFlags_AutoResizeY|
        ImGuiChildFlags_AlwaysAutoResize|ImGuiChildFlags_NavFlattened,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
}
void EndCard(){ImGui::EndChild();ImGui::PopStyleColor();ImGui::PopStyleVar(2);ImGui::Spacing();}
int32_t Columns(const char* id,float minimumWidth){
    const auto s=Scale();const int count=ImGui::GetContentRegionAvail().x>=std::max(160.0f,minimumWidth)*s*2+16*s?2:1;
    const bool started=ImGui::BeginTable(id,count,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoSavedSettings|ImGuiTableFlags_PadOuterX);
    columnScopes.push_back(started);if(started)ImGui::TableNextColumn();return started?count:1;
}
void NextColumn(){if(!columnScopes.empty()&&columnScopes.back())ImGui::TableNextColumn();}
void EndColumns(){if(!columnScopes.empty()){if(columnScopes.back())ImGui::EndTable();columnScopes.pop_back();}}
bool WrappedButton(const char* label,float width){
    const auto& style=ImGui::GetStyle();const float available=std::max(1.0f,ImGui::GetContentRegionAvail().x);
    if(width<=0)width=std::min(available,ImGui::CalcTextSize(Safe(label)).x+style.FramePadding.x*2);
    width=std::max(1.0f,std::min(width,available));
    const float wrap=std::max(1.0f,width-style.FramePadding.x*2);
    const auto text=ImGui::CalcTextSize(Safe(label),nullptr,false,wrap);
    const float height=std::max(ImGui::GetFrameHeight(),text.y+style.FramePadding.y*2);
    const auto position=ImGui::GetCursorScreenPos();const bool clicked=ImGui::Button("##button",{width,height});
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),
        {position.x+(width-text.x)*.5f,position.y+(height-text.y)*.5f},ImGui::GetColorU32(ImGuiCol_Text),Safe(label),nullptr,wrap);
    navigation::Track();return clicked;
}
int32_t Tab(const char* id,const char* label,int32_t selected){
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button,selected?ImVec4(.13f,.29f,.31f,1):ImVec4(.07f,.105f,.15f,1));
    ImGui::PushStyleColor(ImGuiCol_Text,selected?ImVec4(.57f,.91f,.80f,1):ImVec4(.62f,.70f,.79f,1));
    const auto position=ImGui::GetCursorScreenPos();const bool clicked=WrappedButton(label);
    if(selected){const auto end=ImGui::GetItemRectMax();ImGui::GetWindowDrawList()->AddRectFilled({position.x+9*Scale(),end.y-2*Scale()},{end.x-9*Scale(),end.y},IM_COL32(103,215,183,255),Scale());}
    ImGui::PopStyleColor(2);ImGui::PopID();return clicked;
}
void Status(const char* text,int32_t tone){
    if(!text||!*text)return;
    const ImVec4 colors[]={{.52f,.69f,.87f,1},{.42f,.83f,.67f,1},{.94f,.72f,.38f,1},{.97f,.48f,.46f,1}};
    const auto color=colors[std::clamp(tone,0,3)];const float s=Scale(),width=std::max(1.0f,ImGui::GetContentRegionAvail().x);
    const float wrap=std::max(1.0f,width-30*s);const auto size=ImGui::CalcTextSize(text,nullptr,false,wrap);
    const auto position=ImGui::GetCursorScreenPos();const float height=size.y+20*s;auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(position,{position.x+width,position.y+height},ImGui::GetColorU32({color.x*.17f,color.y*.17f,color.z*.17f,1}),6*s);
    draw->AddRectFilled(position,{position.x+3*s,position.y+height},ImGui::GetColorU32(color),2*s);
    draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{position.x+15*s,position.y+10*s},ImGui::GetColorU32(color),text,nullptr,wrap);
    ImGui::Dummy({width,height});
}
void Progress(float fraction,const char* label){
    if(label&&*label){ImGui::PushTextWrapPos(0);ImGui::TextUnformatted(label);ImGui::PopTextWrapPos();}
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,{.36f,.75f,.63f,1});
    ImGui::ProgressBar(std::clamp(fraction,0.0f,1.0f),{-1,6*Scale()},"");ImGui::PopStyleColor();ImGui::Spacing();
}
int32_t Disclosure(const char* id,const char* label,int32_t defaultOpen){
    ImGui::PushID(id);const bool open=ImGui::CollapsingHeader(label,defaultOpen?ImGuiTreeNodeFlags_DefaultOpen:0);navigation::Track();ImGui::PopID();return open;
}
void Theme(){
    ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();
    style.WindowPadding={24,22};style.FramePadding={12,9};style.ItemSpacing={12,10};style.ItemInnerSpacing={9,6};style.CellPadding={7,6};
    style.WindowRounding=14;style.ChildRounding=9;style.FrameRounding=6;style.PopupRounding=10;style.ScrollbarRounding=6;style.GrabRounding=4;
    style.WindowBorderSize=1;style.ChildBorderSize=1;style.FrameBorderSize=0;style.ScrollbarSize=11;style.GrabMinSize=12;
    auto* c=style.Colors;
    c[ImGuiCol_Text]={.88f,.92f,.96f,1};c[ImGuiCol_TextDisabled]={.51f,.61f,.70f,1};
    c[ImGuiCol_WindowBg]={.033f,.050f,.077f,.99f};c[ImGuiCol_ChildBg]={.053f,.076f,.108f,1};c[ImGuiCol_PopupBg]={.060f,.084f,.118f,1};
    c[ImGuiCol_Border]={.17f,.23f,.29f,.65f};c[ImGuiCol_Separator]={.16f,.23f,.29f,.75f};
    c[ImGuiCol_FrameBg]={.085f,.125f,.170f,1};c[ImGuiCol_FrameBgHovered]={.115f,.20f,.24f,1};c[ImGuiCol_FrameBgActive]={.13f,.27f,.29f,1};
    c[ImGuiCol_Button]={.12f,.23f,.28f,1};c[ImGuiCol_ButtonHovered]={.16f,.35f,.38f,1};c[ImGuiCol_ButtonActive]={.18f,.43f,.43f,1};
    c[ImGuiCol_Header]={.12f,.27f,.29f,1};c[ImGuiCol_HeaderHovered]={.13f,.30f,.34f,1};c[ImGuiCol_HeaderActive]={.15f,.36f,.37f,1};
    c[ImGuiCol_CheckMark]={.46f,.87f,.72f,1};c[ImGuiCol_SliderGrab]={.37f,.74f,.66f,1};c[ImGuiCol_SliderGrabActive]={.58f,.93f,.78f,1};
    c[ImGuiCol_NavCursor]={1.0f,.83f,.27f,1};c[ImGuiCol_TextSelectedBg]={.23f,.52f,.52f,.55f};
    c[ImGuiCol_ScrollbarBg]={.025f,.045f,.069f,.6f};c[ImGuiCol_ScrollbarGrab]={.21f,.29f,.36f,1};c[ImGuiCol_ScrollbarGrabHovered]={.30f,.42f,.49f,1};
}
}
