// 统一独立窗口外壳：固定 Aside / Header / Footer，Main 使用剩余空间独立滚动。
// 此文件只处理 ImGui，不轮询设备、不安装挂钩、不读写任何游戏内存或配置。
#include "ui.h"
#include "ui_layout.h"
#include "ui_navigation.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
namespace sky2solo {
namespace {
const char* Pick(int language, const char* const (&texts)[8]) { return texts[std::clamp(language, 0, 7)]; }
const char* CloseText(int language) {
    static const char* values[]{"关闭", "關閉", "閉じる", "Close", "Schließen", "Fermer", "Cerrar", "닫기"}; return Pick(language, values);
}
const char* PausedText(int language) {
    static const char* values[]{"已暂停操作 · 切回游戏后继续", "已暫停操作 · 切回遊戲後繼續", "操作を一時停止 · ゲームに戻ると再開", "Controls paused · Return to the game to continue", "Bedienung pausiert · Zum Spiel zurückkehren", "Commandes en pause · Revenez au jeu", "Controles en pausa · Vuelve al juego", "조작 일시 중지 · 게임으로 돌아가면 계속"}; return Pick(language, values);
}
const char* HelpText(int language, bool controller) {
    static const char* pad[]{"十字键/左摇杆：选择  A：确认/编辑  B：返回  LB/RB：侧栏  LT/RT：页签", "十字鍵/左搖桿：選擇  A：確認/編輯  B：返回  LB/RB：側欄  LT/RT：頁籤", "十字/左スティック：選択  A：決定/編集  B：戻る  LB/RB：分類  LT/RT：タブ", "D-pad/LS: Select  A: Confirm/edit  B: Back  LB/RB: Section  LT/RT: Tab", "Steuerkreuz/LS: Auswahl  A: Bestätigen  B: Zurück  LB/RB: Bereich  LT/RT: Tab", "Croix/LS : sélectionner  A : confirmer  B : retour  LB/RB : section  LT/RT : onglet", "Cruceta/LS: elegir  A: confirmar  B: volver  LB/RB: sección  LT/RT: pestaña", "십자키/LS: 선택  A: 확인/편집  B: 뒤로  LB/RB: 분류  LT/RT: 탭"};
    static const char* keyboard[]{"方向键：选择  Enter：确认/编辑  Esc：返回  PgUp/PgDn：侧栏  Ctrl+PgUp/PgDn：页签", "方向鍵：選擇  Enter：確認/編輯  Esc：返回  PgUp/PgDn：側欄  Ctrl+PgUp/PgDn：頁籤", "矢印：選択  Enter：決定/編集  Esc：戻る  PgUp/PgDn：分類  Ctrl+PgUp/PgDn：タブ", "Arrows: Select  Enter: Confirm/edit  Esc: Back  PgUp/PgDn: Section  Ctrl+PgUp/PgDn: Tab", "Pfeile: Auswahl  Enter: Bestätigen  Esc: Zurück  PgUp/PgDn: Bereich  Ctrl+PgUp/PgDn: Tab", "Flèches : sélectionner  Entrée : confirmer  Échap : retour  PgUp/PgDn : section  Ctrl+PgUp/PgDn : onglet", "Flechas: elegir  Enter: confirmar  Esc: volver  PgUp/PgDn: sección  Ctrl+PgUp/PgDn: pestaña", "방향키: 선택  Enter: 확인/편집  Esc: 뒤로  PgUp/PgDn: 분류  Ctrl+PgUp/PgDn: 탭"};
    return controller ? Pick(language, pad) : Pick(language, keyboard);
}
const char* MoveText(int language) {
    static const char* values[]{"右摇杆：滚动当前内容 · 鼠标拖动顶部标题可移动窗口", "右搖桿：捲動目前內容 · 滑鼠拖曳頂部標題可移動視窗", "右スティック：スクロール · 上部タイトルをドラッグして移動", "RS: Scroll content · Drag the top title to move the window", "RS: Scrollen · Titelleiste ziehen, um das Fenster zu bewegen", "RS : défiler · Faites glisser le titre pour déplacer la fenêtre", "RS: desplazar · Arrastra el título para mover la ventana", "RS: 내용 스크롤 · 위쪽 제목을 끌어 창 이동"}; return Pick(language, values);
}
bool Neutral(const XINPUT_GAMEPAD& p) {
    return !p.wButtons && p.bLeftTrigger <= 30 && p.bRightTrigger <= 30 &&
        std::abs(int(p.sThumbLX)) <= 7849 && std::abs(int(p.sThumbLY)) <= 7849 &&
        std::abs(int(p.sThumbRX)) <= 8689 && std::abs(int(p.sThumbRY)) <= 8689;
}
}
void FeedGamepad(const XINPUT_GAMEPAD* pad, bool enabled) {
    if (!ImGui::GetCurrentContext()) return;
    static ImGuiContext* context = nullptr;
    static bool armed = false, previousEnabled = false;
    static int previousFrame = -1;
    if (context != ImGui::GetCurrentContext() || ImGui::GetFrameCount() < previousFrame) {
        context = ImGui::GetCurrentContext(); armed = previousEnabled = false;
    }
    previousFrame = ImGui::GetFrameCount();
    // 打开组合、失焦后残留按键和重连偏轴不能直接激活首个按钮。只在真正
    // 中立后重新接收导航；退出编辑、页面切换仍由壳和导航层各自消费一次。
    if (!enabled || !pad) armed = false;
    else if (!previousEnabled) armed = Neutral(*pad);
    else if (!armed && Neutral(*pad)) armed = true;
    previousEnabled = enabled && pad;
    const bool accept = enabled && pad && armed && !(pad->wButtons & XINPUT_GAMEPAD_BACK);
    auto& io = ImGui::GetIO();
    if (pad) io.BackendFlags |= ImGuiBackendFlags_HasGamepad; else io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    const struct { ImGuiKey key; WORD button; } keys[]{
        {ImGuiKey_GamepadFaceDown,XINPUT_GAMEPAD_A},{ImGuiKey_GamepadFaceRight,XINPUT_GAMEPAD_B},
        {ImGuiKey_GamepadFaceLeft,XINPUT_GAMEPAD_X},{ImGuiKey_GamepadFaceUp,XINPUT_GAMEPAD_Y},
        {ImGuiKey_GamepadDpadUp,XINPUT_GAMEPAD_DPAD_UP},{ImGuiKey_GamepadDpadDown,XINPUT_GAMEPAD_DPAD_DOWN},
        {ImGuiKey_GamepadDpadLeft,XINPUT_GAMEPAD_DPAD_LEFT},{ImGuiKey_GamepadDpadRight,XINPUT_GAMEPAD_DPAD_RIGHT},
        {ImGuiKey_GamepadL1,XINPUT_GAMEPAD_LEFT_SHOULDER},{ImGuiKey_GamepadR1,XINPUT_GAMEPAD_RIGHT_SHOULDER}};
    for (const auto& key : keys) io.AddKeyEvent(key.key, accept && (pad->wButtons & key.button));
    const auto axis = [&](ImGuiKey key, int value, int deadzone, int maximum) {
        const float amount = accept ? std::clamp(float(value-deadzone)/float(maximum-deadzone),0.0f,1.0f) : 0.0f;
        io.AddKeyAnalogEvent(key, amount > 0, amount);
    };
    const XINPUT_GAMEPAD sample = pad ? *pad : XINPUT_GAMEPAD{};
    axis(ImGuiKey_GamepadLStickLeft,-int(sample.sThumbLX),7849,32767); axis(ImGuiKey_GamepadLStickRight,sample.sThumbLX,7849,32767);
    axis(ImGuiKey_GamepadLStickUp,sample.sThumbLY,7849,32767); axis(ImGuiKey_GamepadLStickDown,-int(sample.sThumbLY),7849,32767);
    axis(ImGuiKey_GamepadRStickLeft,-int(sample.sThumbRX),8689,32767); axis(ImGuiKey_GamepadRStickRight,sample.sThumbRX,8689,32767);
    axis(ImGuiKey_GamepadRStickUp,sample.sThumbRY,8689,32767); axis(ImGuiKey_GamepadRStickDown,-int(sample.sThumbRY),8689,32767);
    axis(ImGuiKey_GamepadL2,sample.bLeftTrigger,30,255); axis(ImGuiKey_GamepadR2,sample.bRightTrigger,30,255);
}
bool DrawWindow(WindowState& state, const WindowSpec& spec, const Sky2Frame& base) {
    auto& io = ImGui::GetIO();
    const float s = std::max(.25f,base.scale);
    const bool interactive = base.foreground != 0;
    bool keepOpen = true;
    const auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key,false); };
    const int count = std::max(1,spec.sectionCount);
    state.section = std::clamp(state.section,0,count-1);
    const int previousSection = state.section;
    int sectionStep = 0, tabStep = 0;
    if (interactive) {
        sectionStep = int(pressed(ImGuiKey_GamepadR1))-int(pressed(ImGuiKey_GamepadL1));
        tabStep = int(pressed(ImGuiKey_GamepadR2))-int(pressed(ImGuiKey_GamepadL2));
        const int pageStep = int(pressed(ImGuiKey_PageDown))-int(pressed(ImGuiKey_PageUp));
        if (io.KeyCtrl) tabStep += pageStep; else sectionStep += pageStep;
        if (sectionStep) state.section = (state.section+(sectionStep>0?1:count-1))%count;
        if (pressed(ImGuiKey_Escape)||pressed(ImGuiKey_GamepadFaceRight)) {
            if (state.editing || ImGui::IsAnyItemActive()) { ImGui::ClearActiveID(); state.editing=false; state.dragging=false; }
            else keepOpen=false;
        }
    } else { state.dragging=false; state.editing=false; }
    const ImVec2 size{std::max(120.0f,std::min(base.width-24*s,980*s)),std::max(120.0f,std::min(base.height-24*s,740*s))};
    if (!state.positioned) { state.position={(base.width-size.x)*.5f,(base.height-size.y)*.5f};state.positioned=true; }
    if (state.dragging && interactive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        state.position={io.MousePos.x-state.dragOffset.x,io.MousePos.y-state.dragOffset.y};
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) state.dragging=false;
    // 分辨率/DPI 改变后自动夹回客户区；至少保留整块标题可拖动，不把窗口留在屏外。
    state.position.x=std::clamp(state.position.x,0.0f,std::max(0.0f,base.width-size.x));
    state.position.y=std::clamp(state.position.y,0.0f,std::max(0.0f,base.height-size.y));
    ImGui::SetNextWindowPos(state.position,ImGuiCond_Always);ImGui::SetNextWindowSize(size,ImGuiCond_Always);
    ImGui::Begin(spec.id?spec.id:"Sky2Standalone",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|
        ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BeginDisabled(!interactive);ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);
    // 内边距取自实际样式；低分辨率只缩放字体时，固定的 24*s 会小于按钮
    // 真正占用的左右留白，导致德语等较长的关闭文字被错误折成两行。
    const float closeWidth=std::max(70*s,ImGui::CalcTextSize(CloseText(spec.language)).x+ImGui::GetStyle().FramePadding.x*2+1);
    const float titleWidth=std::max(1.0f,ImGui::GetContentRegionAvail().x-closeWidth-ImGui::GetStyle().ItemSpacing.x);
    const auto titlePosition=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##drag-title",{titleWidth,34*s});
    if(interactive&&ImGui::IsItemActivated()){state.dragging=true;state.dragOffset={io.MousePos.x-state.position.x,io.MousePos.y-state.position.y};}
    ImGui::GetWindowDrawList()->AddText({titlePosition.x,titlePosition.y+7*s},ImGui::GetColorU32(ImVec4(.58f,.88f,.79f,1)),spec.title?spec.title:"");
    ImGui::SameLine();if(layout::WrappedButton(CloseText(spec.language),closeWidth))keepOpen=false;
    ImGui::Separator();ImGui::Spacing();
    const char* help=interactive?HelpText(spec.language,base.controller!=0):PausedText(spec.language);
    const float availableWidth=ImGui::GetContentRegionAvail().x;
    const float footer=ImGui::CalcTextSize(help,nullptr,false,availableWidth).y+
        ImGui::CalcTextSize(MoveText(spec.language),nullptr,false,availableWidth).y+ImGui::GetStyle().ItemSpacing.y*4+1;
    const float body=std::max(60*s,ImGui::GetContentRegionAvail().y-footer);
    const float aside=std::min(168*s,std::max(82*s,availableWidth*.24f));
    ImGui::BeginChild("aside",{aside,body},ImGuiChildFlags_NavFlattened);
    for(int index=0;index<count;++index){
        const char* label=spec.sections&&index<spec.sectionCount?spec.sections[index]:spec.title;
        const float width=ImGui::GetContentRegionAvail().x;
        const auto position=ImGui::GetCursorScreenPos();
        const float height=std::max(43*s,ImGui::CalcTextSize(label?label:"",nullptr,false,std::max(1.0f,width-20*s)).y+18*s);
        ImGui::PushID(index);if(ImGui::Selectable("##section",state.section==index,0,{width,height}))state.section=index;
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{position.x+10*s,position.y+9*s},
            ImGui::GetColorU32(state.section==index?ImVec4(.58f,.92f,.79f,1):ImVec4(.66f,.75f,.79f,1)),label?label:"",nullptr,std::max(1.0f,width-20*s));
        ImGui::PopID();
    }
    ImGui::EndChild();ImGui::SameLine();ImGui::PopItemFlag();
    if(state.section!=previousSection){state.resetFocus=true;tabStep=0;if(spec.changed)spec.changed(spec.user,state.section);}
    ImGui::PushID(state.section);ImGui::BeginChild("content",{0,body},ImGuiChildFlags_NavFlattened,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    navigation::Begin(state.section,state.resetFocus,interactive,tabStep,base.controller!=0);state.resetFocus=false;
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);
    const char* heading=spec.sections&&state.section<spec.sectionCount?spec.sections[state.section]:spec.title;
    layout::Heading(heading?heading:"",1.35f);if(spec.description)layout::Muted(spec.description);ImGui::Spacing();
    Sky2Frame frame=base;frame.size=sizeof(frame);frame.panel_open=1;frame.page_active=1;frame.header_drawn=0;
    if(spec.header){spec.header(spec.user,frame,state.section);frame.header_drawn=1;}
    ImGui::Separator();ImGui::Spacing();ImGui::PopItemFlag();
    ImGui::BeginChild("main",{0,0},ImGuiChildFlags_NavFlattened);SetContentViewport(ImGui::GetContentRegionAvail());
    if(spec.draw)spec.draw(spec.user,frame,state.section);
    navigation::End();SetContentViewport({});ImGui::EndChild();ImGui::EndChild();ImGui::PopID();ImGui::EndDisabled();
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);ImGui::Spacing();ImGui::Separator();layout::Muted(help);layout::Muted(MoveText(spec.language));ImGui::PopItemFlag();
    // 拖动标题不算编辑业务控件；释放拖动后 Esc/B 应立即能关闭窗口。
    state.editing=interactive&&!state.dragging&&ImGui::IsAnyItemActive();
    ImGui::End();io.MouseDrawCursor=interactive&&!base.controller;return keepOpen;
}
}
