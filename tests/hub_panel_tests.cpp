// 用函数表替身运行生产 Hub 页面，不依赖 ImGui、游戏、输入设备或真实存档。
// 检查的重点是跨页面确认生命周期和命令边界，而非控件是否被逐行绘制。
#include "hub_panel.h"
#include "control_state.h"
#include "game_names.h"
#include <cstdio>
#include <cstddef>
#include <set>
#include <string>
#include <vector>
#ifdef SKY2_MODULE_VISUAL_FIXTURE
#include "visual_capture.h"
#include <filesystem>
#include <Windows.h>
#endif

const Sky2HostApi* Sky2Hub_Host = nullptr;
namespace {
using namespace sky2party;
ControlSnapshot fixture;
std::vector<Sky2Action> actions;
std::string button, selection, checkbox;
int calls = 0, requests = 0, opens = 0, failures = 0, disabled = 0, language = 0;
int cardDepth = 0, columnDepth = 0, extendedSections = 0, layoutColumns = 2;
int tabBarCalls = 0, requestedPage = -1, observedPage = -1;
float viewportHeight = 700.0f, lastListHeight = 0;
int viewportQueries = 0;
std::vector<std::string> visibleTexts;
uint32_t joined = kNoRosterId;
void Check(bool value, const char* what) { if (!value) { ++failures; std::fprintf(stderr, "FAILED: %s\n", what); } }
void SKY2_CALL Text(const char* value) { if(value)visibleTexts.emplace_back(value); }
void SKY2_CALL Empty() {}
int32_t SKY2_CALL Button(const char* id, const char*) {
    if (button != id) return 0;
    button.clear();
    return disabled == 0;
}
int32_t SKY2_CALL Select(const char* id, const char*, int32_t) {
    if (selection != id) return 0;
    selection.clear(); return 1;
}
int32_t SKY2_CALL Checkbox(const char* id, const char*, int32_t* value) {
    if (checkbox != id) return 0;
    checkbox.clear(); *value = !*value; return 1;
}
void SKY2_CALL Disable(int32_t value) { disabled += value ? 1 : 0; }
void SKY2_CALL Enable() { disabled = 0; }
int32_t SKY2_CALL Begin(const char* id, float height) {
    if(std::string(id)=="roster.list")lastListHeight=height;
    return 1;
}
void SKY2_CALL ContentSize(float* width,float* height) {
    ++viewportQueries;*width=1000.0f;*height=viewportHeight;
}
int32_t SKY2_CALL Register(void*, const Sky2Action* value) { actions.push_back(*value); return 1; }
void SKY2_CALL Open(void*) { ++opens; }
int32_t SKY2_CALL LanguageNow() { return language; }
void SKY2_CALL Section(const char* title,const char* description) { ++extendedSections; Text(title); Text(description); }
void SKY2_CALL Card(const char*) { ++cardDepth; }
void SKY2_CALL EndCard() { --cardDepth; }
int32_t SKY2_CALL Columns(const char*,float) { ++columnDepth; return layoutColumns; }
void SKY2_CALL EndColumns() { --columnDepth; }
int32_t SKY2_CALL Tab(const char* id,const char* label,int32_t) { return Button(id,label); }
void SKY2_CALL Status(const char* value,int32_t) { Text(value); }
void SKY2_CALL Progress(float,const char* value) { Text(value); }
int32_t SKY2_CALL Disclosure(const char*,const char* label,int32_t) { Text(label); return 0; }
int32_t SKY2_CALL TabBar(const char* id,const char* const* labels,int32_t count,int32_t selected) {
    Check(std::string(id)=="party.tab"&&count==2&&labels,"top bar keeps the stable Party page order");
    ++tabBarCalls; observedPage=selected;
    // 模拟宿主整栏返回值，而非逐标签变更当前页面；保留旧 ID 供既有测试路由。
    if(requestedPage>=0){const int value=requestedPage;requestedPage=-1;return value;}
    constexpr const char* ids[]{"party.tab.settings","party.tab.roster"};
    for(int index=0;index<count;++index)if(button==ids[index]){button.clear();return index;}
    return selected;
}
Sky2UiApi ui{};
Sky2HostApi host{};
Sky2Frame frame{sizeof(Sky2Frame), 1920, 1080, 1, 1000, 1, 1, 1, 0};
void InitializeFakeHost() {
    ui.size = sizeof(ui); ui.text = ui.text_wrapped = &Text; ui.separator = &Empty;
    ui.button = &Button; ui.checkbox = &Checkbox; ui.selectable = &Select;
    ui.begin_disabled = &Disable; ui.end_disabled = &Enable; ui.begin_child = &Begin; ui.end_child = &Empty;
    host.size = sizeof(host); host.abi = SKY2_HUB_ABI; host.ui = &ui; host.owner = &host;
    host.register_action = &Register; host.open_page = &Open; host.language = &LanguageNow;
    Sky2Hub_Host = &host;
}
void Reset() {
    HubVisibilityChanged(0);
    fixture = {};
    fixture.appliedFeatures = fixture.requestedFeatures = kAllFeatures;
    fixture.roster.ready = fixture.roster.canEdit = fixture.roster.allowUnjoined = true;
    fixture.roster.generation = 1;
    fixture.roster.pendingId = kNoRosterId;
    for (size_t i = 0; i < fixture.roster.members.size(); ++i) {
        fixture.roster.members[i].id = kRosterDefinitions[i].id;
        fixture.roster.members[i].canAdd = fixture.roster.members[i].initialized = true;
    }
    calls = requests = 0; joined = kNoRosterId;
    frame.size=sizeof(frame);frame.header_drawn=0;
    frame.time_ms = 1000; frame.foreground = frame.page_active = frame.panel_open = 1;
    button="party.tab.roster"; checkbox.clear(); selection = "role.0";
    DrawHubPanel(&frame);
}
void ShowTab(const char* id) { button=id; DrawHubPanel(&frame); }
void Confirm() { ShowTab("party.tab.roster"); button = "role.confirm"; DrawHubPanel(&frame); }
}
namespace sky2party {
ControlSnapshot ReadControlSnapshot() noexcept { return fixture; }
void RequestFeatureMask(uint32_t mask) noexcept { fixture.requestedFeatures = mask; ++requests; }
bool QueueAddMember(uint32_t id) noexcept { joined = id; ++calls; return true; }
const char* CharacterNameFor(Language, uint32_t) noexcept { return nullptr; }
const char* GameTermFor(Language, GameTerm) noexcept { return nullptr; }
bool GameNamesReady(Language) noexcept { return false; }
const char* RosterResultText(RosterResult) noexcept { return "result"; }
const char* RosterBlockReasonText(RosterBlockReason) noexcept { return "blocked"; }
void Log(const char*) noexcept {}
}
#ifndef SKY2_MODULE_VISUAL_FIXTURE
int main() {
    InitializeFakeHost();
    Check(sky2party::RegisterHubActions() && actions.size() == 6, "six namespaced actions registered");
    Reset();Check(actions[1].state_now&&actions[1].state_now(actions[1].user)==1,"favorite toggle reports applied state");
    fixture.requestedFeatures&=~FeatureFixedMembers;fixture.waitingForSafeState=true;
    Check(actions[1].state_now(actions[1].user)==2,"favorite toggle reports pending only while a request is waiting");
    fixture.waitingForSafeState=false;
    Check(actions[1].state_now(actions[1].user)==1,"failed request with known rollback displays actual applied state");
    fixture.appliedStateKnown=false;
    Check(actions[1].state_now(actions[1].user)==-1,"unknown native state cannot be shown as an enabled checkbox");Reset();
    Reset();
    actions.front().invoke(actions.front().user);
    actions.back().invoke(actions.back().user);
    Check(opens == 2 && calls == 0 && requests == 0, "page and roster shortcuts never write game state");
    Check((actions.back().flags & SKY2_ACTION_CONFIRM) != 0, "roster action announces confirmation boundary");
    Confirm(); Check(calls == 0, "first confirmation does not enqueue");
    Confirm(); Check(calls == 1 && joined == 0, "second confirmation enqueues exactly selected character");
    Reset(); Confirm(); sky2party::HubVisibilityChanged(0); Confirm();
    Check(calls == 0, "switching or closing page cancels pending confirmation");
    Reset(); Confirm(); frame.foreground = 0; sky2party::TickHubPanel(&frame); frame.foreground = 1; Confirm();
    Check(calls == 0, "losing focus cancels pending confirmation");
    Reset(); Confirm(); ++fixture.roster.generation; Confirm();
    Check(calls == 0, "scene generation invalidates confirmation");
    Reset(); Confirm(); frame.time_ms += 8001; Confirm();
    Check(calls == 0, "confirmation expires after eight seconds");
    Reset(); Confirm(); selection = "role.1"; DrawHubPanel(&frame); Confirm();
    Check(calls == 0, "changing character requires two new confirmations");
    Confirm(); Check(calls == 1 && joined == 1, "changed selection cannot join old character");
    Reset(); fixture.roster.canEdit = false; Confirm(); Confirm();
    Check(calls == 0, "unsafe snapshot disables character execution");
    Reset(); fixture.appliedStateKnown = false; Confirm(); Confirm();
    Check(calls == 0, "unknown patch state disables character execution");
    Reset(); Confirm(); button="party.tab.settings"; checkbox = "feature.0"; DrawHubPanel(&frame); Confirm();
    Check(calls == 0 && requests == 1 && !(fixture.requestedFeatures & FeatureFixedMembers),
        "feature UI only requests a mask and cancels character confirmation");
    Reset(); actions[1].invoke(actions[1].user);
    Check(requests == 1 && !(fixture.requestedFeatures & FeatureFixedMembers), "global fixed toggle retains game-thread guard");
    Reset(); Confirm(); ShowTab("party.tab.settings"); Confirm();
    Check(calls==0,"switching the inner page cancels pending confirmation");
    Reset(); actions.front().invoke(actions.front().user); button="role.confirm"; DrawHubPanel(&frame);
    Check(button=="role.confirm"&&calls==0,"party.open opens feature settings without exposing a roster write");
    actions.back().invoke(actions.back().user); DrawHubPanel(&frame);
    Check(button.empty()&&calls==0,"party.roster navigates directly and only first-arms the selected role");
    language = 1; sky2party::TickHubPanel(&frame);
    Check(sky2party::CurrentLanguage() == sky2party::Language::TraditionalChinese, "host traditional Chinese maps to Party enum");
    language = 2; sky2party::TickHubPanel(&frame);
    Check(sky2party::CurrentLanguage() == sky2party::Language::Japanese, "host Japanese maps to Party enum");
    // 旧 ABI 的尾部完全不可访问时，SDK 应退为纵向布局，且仍保留完整确认流程。
    ui.size = offsetof(Sky2UiApi, section); Reset(); Confirm(); Confirm();
    Check(calls == 1, "old UI prefix retains safe character confirmation");
    ui.size = sizeof(ui); ui.section=&Section; ui.begin_card=&Card; ui.end_card=&EndCard;
    ui.columns=&Columns; ui.next_column=&Empty; ui.end_columns=&EndColumns; ui.tab=&Tab;
    ui.status=&Status; ui.progress=&Progress; ui.disclosure=&Disclosure;
    Reset(); Confirm(); Check(calls==0,"styled page first press remains non-mutating");
    Confirm(); Check(calls==1,"styled page second press uses the existing request queue");
    Check(cardDepth==0&&columnDepth==0&&extendedSections>0,"styled card and column scopes are balanced");
    std::set<std::string> headings;
    for(language=0;language<8;++language){
        visibleTexts.clear(); layoutColumns=language%2 ? 1 : 2; frame.width=language%2 ? 1100.0f : 1920.0f; ShowTab("party.tab.settings");
        Check(!visibleTexts.empty(),"every language has a settings heading");
        if(!visibleTexts.empty())headings.insert(visibleTexts.front());
        Check(cardDepth==0&&columnDepth==0,"both responsive layouts close all scopes");
    }
    Check(headings.size()==8,"new settings heading has eight explicit translations");
    Reset(); fixture.roster.pendingId=1; Confirm(); Confirm();
    Check(calls==0,"pending roster request remains disabled in styled page");
    Reset(); ShowTab("party.tab.settings"); frame.foreground=0; visibleTexts.clear(); checkbox="feature.0"; DrawHubPanel(&frame);
    Check(!visibleTexts.empty()&&requests==0,"background settings remain visible without feature writes");
    Reset(); frame.foreground=0; visibleTexts.clear(); Confirm(); Confirm();
    Check(!visibleTexts.empty()&&calls==0,"background roster remains visible without roster writes");
    ui.tab_bar=&TabBar;
    Reset(); tabBarCalls=0; DrawHubPanel(&frame);
    Check(tabBarCalls==1,"one atomic top bar call per visible frame");
    Confirm(); requestedPage=0; DrawHubPanel(&frame); requestedPage=1; DrawHubPanel(&frame);
    Confirm(); Check(calls==0,"trigger-driven inner page changes cancel the prior confirmation");
    Confirm(); Check(calls==1,"returning to roster still requires two new confirmations");
    Reset(); Confirm(); frame.foreground=0; requestedPage=0; DrawHubPanel(&frame);
    frame.foreground=1; button="role.confirm"; DrawHubPanel(&frame);
    Check(observedPage==1&&button.empty()&&calls==0,"background tab result is ignored and confirmation is canceled");
    requestedPage=99; DrawHubPanel(&frame); DrawHubPanel(&frame);
    Check(observedPage==1&&calls==0,"invalid host tab result cannot select an unknown Party page");
    // 旧布局函数表没有 tab_bar 字段，保留原按钮回退和相同的二次确认边界。
    ui.size=offsetof(Sky2UiApi,tab_bar); tabBarCalls=0; Reset(); Confirm(); Confirm();
    Check(tabBarCalls==0&&calls==1,"old layout prefix ignores the unavailable tab bar tail");
    // 新宿主先画 Header，再把本帧标记传给 Main；页签只绘制一次，而且切页
    // 必须立刻取消武装，不能等下一帧 Tick 才阻止第二次确认提交。
    ui.size=sizeof(ui);Reset();Confirm();tabBarCalls=0;requestedPage=0;
    DrawHubHeader(&frame);frame.header_drawn=1;DrawHubPanel(&frame);
    Check(tabBarCalls==1&&observedPage==1,"fixed header is not duplicated inside Main");
    requestedPage=1;DrawHubHeader(&frame);button="role.confirm";DrawHubPanel(&frame);
    Check(calls==0&&button.empty(),"fixed header tab switch cancels armed roster confirmation");
    button="role.confirm";DrawHubPanel(&frame);
    Check(calls==1,"fixed header path retains two independent confirmation presses");
    // 旧帧内存的尾部故意放非零标记：size 不含字段时必须忽略，并继续画页签。
    frame.size=offsetof(Sky2Frame,header_drawn);tabBarCalls=0;DrawHubPanel(&frame);
    Check(tabBarCalls==1,"original frame prefix cannot accidentally suppress inline tabs");
    frame.size=sizeof(frame);frame.header_drawn=0;ui.content_size=&ContentSize;
    layoutColumns=2;frame.scale=1;viewportHeight=700;Reset();const float wideList=lastListHeight;
    viewportHeight=900;DrawHubPanel(&frame);
    Check(lastListHeight>wideList&&lastListHeight<viewportHeight,"wide roster expands with Main viewport height");
    layoutColumns=1;DrawHubPanel(&frame);
    Check(lastListHeight<wideList&&lastListHeight>100,"narrow roster reserves room for selected-role actions");
    layoutColumns=2;frame.scale=2;viewportHeight=1000;DrawHubPanel(&frame);
    Check(lastListHeight<viewportHeight,"physical viewport height is not scaled twice");
    viewportHeight=0;DrawHubPanel(&frame);
    Check(lastListHeight==520,"invalid viewport size uses the original scaled fallback");
    ui.size=offsetof(Sky2UiApi,content_size);const int queries=viewportQueries;viewportHeight=900;
    DrawHubPanel(&frame);
    Check(viewportQueries==queries&&lastListHeight==520,"old UI prefix never reads optional viewport callback");
    std::printf("Party Hub panel tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
#else
namespace {
bool armPreview=false;
void SKY2_CALL DrawPreview(const Sky2Frame* current){
    if(armPreview&&current&&current->foreground&&current->panel_open&&current->page_active){
        // 公共截图器先渲染关闭帧以重置宿主焦点，该帧会正确取消旧确认。
        // 因此只在首个可见帧用既有按钮替身完成第一次确认，随后由真正的
        // 宿主函数表绘制；不直接改 confirmation 内部状态或伪造前台标记。
        armPreview=false;const auto* realUi=host.ui;host.ui=&ui;frame=*current;
        frame.header_drawn=0;Confirm();host.ui=realUi;
        Check(calls==0,"preview confirmation must not enqueue");
    }
    DrawHubPanel(current);
}
}
// 截图目标复用生产页面和上述快照替身；唯一替换的是显示函数表，由公共
// Capture 使用真正的宿主布局/字体渲染。不会装载游戏业务模块或修改存档。
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    const std::filesystem::path output=argv[1];std::filesystem::create_directories(output);
    InitializeFakeHost();
    for(int scenario=0;scenario<3;++scenario){
        // 上一次 Capture 的 ImGui 已销毁。先恢复无图形替身，再准备选中状态
        // 和第一次确认，避免准备快照时误用已经结束生命周期的显示上下文。
        host.ui=&ui;language=scenario==2?3:0;Reset();
        fixture.message=ControlMessage::Applied;fixture.waitingForSafeState=false;
        for(size_t index=0;index<fixture.roster.members.size();++index){
            auto& member=fixture.roster.members[index];member.level=48+static_cast<uint32_t>(index);
            member.inParty=index<4;member.canAdd=index>=4;member.needsPreparation=index>=9;
        }
        selection="role.101";DrawHubPanel(&frame);
        if(scenario==0)ShowTab("party.tab.settings");
        armPreview=scenario==1;
        const auto path=output/(scenario==0?L"party-normal.png":scenario==1?L"party-confirm.png":L"party-narrow-en.png");
        sky2hub::visual::Capture(path.c_str(),"party",scenario==2?"Party Editor":"队伍编辑",host,
            &DrawPreview,&TickHubPanel,scenario==2?1100:1600,1000,nullptr,5,&DrawHubHeader);
    }
    return failures?1:0;
}
#endif
