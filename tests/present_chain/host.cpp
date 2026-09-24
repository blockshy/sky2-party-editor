// 真实 DXGI Present 的双 DLL 隔离回归：隐藏窗口、WARP 软件设备，不访问真实游戏。
// race 模式通过两个工作线程及事件屏障强制 Create(A),Create(B),Enable(A),Enable(B)。
// serial 模式把每个 DLL 的 Create+Enable 作为一个完整步骤，验证原生转发和两层回调。
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

static void Require(bool value,const char* message){
    if(!value){std::fprintf(stderr,"FAIL: %s (error=%lu)\n",message,GetLastError());std::exit(1);}
}
struct Plugin{
    HMODULE module=nullptr;
    int(*prepare)(void*)=nullptr;int(*enable)()=nullptr;int(*stop)()=nullptr;
    unsigned(*count)()=nullptr;void*(*identity)()=nullptr;void*(*next)()=nullptr;
    explicit Plugin(const wchar_t* name){
        module=LoadLibraryExW(name,nullptr,LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        Require(module!=nullptr,"load independent test DLL");
        prepare=reinterpret_cast<decltype(prepare)>(GetProcAddress(module,"Prepare"));
        enable=reinterpret_cast<decltype(enable)>(GetProcAddress(module,"Enable"));
        stop=reinterpret_cast<decltype(stop)>(GetProcAddress(module,"Stop"));
        count=reinterpret_cast<decltype(count)>(GetProcAddress(module,"Count"));
        identity=reinterpret_cast<decltype(identity)>(GetProcAddress(module,"LibraryIdentity"));
        next=reinterpret_cast<decltype(next)>(GetProcAddress(module,"NextAddress"));
        Require(prepare&&enable&&stop&&count&&identity&&next,"resolve isolated plugin exports");
    }
};
struct Worker{
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE go=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    int prepareResult=-1,enableResult=-1;
    std::thread thread;
    Worker(Plugin& plugin,void* target):thread([&,target]{
        prepareResult=plugin.prepare(target);SetEvent(ready);
        WaitForSingleObject(go,INFINITE);
        if(prepareResult==0)enableResult=plugin.enable();SetEvent(done);
    }){}
    void ReleaseAndWait(){
        Require(WaitForSingleObject(ready,10000)==WAIT_OBJECT_0,"wait create barrier");
        Require(prepareResult==0,"worker Create succeeded");
        SetEvent(go);Require(WaitForSingleObject(done,10000)==WAIT_OBJECT_0,"wait enable completion");
        Require(enableResult==0,"worker Enable succeeded");
    }
    ~Worker(){thread.join();CloseHandle(ready);CloseHandle(go);CloseHandle(done);}
};
int main(int argc,char** argv){
    Require(argc==2,"mode: race-ab/race-ba/serial-ab/serial-ba/ready-ab/ready-ba");
    const bool race=std::strncmp(argv[1],"race-",5)==0;
    const bool readyProtocol=std::strncmp(argv[1],"ready-",6)==0;
    const bool reverse=std::strstr(argv[1],"-ba")!=nullptr;
    Require(race||readyProtocol||std::strncmp(argv[1],"serial-",7)==0,"known mode");
    const auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=DefWindowProcW;cls.lpszClassName=L"PresentChainReviewHidden";
    Require(RegisterClassW(&cls)!=0,"register hidden window");
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"",WS_OVERLAPPED,0,0,128,128,nullptr,nullptr,instance,nullptr);
    Require(window!=nullptr,"create hidden window");
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=128;desc.BufferDesc.Height=128;
    desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=window;
    desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* swap=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    const auto made=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
        &desc,&swap,&device,nullptr,&context);
    Require(SUCCEEDED(made),"create real WARP swap chain");
    void* target=(*reinterpret_cast<void***>(swap))[8];
    unsigned char original[16]{};std::memcpy(original,target,sizeof(original));
    Plugin a(L"HookA.dll"),b(L"HookB.dll");
    Require(a.identity()!=b.identity(),"distinct statically linked MinHook copies");
    Plugin& first=reverse?b:a;Plugin& second=reverse?a:b;
    if(race){
        Worker wa(a,target),wb(b,target);
        HANDLE ready[]={wa.ready,wb.ready};
        Require(WaitForMultipleObjects(2,ready,TRUE,10000)==WAIT_OBJECT_0,"both creates before any enable");
        if(reverse){wb.ReleaseAndWait();wa.ReleaseAndWait();}
        else{wa.ReleaseAndWait();wb.ReleaseAndWait();}
    }else if(readyProtocol){
        // 模拟生产修复协议：后加入者必须在 Create 之前等待先加入者 Enable 完成。
        // 用事件表示“可证实的就绪状态”，不靠固定睡眠猜测初始化快慢。
        HANDLE waiting=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        int firstResult=-1,secondResult=-1;
        std::thread later([&]{
            SetEvent(waiting);
            Require(WaitForSingleObject(ready,10000)==WAIT_OBJECT_0,"later DLL waits readiness before Create");
            secondResult=second.prepare(target);
            if(secondResult==0)secondResult=second.enable();
        });
        std::thread earlier([&]{
            Require(WaitForSingleObject(waiting,10000)==WAIT_OBJECT_0,"later initializer already waiting");
            firstResult=first.prepare(target);
            if(firstResult==0)firstResult=first.enable();
            Require(firstResult==0,"earlier DLL complete Create and Enable before readiness");
            SetEvent(ready);
        });
        earlier.join();later.join();CloseHandle(ready);CloseHandle(waiting);
        Require(firstResult==0&&secondResult==0,"ready protocol two hook installs succeed");
    }else{
        Require(first.prepare(target)==0&&first.enable()==0,"serial first complete hook");
        Require(second.prepare(target)==0&&second.enable()==0,"serial second complete hook");
    }
    HRESULT result=S_OK;
    for(unsigned i=0;i<3;++i){
        result=swap->Present(0,0);
        Require(SUCCEEDED(result),"real Present forwards successfully");
    }
    std::printf("mode=%s A=%u B=%u Present=0x%08lx independentMinHook=true\n",argv[1],a.count(),b.count(),static_cast<unsigned long>(result));
    if(race){
        Require(first.count()==0&&second.count()==3,"stale trampoline reproducibly skips first DLL");
    }else{
        Require(first.count()==3&&second.count()==3,"serial creation invokes both DLL callbacks");
    }
    // 逆序移除后比较原字节并再调用原生 Present，防止只证明回调却留下损坏入口。
    Require(second.stop()==0&&first.stop()==0,"reverse hook teardown");
    Require(std::memcmp(original,target,sizeof(original))==0,"original Present entry restored");
    Require(SUCCEEDED(swap->Present(0,0)),"native Present still works after cleanup");
    FreeLibrary(b.module);FreeLibrary(a.module);
    context->Release();device->Release();swap->Release();
    DestroyWindow(window);UnregisterClassW(cls.lpszClassName,instance);
    std::puts("PASS: expected chain result, real Present, original byte restoration; no game access.");
}
