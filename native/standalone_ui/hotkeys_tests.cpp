// 真实快捷键配置、输入匹配和跨 DLL 事务回归。只创建进程私有的临时目录，
// 不安装 Mod、不运行游戏、不修改系统键盘状态；每种启动情形由 CTest 新进程执行。
#include "hotkeys.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
namespace fs=std::filesystem;
using namespace sky2solo;
namespace {
void Check(bool ok,const char* text){if(!ok){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
struct Folder {
    fs::path path=fs::current_path()/(L"hotkeys-fixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Folder(){Check(fs::create_directory(path),"create isolated fixture directory");}
    ~Folder(){std::error_code ignored;fs::remove_all(path,ignored);}
};
void Write(const fs::path& path,const std::string& text){std::ofstream output(path,std::ios::binary|std::ios::trunc);output<<text;Check(bool(output),"write fixture file");}
std::string Read(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
const HotkeyDefinition defaults[]{
    {"party.open","Party window",{VK_F8,0,XINPUT_GAMEPAD_DPAD_LEFT},true},
    {"party.action","Party action",{VK_F1,HotkeyCtrl,XINPUT_GAMEPAD_A},false}};
bool Boot(const fs::path& path){return InitializeHotkeys(0x54455354,"Test Party",path.c_str(),defaults,2);}
void InputChecks(){
    HotkeySnapshot snapshot; snapshot.count=1;snapshot.revision=1;snapshot.bindings[0]={VK_F8,0,XINPUT_GAMEPAD_DPAD_LEFT};
    HotkeyKeyboardTracker tracker;HotkeyKeyboardState state;state.down[7]=1;
    Check(!AnyHotkeyKeyboardDown(state),"reserved VK7 cannot prevent rearming");
    Check(!tracker.Update(snapshot,state,true),"first neutral sample only arms");state.down[VK_F8]=1;
    Check(tracker.Update(snapshot,state,true)==1,"new exact F8 activates");
    Check(!tracker.Update(snapshot,state,true),"held key never repeats");
    state={};tracker.Update(snapshot,state,true);state.down[VK_F8]=1;state.modifiers=HotkeyCtrl;
    Check(!tracker.Update(snapshot,state,true),"extra modifier rejects plain binding");state.modifiers=0;
    Check(!tracker.Update(snapshot,state,true),"modifier release cannot synthesize primary-key edge");
    ++snapshot.revision;Check(!tracker.Update(snapshot,state,true),"revision change blocks held old key");
    state={};tracker.Update(snapshot,state,true);state.down[VK_F8]=1;
    Check(tracker.Update(snapshot,state,true)==1,"new press after revised binding release works");
    tracker.Update(snapshot,state,false);Check(!tracker.Update(snapshot,state,true),"focus restoration waits for release");
    state={};tracker.Update(snapshot,state,true);state.down[VK_F8]=1;state.windows=true;
    Check(!tracker.Update(snapshot,state,true),"Windows-modified keys never activate");
    Check(HotkeyPadHeldMask(snapshot,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_DPAD_LEFT)==1,"exact pad chord matches");
    Check(!HotkeyPadHeldMask(snapshot,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_A),"extra digital pad button is rejected");
    Check(!HotkeyPadHeldMask(snapshot,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_UP),"diagonal pad chord is rejected");
    Check(!HotkeyPadPressedMask(snapshot,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_LEFT),"prefix after held target cannot synthesize pad edge");
    Check(HotkeyPadPressedMask(snapshot,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_BACK)==1,"new target after View activates");
}
struct Module {
    HMODULE dll;
    bool(*init)(unsigned,const wchar_t*,unsigned);
    bool(*commit)(unsigned,unsigned,unsigned,unsigned);
    bool(*reset)();HotkeySnapshot(*read)();
    bool(*validate)(unsigned,unsigned,unsigned,unsigned,char*,unsigned);
    explicit Module(const wchar_t* file):dll(LoadLibraryW(file)){
        Check(dll!=nullptr,"load independently linked engine fixture");
        init=reinterpret_cast<decltype(init)>(GetProcAddress(dll,"HkInit"));
        commit=reinterpret_cast<decltype(commit)>(GetProcAddress(dll,"HkCommit"));
        reset=reinterpret_cast<decltype(reset)>(GetProcAddress(dll,"HkReset"));
        read=reinterpret_cast<decltype(read)>(GetProcAddress(dll,"HkRead"));
        validate=reinterpret_cast<decltype(validate)>(GetProcAddress(dll,"HkValidate"));
        Check(init&&commit&&reset&&read&&validate,"fixture exports");
    }
    ~Module(){FreeLibrary(dll);}
};
void CrossDll(const wchar_t* first,const wchar_t* second){
    Folder folder;const auto aPath=folder.path/L"a",bPath=folder.path/L"b";
    fs::create_directory(aPath);fs::create_directory(bPath);
    Module a(first),b(second);
    Check(a.init(0x41414141,aPath.c_str(),0)&&a.commit(1,VK_F8,0,0),"first DLL reserves both another module's configured and default keys");
    Write(bPath/L"shortcuts.ini","[Hotkeys]\nSchema=1\nwindow.open=118,0,0\n");
    const auto beforeBoot=Read(bPath/L"shortcuts.ini");
    Check(b.init(0x42424242,bPath.c_str(),1),"conflicting startup still finds a keyboard entrance");
    Check(b.read().bindings[0].key==VK_F1&&b.read().bindings[0].modifiers==7&&b.read().bindings[0].pad==0,
        "occupied configured and default bindings use an explicit non-conflicting fallback");
    Check(Read(bPath/L"shortcuts.ini")==beforeBoot,"cross-Mod startup fallback never rewrites manual config");
    Check(a.commit(1,VK_F1,HotkeyCtrl,0)&&b.commit(0,VK_F8,0,XINPUT_GAMEPAD_DPAD_LEFT),"restore unique bindings for transaction scenarios");
    char error[512]{};
    Check(!b.validate(1,VK_F7,0,0,error,sizeof(error))&&std::string(error).find("Fixture A / Window")!=std::string::npos,
        "cross-Mod collision identifies occupying module and action");
    Check(!b.commit(1,VK_F10,0,XINPUT_GAMEPAD_DPAD_UP),"controller collision is rejected across DLLs");
    Check(!b.commit(1,VK_F8,0,0),"same-Mod duplicate is rejected");
    // 写失败必须撤销预留；随后另一个 DLL 可以取得该组合，旧生效值与版本不变。
    Check(a.commit(1,VK_F11,HotkeyCtrl,0),"seed plain config before failure");
    const auto before=a.read();
    Check(SetFileAttributesW((aPath/L"shortcuts.ini").c_str(),FILE_ATTRIBUTE_READONLY)!=FALSE,"make save target readonly");
    Check(!a.commit(1,VK_F12,HotkeyCtrl,0),"read-only target prevents commit");
    Check(a.read().revision==before.revision&&a.read().bindings[1]==before.bindings[1],"failed save does not publish");
    Check(SetFileAttributesW((aPath/L"shortcuts.ini").c_str(),FILE_ATTRIBUTE_NORMAL)!=FALSE,"restore fixture attributes");
    Check(b.commit(1,VK_F12,HotkeyCtrl,0),"failed writer releases shared candidate reservation");
    // 两个真实 DLL 同时尝试同一个空闲组合；每轮只允许一个提交成功。
    for(unsigned round=0;round<16;++round){
        Check(a.commit(1,VK_F1,HotkeyCtrl,0)&&b.commit(1,VK_F2,HotkeyCtrl,0),"restore independent trial keys");
        std::atomic<unsigned> ready{0};std::atomic<bool> start{false};bool okA=false,okB=false;
        std::thread one([&]{++ready;while(!start.load())SwitchToThread();okA=a.commit(1,VK_F10,HotkeyShift,0);});
        std::thread two([&]{++ready;while(!start.load())SwitchToThread();okB=b.commit(1,VK_F10,HotkeyShift,0);});
        while(ready.load()!=2)SwitchToThread();start=true;one.join();two.join();
        Check(okA!=okB,"exactly one cross-DLL concurrent commit wins");
    }
    Check(a.commit(0,VK_F13,0,0)&&b.commit(1,VK_F7,0,0),"another module occupies a released default key");
    const auto beforeReset=a.read();Check(!a.reset(),"restore defaults must not steal another module's key");
    Check(a.read().revision==beforeReset.revision,"failed group reset preserves old revision");
}
}
int wmain(int argc,wchar_t** argv){
    Check(argc>=2,"scenario argument required");const std::wstring scenario=argv[1];
    if(scenario==L"cross"){Check(argc==4,"two fixture paths required");CrossDll(argv[2],argv[3]);}
    else if(scenario==L"reload"){
        Check(argc==3&&Boot(argv[2]),"reinitialize saved config in a fresh process");
        Check(ReadHotkeys().bindings[0]==HotkeyBinding{VK_F12,HotkeyShift,XINPUT_GAMEPAD_Y},"saved bindings load exactly after restart");
    } else {
        Folder folder;const auto file=folder.path/L"shortcuts.ini";
        if(scenario==L"config"){
            Write(file,"; keep comment\r\n[Other]\r\nCustom=retain-me\r\n[Hotkeys]\r\nSchema=1\r\nFuture.Action=42\r\nparty.open=119,0,4\r\nparty.action=0,0,0\r\n");
            Check(Boot(folder.path),"load existing config");Check(ReadHotkeys().bindings[1].key==0,"explicit clear remains unbound");
            std::string error;Check(CommitHotkey(0,{VK_F12,HotkeyShift,XINPUT_GAMEPAD_Y},error),"save valid candidate");
            const auto bytes=Read(file);Check(bytes.find("Custom=retain-me")!=std::string::npos&&bytes.find("Future.Action=42")!=std::string::npos&&bytes.find("; keep comment")!=std::string::npos,"unknown keys sections and comments survive save");
            STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
            std::wstring command=L"\""+std::wstring(argv[0])+L"\" reload \""+folder.path.wstring()+L"\"";
            Check(CreateProcessW(argv[0],command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"start isolated reload verifier");
            Check(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0,"reload verifier finishes");DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);Check(code==0,"fresh process loads persisted values");
            Check(!CommitHotkey(0,{},error),"window keyboard cannot be cleared");
            Check(!CommitHotkey(1,{VK_F4,HotkeyAlt,0},error),"Alt+F4 refused");
            Check(!CommitHotkey(1,{VK_DELETE,HotkeyCtrl|HotkeyAlt,0},error),"secure attention combination refused");
            Check(!CommitHotkey(1,{VK_ESCAPE,0,0},error)&&!CommitHotkey(1,{VK_LWIN,0,0},error),"navigation and Windows keys refused");
            Check(!CommitHotkey(1,{VK_F1,0,XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B},error),"multiple controller buttons refused");
            Check(RestoreDefaultHotkeys(error)&&ReadHotkeys().bindings[0]==defaults[0].defaults,"restore this module as one checked group");
        } else if(scenario==L"bad"){
            Write(file,"[Hotkeys]\nSchema=1\nparty.open=0,0,0\nparty.open=121,0,0\nparty.open=122,0,0\nparty.action=119,0,4\n");const auto original=Read(file);
            Check(Boot(folder.path),"bad input falls back without failing startup");
            Check(ReadHotkeys().bindings[0]==defaults[0].defaults,"invalid window binding keeps available default");
            Check(ReadHotkeys().bindings[1]==defaults[1].defaults,"manual same-Mod duplicate falls back");
            Check(!HotkeyNotice().empty()&&Read(file)==original,"startup explains repair without overwriting original config");
        } else if(scenario==L"link"){
            Write(file,"[Hotkeys]\nSchema=1\nparty.open=122,0,4\n");const auto twin=folder.path/L"other.ini";
            Check(CreateHardLinkW(twin.c_str(),file.c_str(),nullptr)!=FALSE,"create isolated hardlink fixture");
            Check(Boot(folder.path)&&ReadHotkeys().bindings[0]==defaults[0].defaults,"linked settings use safe memory defaults");
            const auto before=ReadHotkeys();std::string error;
            Check(!CommitHotkey(0,{VK_F12,0,0},error)&&ReadHotkeys().revision==before.revision,"linked target rejects writes without publishing");
        } else if(scenario==L"input"){
            Check(Boot(folder.path)&&!fs::exists(file),"missing config uses defaults without automatic disk write");InputChecks();
            Check(!ValidHotkeyKey(7)&&!ValidHotkeyPad(XINPUT_GAMEPAD_BACK)&&!ValidHotkeyPad(XINPUT_GAMEPAD_START),"reserved inputs excluded from picker");
        } else Check(false,"unknown scenario");
    }
    std::cout<<"PASS: hotkey scenario completed\n";
}
