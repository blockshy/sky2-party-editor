// 独立页面的轻量布局适配器，只调用当前绘制上下文借用的函数表，不拥有
// ImGui 对象。精简测试替身缺少布局回调时，控件以线性布局呈现以保留可测试性。
#pragma once
#include "sky2_ui.h"
#include <stddef.h>
#include <string>
namespace sky2ui {
// 帧大小检查也适用于精简测试帧；只有实际完成页头绘制才跳过 Main 内的页签。
inline bool HeaderDrawn(const Sky2Frame& frame){
    return frame.size>=offsetof(Sky2Frame,header_drawn)+sizeof(frame.header_drawn)&&frame.header_drawn!=0;
}
// 尺寸查询只借用输出地址，不保存指针。精简替身与非页面回调返回 false，
// 调用方应保留原有高度；不得用自动增高卡片的剩余空间反推 Main 大小。
inline bool ContentSize(const Sky2UiApi& ui,float* width,float* height){
    float w=0,h=0;
    if(ui.size>=offsetof(Sky2UiApi,content_size)+sizeof(ui.content_size)&&ui.content_size)ui.content_size(&w,&h);
    if(width)*width=w;if(height)*height=h;return w>0&&h>0;
}
inline bool HasLayout(const Sky2UiApi& ui) {
    return ui.size >= offsetof(Sky2UiApi,disclosure)+sizeof(ui.disclosure) &&
        ui.section&&ui.begin_card&&ui.end_card&&ui.columns&&ui.next_column&&
        ui.end_columns&&ui.tab&&ui.status&&ui.progress&&ui.disclosure;
}
inline void Section(const Sky2UiApi& ui,const char* title,const char* description=nullptr) {
    if(HasLayout(ui))ui.section(title,description);
    else {if(ui.separator)ui.separator();if(title&&ui.text)ui.text(title);if(description&&ui.text_wrapped)ui.text_wrapped(description);}
}
inline void BeginCard(const Sky2UiApi& ui,const char* id){if(HasLayout(ui))ui.begin_card(id);else if(ui.spacing)ui.spacing();}
inline void EndCard(const Sky2UiApi& ui){if(HasLayout(ui))ui.end_card();else if(ui.spacing)ui.spacing();}
// 返回后已位于首列。调用方每完成一项才 NextColumn；回退时 NextColumn
// 不执行横排，所以长翻译和窄窗口仍可完整使用。必须与 EndColumns 配对。
inline int Columns(const Sky2UiApi& ui,const char* id,float minimumWidth=300.0f){return HasLayout(ui)?ui.columns(id,minimumWidth):1;}
inline void NextColumn(const Sky2UiApi& ui){if(HasLayout(ui))ui.next_column();}
inline void EndColumns(const Sky2UiApi& ui){if(HasLayout(ui))ui.end_columns();}
inline bool Tab(const Sky2UiApi& ui,const char* id,const char* label,bool selected){return HasLayout(ui)?ui.tab(id,label,selected)!=0:ui.button&&ui.button(id,label)!=0;}
inline bool HasTabBar(const Sky2UiApi& ui){return ui.size>=offsetof(Sky2UiApi,tab_bar)+sizeof(ui.tab_bar)&&ui.tab_bar;}
inline int TabBar(const Sky2UiApi& ui,const char* id,const char* const* labels,int count,int selected){
    if(!labels||count<=0)return selected;
    if(HasTabBar(ui))return ui.tab_bar(id,labels,count,selected);
    // 通用回退使用稳定序号ID。需要保留旧按钮ID的模块可自行回退；无论如何，
    // 所有按钮都先绘完再返回单一结果，避免同一帧重复切页或部分页面先被执行。
    int next=selected;for(int i=0;i<count;++i){if(i&&ui.same_line)ui.same_line();const auto key=std::string(id?id:"tabs")+"."+std::to_string(i);if(Tab(ui,key.c_str(),labels[i],i==selected))next=i;}
    return next;
}
inline void Status(const Sky2UiApi& ui,const char* text,int tone=0){if(!text||!*text)return;if(HasLayout(ui))ui.status(text,tone);else if(ui.text_wrapped)ui.text_wrapped(text);}
inline void Progress(const Sky2UiApi& ui,float fraction,const char* label){if(HasLayout(ui))ui.progress(fraction,label);else if(label&&ui.text)ui.text(label);}
// 折叠标题本身即可结束，不要求调用 end。旧 UI 总显示内容，不能把必需的
// 设置藏在不存在的控件后面。危险操作的确认应始终放在折叠内容之外。
inline bool Disclosure(const Sky2UiApi& ui,const char* id,const char* label,bool defaultOpen=false){
    if(HasLayout(ui))return ui.disclosure(id,label,defaultOpen)!=0;
    if(label&&ui.text)ui.text(label);return true;
}
}
