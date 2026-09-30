// 执行真实独立窗口和导航组件，覆盖固定页头、侧栏/Tab快捷切换、焦点范围、
// 失焦只读与拖动。用 ImGui 事件队列模拟输入，不操纵真实桌面或运行中的游戏。
#include "ui.h"
#include <imgui_internal.h>
#include <cstdlib>
#include <iostream>
#include <cmath>
namespace {
sky2solo::WindowState state;
Sky2Frame frame{sizeof(Sky2Frame),1600,1000,1,1000,1,1,1,1};
ImGuiID first=0,nextId=0,helpId=0;
ImGuiWindow* mainWindow=nullptr;
ImVec2 headerPosition{};
int tab=0,clicks=0,changes=0;
bool open=true,longPage=false;
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<" nav="<<GImGui->NavId<<"\n";std::exit(1);}}
void Header(void*,const Sky2Frame&,int){headerPosition=ImGui::GetCursorScreenPos();const char* labels[]{"Settings","List"};tab=sky2solo::UiApi()->tab_bar("tabs",labels,2,tab);}
void Page(void*,const Sky2Frame& value,int){
    Check(value.header_drawn==1,"Main knows fixed header already drew");mainWindow=GImGui->CurrentWindow;
    auto* ui=sky2solo::UiApi();float width=0,height=0;ui->content_size(&width,&height);Check(width>100&&height>100,"Main exposes actual remaining viewport");
    if(ui->button("first","First"))++clicks;first=GImGui->LastItemData.ID;
    ui->begin_disabled(1);ui->button("disabled","Previous");ui->end_disabled();ui->same_line();ui->button("next","Next");nextId=GImGui->LastItemData.ID;
    if(longPage)for(int i=0;i<70;++i)ui->text("Long page detail for scrolling");
    ui->disclosure("help","Instructions",0);helpId=GImGui->LastItemData.ID;
}
const char* sections[]{"Functions","Instructions"};
sky2solo::WindowSpec spec{"SoloFixture","Standalone fixture","Fixed header description",sections,2,nullptr,&Header,&Page,[](void*,int){++changes;},3};
void Draw(){auto& io=ImGui::GetIO();io.DisplaySize={frame.width,frame.height};io.DeltaTime=1.0f/60;
    ImGui::NewFrame();const bool keepOpen=sky2solo::DrawWindow(state,spec,frame);open=open&&keepOpen;ImGui::Render();}
void Key(ImGuiKey key){auto& io=ImGui::GetIO();io.AddKeyEvent(key,true);Draw();io.AddKeyEvent(key,false);Draw();}
}
int main(){
    auto* context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    ImFontConfig font;font.SizePixels=20;io.Fonts->AddFontDefault(&font);unsigned char* pixels=nullptr;int width=0,height=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    sky2solo::ConfigureTheme();io.BackendFlags|=ImGuiBackendFlags_HasGamepad;Draw();Draw();Draw();
    Check(GImGui->NavId==first,"initial focus is a Main control");Key(ImGuiKey_GamepadDpadLeft);Check(GImGui->NavId==first,"left boundary never enters sidebar");
    Key(ImGuiKey_GamepadDpadDown);Check(GImGui->NavId==nextId,"disabled previous does not block next");
    Key(ImGuiKey_GamepadDpadDown);Check(GImGui->NavId==helpId,"disclosure can receive focus");
    Key(ImGuiKey_GamepadR2);Check(tab==1&&GImGui->NavId==first,"RT switches header tab and resets Main focus");
    Key(ImGuiKey_GamepadR1);Check(state.section==1&&changes==1,"RB switches aside without navigating into it");
    io.AddKeyEvent(ImGuiMod_Ctrl,true);Draw();Key(ImGuiKey_PageUp);io.AddKeyEvent(ImGuiMod_Ctrl,false);Draw();Check(tab==0,"Ctrl+PgUp switches tab");
    Key(ImGuiKey_PageUp);Check(state.section==0,"PgUp switches section");
    frame.controller=0;ImGui::SetNavCursorVisible(false);Draw();frame.controller=1;Draw();Check(GImGui->NavCursorVisible,"controller restores highlight after mouse");
    longPage=true;state.resetFocus=true;Draw();Draw();const auto fixed=headerPosition;
    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown,true,1);for(int i=0;i<35;++i)Draw();io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown,false,0);Draw();
    Check(mainWindow->Scroll.y>0,"right stick scrolls outer Main");Check(std::abs(headerPosition.y-fixed.y)<.1f,"Header does not scroll with Main");
    auto* root=ImGui::FindWindowByName("SoloFixture");Check(root&&std::floor(root->ScrollMax.y)==0,"root has no hidden vertical scroll");
    frame.foreground=0;const int oldTab=tab;Key(ImGuiKey_GamepadR2);Key(ImGuiKey_GamepadFaceDown);Check(tab==oldTab&&clicks==0&&root->Active,"background panel remains visible and read-only");
    frame.foreground=1;Draw();const auto before=state.position;
    io.AddMousePosEvent(before.x+70,before.y+25);Draw();io.AddMouseButtonEvent(0,true);Draw();
    io.AddMousePosEvent(before.x+170,before.y+85);Draw();io.AddMouseButtonEvent(0,false);Draw();
    Check(state.position.x>before.x+80&&state.position.y>before.y+40,"title dragging moves the window");
    frame.width=800;frame.height=600;Draw();Check(state.position.x>=0&&state.position.y>=0&&state.position.x+root->Size.x<=801,"resolution shrink keeps window on screen");
    frame.foreground=0;Key(ImGuiKey_Escape);Check(open,"background Escape cannot close read-only window");
    frame.foreground=1;Draw();Key(ImGuiKey_Escape);Check(!open,"Escape closes idle foreground window");
    ImGui::DestroyContext(context);std::cout<<"standalone window contract passed\n";
}
