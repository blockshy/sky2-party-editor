// 隔离回归 DLL：每个副本分别静态链接 MinHook，故内部全局注册表不会共享。
// 只挂接测试宿主传入的 DXGI Present，不查找、不打开、不修改任何游戏进程。
#include <Windows.h>
#include <dxgi.h>
#include <MinHook.h>
#include <atomic>

using PresentFn=HRESULT(WINAPI*)(IDXGISwapChain*,UINT,UINT);
static PresentFn nextPresent=nullptr;
static void* targetPresent=nullptr;
static std::atomic<unsigned> calls{0};
static bool initialized=false,created=false,enabled=false;

static HRESULT WINAPI HookPresent(IDXGISwapChain* swap,UINT interval,UINT flags){
    calls.fetch_add(1,std::memory_order_relaxed);
    return nextPresent(swap,interval,flags);
}

// Create 与 Enable 特意拆开，以确定性屏障复现并发初始化的危险交错顺序。
extern "C" __declspec(dllexport) int Prepare(void* target){
    const auto status=MH_Initialize();
    if(status!=MH_OK)return 1000+status;
    initialized=true;targetPresent=target;
    const auto create=MH_CreateHook(target,reinterpret_cast<void*>(&HookPresent),reinterpret_cast<void**>(&nextPresent));
    created=create==MH_OK;
    return create;
}
extern "C" __declspec(dllexport) int Enable(){
    const auto status=MH_EnableHook(targetPresent);enabled=status==MH_OK;return status;
}
extern "C" __declspec(dllexport) unsigned Count(){return calls.load(std::memory_order_relaxed);}
extern "C" __declspec(dllexport) void* LibraryIdentity(){return reinterpret_cast<void*>(&MH_Initialize);}
extern "C" __declspec(dllexport) void* NextAddress(){return reinterpret_cast<void*>(nextPresent);}
extern "C" __declspec(dllexport) int Stop(){
    // 宿主按照启用顺序的逆序调用，只移除本 DLL 的目标，不使用 MH_ALL_HOOKS。
    if(enabled&&MH_DisableHook(targetPresent)!=MH_OK)return 1;
    enabled=false;
    if(created&&MH_RemoveHook(targetPresent)!=MH_OK)return 2;
    created=false;
    if(initialized&&MH_Uninitialize()!=MH_OK)return 3;
    initialized=false;return 0;
}
