// 使用三份真正独立链接的合作输入实现验证原始样本观察、最外隔离、View补发
// 及线程隔离，不挂钩系统函数，不产生实际键盘/鼠标/手柄输入。
#include "input.h"
#include <cstdlib>
#include <iostream>
#include <thread>
using Next=void(*)(XINPUT_GAMEPAD*);
using Observe=unsigned(*)(Next,XINPUT_GAMEPAD*,unsigned);
namespace {
Observe inner=nullptr;
unsigned innerSeen=0;
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
void Nested(XINPUT_GAMEPAD* pad){innerSeen=inner(nullptr,pad,1);}
}
int wmain(int argc,wchar_t** argv){
    Check(argc==3,"two independently linked fixtures required");
    HMODULE a=LoadLibraryW(argv[1]),b=LoadLibraryW(argv[2]);Check(a&&b,"load fixtures");
    auto outer=reinterpret_cast<Observe>(GetProcAddress(a,"Observe"));inner=reinterpret_cast<Observe>(GetProcAddress(b,"Observe"));
    auto claim=reinterpret_cast<bool(*)(unsigned)>(GetProcAddress(a,"Claim"));
    auto owner=reinterpret_cast<unsigned(*)()>(GetProcAddress(b,"Owner"));Check(outer&&inner&&claim&&owner,"fixture exports");
    sky2solo::InputLease root(0x1111);Check(root.Claim()&&owner()==0x1111,"owner is shared across all DLL copies");
    Check(claim(0x2222)&&!root.Owns()&&owner()==0x2222,"new panel atomically takes ownership");
    root.Release();Check(owner()==0x2222,"old panel release cannot clear newer owner");
    sky2solo::InputLease(0x2222).Release();Check(owner()==0,"owner released");
    XINPUT_GAMEPAD sample{};sample.wButtons=XINPUT_GAMEPAD_A;sample.sThumbRY=20000;
    const auto observed=outer(&Nested,&sample,0);
    Check(observed==XINPUT_GAMEPAD_A&&innerSeen==XINPUT_GAMEPAD_A,"inner capture cannot hide raw input from outer observer");
    Check(!sample.wButtons&&!sample.sThumbRY,"outermost observer applies inner modal capture");
    sample.wButtons=XINPUT_GAMEPAD_A;outer(nullptr,&sample,0);Check(sample.wButtons==XINPUT_GAMEPAD_A,"capture request does not leak into next call");
    sample={};outer(nullptr,&sample,2);Check(sample.wButtons==XINPUT_GAMEPAD_BACK,"single View replay reaches final game result");
    sample.wButtons=XINPUT_GAMEPAD_BACK;outer(nullptr,&sample,4);Check(!sample.wButtons,"View prefix is delayed");
    sample={};outer(nullptr,&sample,3);Check(!sample.wButtons,"modal capture dominates View replay");
    { sky2solo::GamepadCall rootCall;rootCall.RequestCapture(true);bool separate=false;
      std::thread worker([&]{sky2solo::GamepadCall other;separate=other.Outermost()&&!other.CaptureRequested();});worker.join();
      Check(separate&&rootCall.CaptureRequested(),"capture remains confined to the active polling thread"); }
    {sky2solo::PresentInstallGuard guard;Check(bool(guard),"shared Present installation lock available");}
    Check(!sky2solo::IsPhysicalVirtualKey(7)&&sky2solo::IsPhysicalVirtualKey('A'),"reserved VK cannot keep a release tail armed");
    Check(sky2solo::FrameHealthy(100,600)&&!sky2solo::FrameHealthy(100,601),"500 ms fail-open frame boundary");
    // fixture映射故意随进程保留；释放DLL仅发生在所有同步调用与线程结束以后。
    FreeLibrary(b);FreeLibrary(a);std::cout<<"cross-DLL input protocol passed\n";
}
