// 每个测试 DLL 静态编入自己的 input.cpp，证明协议确实跨 DLL 共享，而不是
// 单个链接单元内的 thread_local 恰好通过测试。导出仅属于测试目标。
#include "input.h"
using Next = void (*)(XINPUT_GAMEPAD*);
extern "C" __declspec(dllexport) unsigned Observe(Next next,XINPUT_GAMEPAD* pad,unsigned requests) {
    sky2solo::GamepadCall call;
    if(next)next(pad);
    const unsigned observed=pad->wButtons;
    call.RequestCapture((requests&1)!=0);
    call.RequestViewReplay((requests&2)!=0);
    call.RequestViewSuppression((requests&4)!=0);
    call.Filter(*pad);
    return observed;
}
extern "C" __declspec(dllexport) bool Claim(unsigned token) { return sky2solo::InputLease(token).Claim(); }
extern "C" __declspec(dllexport) unsigned Owner() { return sky2solo::InputOwner(); }
