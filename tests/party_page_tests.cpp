// 用显式 UI 函数表替身运行独立队伍业务页，不依赖 ImGui、游戏或真实存档。
// 检查的重点是跨页面确认生命周期和命令边界，而非控件是否被逐行绘制。
#include "party_page.h"
#include "control_state.h"
#include "game_names.h"
#include <cstdio>
#include <cstddef>
#include <set>
#include <string>
#include <vector>


namespace {
using namespace sky2party;
ControlSnapshot fixture;
std::string button, selection, checkbox;
int calls = 0, requests = 0, failures = 0, disabled = 0, language = 0;
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
    // 模拟绘制后端整栏返回值，而非逐标签变更当前页面；保留旧 ID 供既有测试路由。
    if(requestedPage>=0){const int value=requestedPage;requestedPage=-1;return value;}
    constexpr const char* ids[]{"party.tab.settings","party.tab.roster"};
    for(int index=0;index<count;++index)if(button==ids[index]){button.clear();return index;}
    return selected;
}
Sky2UiApi ui{};
Sky2Frame frame{sizeof(Sky2Frame), 1920, 1080, 1, 1000, 1, 1, 1, 0};
void InitializeFakeUi() {
    ui.size = sizeof(ui); ui.text = ui.text_wrapped = &Text; ui.separator = &Empty;
    ui.button = &Button; ui.checkbox = &Checkbox; ui.selectable = &Select;
    ui.begin_disabled = &Disable; ui.end_disabled = &Enable; ui.begin_child = &Begin; ui.end_child = &Empty;

}
void DrawPage() {
    // 语言由独立窗口在绘制前设置；替身按同一八语顺序模拟本地游戏语言。
    constexpr Language values[]{Language::Chinese,Language::TraditionalChinese,Language::Japanese,
        Language::English,Language::German,Language::French,Language::Spanish,Language::Korean};
    SetDisplayLanguage(values[language >= 0 && language < 8 ? language : 0]); DrawPartyPage(ui, frame);
}
void Reset() {
    PartyPageVisibilityChanged(false);
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
    DrawPage();
}
void ShowTab(const char* id) { button=id; DrawPage(); }
void Confirm() { ShowTab("party.tab.roster"); button = "role.confirm"; DrawPage(); }
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
int main() {
    InitializeFakeUi(); Reset();
    Confirm(); Check(calls == 0, "first confirmation does not enqueue");
    Confirm(); Check(calls == 1 && joined == 0, "second confirmation enqueues exactly selected character");
    Reset(); Confirm(); sky2party::PartyPageVisibilityChanged(false); Confirm();
    Check(calls == 0, "switching or closing page cancels pending confirmation");
    Reset(); Confirm(); frame.foreground = 0; sky2party::TickPartyPage(frame); frame.foreground = 1; Confirm();
    Check(calls == 0, "losing focus cancels pending confirmation");
    Reset(); Confirm(); ++fixture.roster.generation; Confirm();
    Check(calls == 0, "scene generation invalidates confirmation");
    Reset(); Confirm(); frame.time_ms += 8001; Confirm();
    Check(calls == 0, "confirmation expires after eight seconds");
    Reset(); Confirm(); selection = "role.1"; DrawPage(); Confirm();
    Check(calls == 0, "changing character requires two new confirmations");
    Confirm(); Check(calls == 1 && joined == 1, "changed selection cannot join old character");
    Reset(); fixture.roster.canEdit = false; Confirm(); Confirm();
    Check(calls == 0, "unsafe snapshot disables character execution");
    Reset(); fixture.appliedStateKnown = false; Confirm(); Confirm();
    Check(calls == 0, "unknown patch state disables character execution");
    Reset(); Confirm(); button="party.tab.settings"; checkbox = "feature.0"; DrawPage(); Confirm();
    Check(calls == 0 && requests == 1 && !(fixture.requestedFeatures & FeatureFixedMembers),
        "feature UI only requests a mask and cancels character confirmation");
    Reset(); Confirm(); ShowTab("party.tab.settings"); Confirm();
    Check(calls==0,"switching the inner page cancels pending confirmation");
    // 精简 UI 函数表没有布局扩展时，帮助函数应退为纵向布局，且仍保留完整确认流程。
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
    Reset(); ShowTab("party.tab.settings"); frame.foreground=0; visibleTexts.clear(); checkbox="feature.0"; DrawPage();
    Check(!visibleTexts.empty()&&requests==0,"background settings remain visible without feature writes");
    Reset(); frame.foreground=0; visibleTexts.clear(); Confirm(); Confirm();
    Check(!visibleTexts.empty()&&calls==0,"background roster remains visible without roster writes");
    ui.tab_bar=&TabBar;
    Reset(); tabBarCalls=0; DrawPage();
    Check(tabBarCalls==1,"one atomic top bar call per visible frame");
    Confirm(); requestedPage=0; DrawPage(); requestedPage=1; DrawPage();
    Confirm(); Check(calls==0,"trigger-driven inner page changes cancel the prior confirmation");
    Confirm(); Check(calls==1,"returning to roster still requires two new confirmations");
    Reset(); Confirm(); frame.foreground=0; requestedPage=0; DrawPage();
    frame.foreground=1; button="role.confirm"; DrawPage();
    Check(observedPage==1&&button.empty()&&calls==0,"background tab result is ignored and confirmation is canceled");
    requestedPage=99; DrawPage(); DrawPage();
    Check(observedPage==1&&calls==0,"invalid backend tab result cannot select an unknown Party page");
    // 旧布局函数表没有 tab_bar 字段，保留原按钮回退和相同的二次确认边界。
    ui.size=offsetof(Sky2UiApi,tab_bar); tabBarCalls=0; Reset(); Confirm(); Confirm();
    Check(tabBarCalls==0&&calls==1,"old layout prefix ignores the unavailable tab bar tail");
    // 外壳先画 Header，再把本帧标记传给 Main；页签只绘制一次，而且切页
    // 必须立刻取消武装，不能等下一帧 Tick 才阻止第二次确认提交。
    ui.size=sizeof(ui);Reset();Confirm();tabBarCalls=0;requestedPage=0;
    DrawPartyHeader(ui, frame);frame.header_drawn=1;DrawPage();
    Check(tabBarCalls==1&&observedPage==1,"fixed header is not duplicated inside Main");
    requestedPage=1;DrawPartyHeader(ui, frame);button="role.confirm";DrawPage();
    Check(calls==0&&button.empty(),"fixed header tab switch cancels armed roster confirmation");
    button="role.confirm";DrawPage();
    Check(calls==1,"fixed header path retains two independent confirmation presses");
    // 简化帧内存的尾部故意放非零标记：size 不含字段时必须忽略，并继续画页签。
    frame.size=offsetof(Sky2Frame,header_drawn);tabBarCalls=0;DrawPage();
    Check(tabBarCalls==1,"original frame prefix cannot accidentally suppress inline tabs");
    frame.size=sizeof(frame);frame.header_drawn=0;ui.content_size=&ContentSize;
    layoutColumns=2;frame.scale=1;viewportHeight=700;Reset();const float wideList=lastListHeight;
    viewportHeight=900;DrawPage();
    Check(lastListHeight>wideList&&lastListHeight<viewportHeight,"wide roster expands with Main viewport height");
    layoutColumns=1;DrawPage();
    Check(lastListHeight<wideList&&lastListHeight>100,"narrow roster reserves room for selected-role actions");
    layoutColumns=2;frame.scale=2;viewportHeight=1000;DrawPage();
    Check(lastListHeight<viewportHeight,"physical viewport height is not scaled twice");
    viewportHeight=0;DrawPage();
    Check(lastListHeight==520,"invalid viewport size uses the original scaled fallback");
    ui.size=offsetof(Sky2UiApi,content_size);const int queries=viewportQueries;viewportHeight=900;
    DrawPage();
    Check(viewportQueries==queries&&lastListHeight==520,"old UI prefix never reads optional viewport callback");
    std::printf("Party page tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
