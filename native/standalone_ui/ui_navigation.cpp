// 只登记实际功能控件，以当帧矩形决定方向邻居，避免 ImGui 默认跨窗口评分
// 将焦点跳到侧栏或远处的对角控件。整个状态仅在 Present 渲染线程使用。
#include "ui_navigation.h"
#include "navigation_policy.h"
#include "ui_layout.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <vector>
namespace sky2solo::navigation {
namespace {
struct Control { ImGuiID id;ImGuiWindow* window;ImGuiID scope;ImRect rect,lane;Kind kind; };
struct State {
    ImGuiContext* context=nullptr;ImGuiWindow* root=nullptr;int frame=-1,page=-1,tabStep=0;
    bool collecting=false,reset=false,interactive=false,controller=false;
    ImGuiID focused=0;float anchorX=0;
    bool disabledFocus=false;ImRect disabledRect,disabledLane;
    Direction direction=Direction::None,held=Direction::None;double repeatAt=0;
    std::vector<Control> controls;
} state;
ImRect CurrentLane(ImGuiWindow* window){
    auto lane=window->InnerRect;
    for(auto* current=window;current;current=current->ParentWindow){
        // BeginChild 会切走 g.CurrentTable，但父窗口仍记录正在绘制的表格。
        // 因而必须沿活动祖先找最近的真实列，不能把卡片/列表自己的内边距
        // 当成列边界：搜索框左侧锚点原本会落在这些内边距之外，跳过整列。
        const int tableIndex=current->DC.CurrentTableIdx;
        const auto* table=tableIndex>=0?GImGui->Tables.GetByIndex(tableIndex):nullptr;
        if(table&&table->InnerWindow==current&&table->CurrentColumn>=0){
            const auto& column=table->Columns[table->CurrentColumn];
            lane.Min.x=column.MinX;lane.Max.x=column.MaxX;return lane;
        }
        if(current==state.root||!(current->Flags&ImGuiWindowFlags_ChildWindow)||!current->ParentWindow)break;
        auto* parent=current->ParentWindow;
        // 只有占满父工作区宽度的嵌套容器才继承父列。并排的窄子框仍保留
        // 自身边界，不能因为同属一个页面就让上下方向跳进另一栏。
        // 2px 只容忍边框和布局像素取整，不按卡片层数叠加模糊容差。
        if(current->Pos.x>parent->WorkRect.Min.x+2||current->Pos.x+current->Size.x<parent->WorkRect.Max.x-2)break;
        lane.Min.x=parent->InnerRect.Min.x;lane.Max.x=parent->InnerRect.Max.x;
    }
    return lane;
}
void CancelMove(){
    // 取消 ImGui 的延后评分结果，防止下一帧在自定义选择之后再次移动。
    ImGui::NavMoveRequestCancel();auto& g=*GImGui;
    g.NavInitRequest=false;g.NavInitResult=ImGuiNavItemData();g.NavAnyRequest=false;
}
void Focus(const Control& control,bool scroll){
    auto& g=*GImGui;ImGui::SetNavWindow(control.window);
    ImGui::SetNavID(control.id,ImGuiNavLayer_Main,control.scope,ImGui::WindowRectAbsToRel(control.window,control.rect));
    g.NavIdIsAlive=true;
    if(scroll)ImGui::ScrollToRectEx(control.window,control.rect,ImGuiScrollFlags_KeepVisibleEdgeX|ImGuiScrollFlags_KeepVisibleEdgeY);
}
float PixelScrollTarget(float current,float delta,float maximum){
    // ImGui 在下一帧应用 ScrollTarget 时会向整数像素截断；布局计算出的
    // ScrollMax 却可能带小数。以实际可到达的像素为边界，避免 996/996.926
    // 永远被判为“尚未到底”，也避免自适应卡片的浮点尾差吞掉外层滚动。
    const float limit=std::floor(std::max(0.0f,maximum));
    current=std::clamp(std::floor(current),0.0f,limit);
    // 有效模拟输入最少推进一个像素。否则高帧率、轻推摇杆时，小于一像素
    // 的请求每帧都被截断，还可能被错误转交给父窗口。
    const float step=delta>0?std::max(1.0f,std::floor(delta)):delta<0?-std::max(1.0f,std::floor(-delta)):0.0f;
    return std::clamp(current+step,0.0f,limit);
}
bool ScrollWindow(ImGuiWindow* window,float x,float y){
    // 从当前内容向外寻找能够滚动的窗口。子框到边缘后可继续滚动外层页，
    // 不要求玩家移动鼠标，也不把右摇杆解释为黄色焦点移动。
    for(auto* current=window;current;current=current->ParentWindow){
        if(!(current->Flags&ImGuiWindowFlags_ChildWindow))break;
        const float baseX=PixelScrollTarget(current->ScrollTarget.x<FLT_MAX?current->ScrollTarget.x-current->DecoOuterSizeX1:current->Scroll.x,0,current->ScrollMax.x);
        const float baseY=PixelScrollTarget(current->ScrollTarget.y<FLT_MAX?current->ScrollTarget.y-current->DecoOuterSizeY1:current->Scroll.y,0,current->ScrollMax.y);
        const float nextX=PixelScrollTarget(baseX,x,current->ScrollMax.x);
        const float nextY=PixelScrollTarget(baseY,y,current->ScrollMax.y);
        if(nextX!=baseX||nextY!=baseY){
            if(nextX!=baseX)ImGui::SetScrollX(current,nextX);
            if(nextY!=baseY)ImGui::SetScrollY(current,nextY);
            return true;
        }
    }return false;
}
bool Pressed(ImGuiKey key){return ImGui::IsKeyPressed(key,true);}
bool Analog(ImGuiKey key){const auto* data=ImGui::GetKeyData(key);return data->Down&&data->AnalogValue>.35f;}
float ColumnAnchor(const Control& control){
    // 同列按钮可能因标签长度而宽窄不一。使用控件左侧内部的稳定锚点，
    // 避免从宽滑块向下时越过短按钮；跨过全宽行时仍保留原来的列位置。
    return control.rect.Min.x+std::min(control.rect.GetWidth()*.5f,12*ImGui::GetStyle().FontScaleDpi);
}
ImGuiWindow* VerticalScrollScope(ImGuiWindow* window){
    // 使用真实可滚动容器作为列表边界。卡片自适应高度的微小浮点余量不算
    // 滚动范围；外层固定页头不在功能列表范围内，也不参与此优先级。
    for(auto* current=window;current&&current!=state.root;current=current->ParentWindow){
        if(!(current->Flags&ImGuiWindowFlags_ChildWindow))break;
        if(std::floor(current->ScrollMax.y)>0)return current;
    }
    return nullptr;
}
bool DescendsFrom(ImGuiWindow* window,ImGuiWindow* ancestor){
    for(auto* current=window;current;current=current->ParentWindow)if(current==ancestor)return true;
    return false;
}
Direction ReadDirection(){
    const bool up=ImGui::IsKeyDown(ImGuiKey_GamepadDpadUp)||Analog(ImGuiKey_GamepadLStickUp);
    const bool down=ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown)||Analog(ImGuiKey_GamepadLStickDown);
    const bool left=ImGui::IsKeyDown(ImGuiKey_GamepadDpadLeft)||Analog(ImGuiKey_GamepadLStickLeft);
    const bool right=ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight)||Analog(ImGuiKey_GamepadLStickRight);
    // 对角输入固定优先纵向；相反方向同时按下则不移动，摇杆微小漂移不生效。
    Direction held=Direction::None;
    if(up!=down)held=up?Direction::Up:Direction::Down;
    else if(left!=right)held=left?Direction::Left:Direction::Right;
    Direction result=Direction::None;const double now=ImGui::GetTime();
    if(held!=state.held){state.held=held;state.repeatAt=now+.33;result=held;}
    else if(held!=Direction::None&&now>=state.repeatAt){state.repeatAt=now+.12;result=held;}
    if(Pressed(ImGuiKey_UpArrow))return Direction::Up;
    if(Pressed(ImGuiKey_DownArrow))return Direction::Down;
    if(Pressed(ImGuiKey_LeftArrow))return Direction::Left;
    if(Pressed(ImGuiKey_RightArrow))return Direction::Right;
    return result;
}
}
void ResetFocus(){
    state.reset=true;state.focused=0;auto& g=*GImGui;
    ImGui::ClearActiveID();CancelMove();
    // 切页不能把旧页的 A/Enter 激活传给新页相同位置的控件。
    g.NavActivateId=g.NavActivateDownId=g.NavActivatePressedId=0;
    g.NavId=0;
}
void Begin(int page,bool reset,bool interactive,int tabStep,bool controller){
    auto& g=*GImGui;
    if(state.context!=GImGui||g.FrameCount<=state.frame){state=State{};state.context=GImGui;}
    const bool pageChanged=state.page!=page;
    state.page=page;state.frame=g.FrameCount;state.interactive=interactive;state.controller=controller;state.root=g.CurrentWindow;
    state.collecting=true;state.controls.clear();state.disabledFocus=false;state.tabStep=interactive?tabStep:0;
    state.direction=Direction::None;state.reset=false;
    if(!interactive){CancelMove();return;}
    // 鼠标输入会隐藏 ImGui 导航框；改用手柄时，即使方向已到边界或焦点
    // 没有变化也立即恢复显示，不能依赖切页或移动到另一控件才显示。
    if(controller)ImGui::SetNavCursorVisible(true);
    if(reset||pageChanged)ResetFocus();
    // 改键弹窗拥有自己的取消按钮，弹窗内仍使用 ImGui 的局部导航。
    if(ImGui::GetTopMostPopupModal())return;
    state.direction=ReadDirection();
    if(!state.reset)CancelMove();
}
void Track(Kind kind){
    if(!state.collecting||!state.interactive)return;
    const auto& item=GImGui->LastItemData;
    if(!item.ID||(item.ItemFlags&ImGuiItemFlags_NoNav))return;
    auto* window=GImGui->CurrentWindow;if(window->Flags&ImGuiWindowFlags_Popup)return;
    if(item.ItemFlags&ImGuiItemFlags_Disabled){
        // 下一页到末页后会变灰，记录其当前几何位置以就近转到上一页，
        // 不把整个功能区的焦点重置到第一项。
        if(item.ID==state.focused){state.disabledFocus=true;state.disabledRect=item.NavRect;state.disabledLane=CurrentLane(window);}
        return;
    }
    // 几何命中仍使用真实控件矩形，纵向列身份则包含整列的嵌套内边距。
    // 这样搜索框能进入列表，短按钮也不会被同排禁用项或卡片缩进隔断。
    state.controls.push_back({item.ID,window,GImGui->CurrentFocusScopeId,item.NavRect,CurrentLane(window),kind});
}
void TrackScrollArea(){
    if(!state.collecting||!state.interactive)return;
    auto* window=GImGui->CurrentWindow;
    if(std::floor(window->ScrollMax.y)<=0&&std::floor(window->ScrollMax.x)<=0)return;
    if(GImGui->CurrentItemFlags&ImGuiItemFlags_Disabled)return;
    // 列表已有可选行时，直接滚动其焦点所在窗口；不再生成一个重叠焦点。
    for(const auto& item:state.controls){for(auto* parent=item.window;parent;parent=parent->ParentWindow)if(parent==window)return;}
    const auto id=window->GetID("##read-only-scroll");
    state.controls.push_back({id,window,GImGui->CurrentFocusScopeId,window->InnerRect,CurrentLane(window),Kind::ScrollRegion});
    if(GImGui->NavId==id&&GImGui->NavCursorVisible){
        // 展示框没有标准交互项，边框单独绘制；只在当前功能框内显示黄色。
        auto* draw=window->DrawList;draw->PushClipRect(window->OuterRectClipped.Min,window->OuterRectClipped.Max,false);
        const auto rect=window->InnerRect;draw->AddRect({rect.Min.x+2,rect.Min.y+2},{rect.Max.x-2,rect.Max.y-2},
            ImGui::GetColorU32(ImGuiCol_NavCursor),3.0f,2.0f);draw->PopClipRect();
    }
}
void End(){
    // 整个功能页只有文字时也允许手柄选中并滚动，不依赖页面必须有按钮。
    if(state.controls.empty())TrackScrollArea();
    state.collecting=false;state.tabStep=0;
    if(!state.interactive||ImGui::GetTopMostPopupModal())return;
    CancelMove();auto& g=*GImGui;auto& controls=state.controls;
    if(controls.empty()){g.NavId=0;state.focused=0;return;}
    // 鼠标点击功能区后从被点击控件继续导航；点击顶部或侧栏不能成为导航目标。
    auto find=[&](ImGuiID id){for(size_t i=0;i<controls.size();++i)if(controls[i].id==id)return static_cast<int>(i);return -1;};
    int current=state.reset?-1:find(g.NavId);
    if(current<0&&!state.reset)current=find(state.focused);
    if(current<0&&!state.reset&&state.disabledFocus){
        float best=FLT_MAX;
        for(size_t i=0;i<controls.size();++i){const auto& item=controls[i];
            if(item.lane.Max.x<=state.disabledLane.Min.x||item.lane.Min.x>=state.disabledLane.Max.x)continue;
            const float score=std::abs(item.rect.GetCenter().y-state.disabledRect.GetCenter().y)*1000+
                std::abs(item.rect.GetCenter().x-state.disabledRect.GetCenter().x);
            if(score<best){best=score;current=static_cast<int>(i);}
        }
    }
    const bool initial=current<0;if(initial)current=0;
    if(initial||controls[current].id!=state.focused)state.anchorX=ColumnAnchor(controls[current]);
    const auto& control=controls[current];int next=current;
    bool editing=g.ActiveId==control.id;
    if(control.kind==Kind::ScrollRegion&&(state.direction==Direction::Up||state.direction==Direction::Down)){
        const float delta=(state.direction==Direction::Up?-1.0f:1.0f)*ImGui::GetTextLineHeightWithSpacing()*3;
        // 十字键也可浏览只读框：先滚动框内内容，到顶/底后再离开这个框。
        const float nextY=PixelScrollTarget(control.window->Scroll.y,delta,control.window->ScrollMax.y);
        if(nextY!=control.window->Scroll.y){ImGui::SetScrollY(control.window,nextY);state.direction=Direction::None;}
    }
    if(editing&&control.kind==Kind::Slider&&(state.direction==Direction::Up||state.direction==Direction::Down)){
        // A 开始改值，左右留给滑块；上下明确结束编辑并转移到同列下一项。
        ImGui::ClearActiveID();editing=false;
    }
    if(!editing&&!state.reset){
        std::vector<Rect> bounds,lanes;bounds.reserve(controls.size());lanes.reserve(controls.size());
        for(const auto& item:controls){bounds.push_back({item.rect.Min.x,item.rect.Min.y,item.rect.Max.x,item.rect.Max.y});
            lanes.push_back({item.lane.Min.x,item.lane.Min.y,item.lane.Max.x,item.lane.Max.y});}
        int neighbor=Neighbor(bounds,current,state.direction,state.anchorX,&lanes);
        if(state.direction==Direction::Up||state.direction==Direction::Down){
            if(auto* scope=VerticalScrollScope(control.window)){
                // 列表的下一组可以隔着分组标题，离屏行的屏幕 Y 还可能越过
                // 外部“使用说明”。先在当前滚动容器内找同列邻居，防止局部
                // 列表尚未走完就被父页面更近的矩形截走；首/末项才向外查找。
                // 同列限制继续由原几何策略判断，左右跨列不使用此优先级。
                std::vector<Rect> localBounds,localLanes;std::vector<int> indices;int localCurrent=-1;
                for(size_t i=0;i<controls.size();++i)if(DescendsFrom(controls[i].window,scope)){
                    if(static_cast<int>(i)==current)localCurrent=static_cast<int>(indices.size());
                    localBounds.push_back(bounds[i]);localLanes.push_back(lanes[i]);indices.push_back(static_cast<int>(i));
                }
                const int local=Neighbor(localBounds,localCurrent,state.direction,state.anchorX,&localLanes);
                if(local>=0)neighbor=indices[static_cast<size_t>(local)];
            }
        }
        if(neighbor>=0)next=neighbor;
        // Tab 仍提供顺序键盘访问，但到达边缘时停住，绝不进入外层导航栏。
        if(Pressed(ImGuiKey_Tab))next=std::clamp(current+(ImGui::GetIO().KeyShift?-1:1),0,static_cast<int>(controls.size())-1);
    }
    const bool moved=next!=current;
    if(moved&&(state.direction==Direction::Left||state.direction==Direction::Right||Pressed(ImGuiKey_Tab)))state.anchorX=ColumnAnchor(controls[next]);
    state.focused=controls[next].id;Focus(controls[next],initial||moved||state.reset);
    // 右摇杆独立控制内容滚动；长页面、只读框和含可选项列表共用同一路径。
    const auto analog=[](ImGuiKey key){const auto* data=ImGui::GetKeyData(key);return data->Down?data->AnalogValue:0.0f;};
    const float amount=600*ImGui::GetStyle().FontScaleDpi*std::min(ImGui::GetIO().DeltaTime,.05f);
    const float x=analog(ImGuiKey_GamepadRStickRight)-analog(ImGuiKey_GamepadRStickLeft);
    const float y=analog(ImGuiKey_GamepadRStickDown)-analog(ImGuiKey_GamepadRStickUp);
    if(!state.reset&&(std::abs(x)>.15f||std::abs(y)>.15f))ScrollWindow(controls[next].window,x*amount,y*amount);
    if(initial||moved||state.reset||state.controller||state.direction!=Direction::None)ImGui::SetNavCursorVisible(true);
}
int32_t TabBar(const char* id,const char* const* labels,int32_t count,int32_t selected){
    if(!labels||count<=0)return selected;
    selected=std::clamp(selected,0,count-1);int next=selected;
    if(state.interactive&&state.tabStep){next=(selected+(state.tabStep>0?1:count-1))%count;state.tabStep=0;}
    ImGui::PushID(id?id:"tabs");ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);
    for(int i=0;i<count;++i){
        // 多语言或窄窗口会自动换行；鼠标命中区不受手柄焦点限制影响。
        if(i){const auto right=ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x;
            const auto width=ImGui::CalcTextSize(labels[i]?labels[i]:"").x+ImGui::GetStyle().FramePadding.x*2;
            if(ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+width<right)ImGui::SameLine();}
        ImGui::PushID(i);if(layout::Tab("tab",labels[i],next==i))next=i;ImGui::PopID();
    }
    ImGui::PopItemFlag();ImGui::PopID();
    if(state.interactive&&next!=selected)ResetFocus();return next;
}
}
