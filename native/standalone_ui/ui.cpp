// 独立窗口的 UI 函数表。业务页面绘制内容，窗口组件管理输入焦点、字体
// 和缩放。所有函数均在对应 Mod 持有绘制锁的 Present 线程执行。
#include "ui.h"
#include <imgui_internal.h>
#include <vector>
#include "ui_layout.h"
#include "ui_navigation.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace sky2solo {
namespace {
int moduleDisabledDepth=0;std::vector<bool> disabledScopes;
// 仅在 Main 的绘制作用域内保存可视尺寸。查询不暴露 ImGuiWindow，也不会
// 随 auto-resize 卡片、滚动条位置或上一帧的内容高度变化；全部由渲染线程使用。
ImVec2 contentViewport{};
void ContentSize(float* width,float* height){if(width)*width=contentViewport.x;if(height)*height=contentViewport.y;}
// 动态字库按真实 Unicode 码点查询，缺字时让业务页保留原有降级提示。
bool FontCovers(const char* text){
    if(!text||!ImGui::GetCurrentContext())return false;
    auto* font=ImGui::GetFont();if(!font)return false;
    while(*text){unsigned int code=0;const int count=ImTextCharFromUtf8(&code,text,nullptr);
        if(count<=0||code==IM_UNICODE_CODEPOINT_INVALID)return false;text+=count;
        if(code>=32&&code!=127&&(code>IM_UNICODE_CODEPOINT_MAX||!font->IsGlyphInFont(static_cast<ImWchar>(code))))return false;
    }return true;
}
const char* Safe(const char* value){return value?value:"";}
void Text(const char* text){ImGui::TextUnformatted(Safe(text));}
void TextWrapped(const char* text){ImGui::PushTextWrapPos();Text(text);ImGui::PopTextWrapPos();}
int32_t Button(const char* id,const char* label){ImGui::PushID(id);const bool value=layout::WrappedButton(Safe(label));ImGui::PopID();return value;}
int32_t Checkbox(const char* id,const char* label,int32_t* value){
    if(!value)return 0;ImGui::PushID(id);const float s=ImGui::GetStyle().FontScaleDpi;
    // 整行是同一个标准按钮命中区，长翻译自动换行；键鼠与手柄共用同一个
    // ImGui 导航项。视觉复选框只是绘图，不能额外生成第二次操作。
    const float width=std::max(1.0f,ImGui::GetContentRegionAvail().x),wrap=std::max(1.0f,width-42*s);
    const auto text=ImGui::CalcTextSize(Safe(label),nullptr,false,wrap);const float height=std::max(24*s,text.y)+10*s;
    const auto position=ImGui::GetCursorScreenPos();ImGui::PushStyleColor(ImGuiCol_Button,{0,0,0,0});
    const bool changed=ImGui::Button("##checkbox",{width,height});ImGui::PopStyleColor();if(changed)*value=!*value;
    auto* draw=ImGui::GetWindowDrawList();const ImVec2 box{position.x+4*s,position.y+(height-20*s)*.5f};
    draw->AddRectFilled(box,{box.x+20*s,box.y+20*s},ImGui::GetColorU32(*value?ImGuiCol_ButtonActive:ImGuiCol_FrameBg),4*s);
    draw->AddRect(box,{box.x+20*s,box.y+20*s},ImGui::GetColorU32(*value?ImGuiCol_CheckMark:ImGuiCol_Border),4*s);
    if(*value){const auto color=ImGui::GetColorU32(ImGuiCol_CheckMark);draw->AddLine({box.x+4*s,box.y+10*s},{box.x+8*s,box.y+14*s},color,2*s);draw->AddLine({box.x+8*s,box.y+14*s},{box.x+16*s,box.y+6*s},color,2*s);}
    draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{position.x+36*s,position.y+(height-text.y)*.5f},ImGui::GetColorU32(ImGuiCol_Text),Safe(label),nullptr,wrap);
    navigation::Track();ImGui::PopID();return changed;
}
int32_t Slider(const char* id,const char* label,float* value,float minimum,float maximum,const char* format){ImGui::PushID(id);TextWrapped(label);ImGui::SetNextItemWidth(-1);const bool changed=ImGui::SliderFloat("##value",value,minimum,maximum,format?format:"%.2f");navigation::Track(navigation::Kind::Slider);ImGui::PopID();return changed;}
int32_t Selectable(const char* id,const char* label,int32_t selected){
    ImGui::PushID(id);const float s=ImGui::GetStyle().FontScaleDpi,width=std::max(1.0f,ImGui::GetContentRegionAvail().x);
    const auto position=ImGui::GetCursorScreenPos();const float wrap=std::max(1.0f,width-18*s);
    const float height=std::max(34*s,ImGui::CalcTextSize(Safe(label),nullptr,false,wrap).y+14*s);
    const bool result=ImGui::Selectable("##row",selected!=0,0,{width,height});
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{position.x+9*s,position.y+7*s},ImGui::GetColorU32(ImGuiCol_Text),Safe(label),nullptr,wrap);
    navigation::Track();ImGui::PopID();return result;
}
int32_t BeginChild(const char* id,float height){return ImGui::BeginChild(id,ImVec2(0,height),ImGuiChildFlags_Borders|ImGuiChildFlags_NavFlattened);}
int32_t InputText(const char* id,const char* label,char* buffer,uint32_t capacity){ImGui::PushID(id);TextWrapped(label);ImGui::SetNextItemWidth(-1);const bool result=ImGui::InputText("##input",buffer,capacity);navigation::Track(navigation::Kind::TextInput);ImGui::PopID();return result;}
void TextColor(uint32_t rgba,const char* text){ImGui::PushStyleColor(ImGuiCol_Text,rgba);Text(text);ImGui::PopStyleColor();}
ImDrawList* Draw(){return ImGui::GetBackgroundDrawList();}
void Line(float x1,float y1,float x2,float y2,uint32_t c,float t){Draw()->AddLine({x1,y1},{x2,y2},c,t);}
void Rect(float x1,float y1,float x2,float y2,uint32_t c,float rounding,float thickness,int32_t filled){if(filled)Draw()->AddRectFilled({x1,y1},{x2,y2},c,rounding);else Draw()->AddRect({x1,y1},{x2,y2},c,rounding,0,thickness);}
void Circle(float x,float y,float r,uint32_t c,float thickness,int32_t filled){if(filled)Draw()->AddCircleFilled({x,y},r,c);else Draw()->AddCircle({x,y},r,c,0,thickness);}
void Triangle(float x1,float y1,float x2,float y2,float x3,float y3,uint32_t c,float t,int32_t filled){if(filled)Draw()->AddTriangleFilled({x1,y1},{x2,y2},{x3,y3},c);else Draw()->AddTriangle({x1,y1},{x2,y2},{x3,y3},c,t);}
void DrawText(float x,float y,float size,uint32_t c,const char* text){if(text)Draw()->AddText(ImGui::GetFont(),size,{x,y},c,text);}
void Measure(const char* text,float size,float* width,float* height){const auto value=ImGui::GetFont()->CalcTextSizeA(size,FLT_MAX,0,Safe(text));if(width)*width=value.x;if(height)*height=value.y;}
const Sky2UiApi ui={sizeof(Sky2UiApi),&Text,&TextWrapped,[]{ImGui::Separator();},[]{const auto right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;if(ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+110*ImGui::GetStyle().FontScaleDpi<right)ImGui::SameLine();},[]{ImGui::Spacing();},&Button,&Checkbox,&Slider,&Selectable,
    [](int32_t disabled){ImGui::BeginDisabled(disabled!=0);disabledScopes.push_back(disabled!=0);moduleDisabledDepth+=disabled?1:0;},
    []{ImGui::EndDisabled();if(!disabledScopes.empty()){moduleDisabledDepth-=disabledScopes.back()?1:0;disabledScopes.pop_back();}},&BeginChild,[]{navigation::TrackScrollArea();ImGui::EndChild();},&InputText,&TextColor,
    &Line,&Rect,&Circle,&Triangle,&DrawText,&Measure,[](float x1,float y1,float x2,float y2){Draw()->PushClipRect({x1,y1},{x2,y2},true);},[]{Draw()->PopClipRect();},
    [](const char* text)->int32_t{return FontCovers(text);},[]()->int32_t{return ImGui::IsAnyItemActive();},
    &layout::Section,&layout::BeginCard,&layout::EndCard,&layout::Columns,&layout::NextColumn,&layout::EndColumns,
    &layout::Tab,&layout::Status,&layout::Progress,&layout::Disclosure,&navigation::TabBar,&ContentSize};
}
const Sky2UiApi* UiApi(){return &ui;}
void SetContentViewport(ImVec2 size){contentViewport=size;}
void ConfigureTheme(){layout::Theme();auto& style=ImGui::GetStyle();
    // 延续队伍独立窗口的深青底与青绿边框；导航色继续统一为黄色。
    style.WindowPadding={18,14};style.ItemSpacing={9,8};style.WindowRounding=9;
    style.Colors[ImGuiCol_WindowBg]={.035f,.07f,.085f,.98f};
    style.Colors[ImGuiCol_Border]={.3f,.65f,.61f,.85f};
    auto& io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad|ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigNavMoveSetMousePos=false;io.ConfigWindowsMoveFromTitleBarOnly=true;io.IniFilename=nullptr;
}
}
