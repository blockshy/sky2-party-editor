// 隔离诊断宿主：复用宝箱项目公开测试的 XInput ordinal 2/3 导入方法。
// 必须由真正的 Ultimate ASI Loader 完成首次插件初始化；宿主只在已看到
// 不支持 EXE 的拒绝日志后测试重复入口。整个程序不查找游戏进程或存档。
#include <Windows.h>
#include <Xinput.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" __declspec(dllimport) DWORD WINAPI PartyHostGetState(DWORD, XINPUT_STATE*);
extern "C" __declspec(dllimport) DWORD WINAPI PartyHostSetState(DWORD, XINPUT_VIBRATION*);

namespace {
constexpr char kPartyReject[] = "Unsupported executable; no game hooks installed.";
constexpr char kPartyStart[] = "Runtime initialization started.";
constexpr char kChestReject[] = "Unsupported executable; all hooks skipped.";

bool Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s (win32=%lu)\n", message, GetLastError());
    return false;
}

std::string ReadLog(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

size_t Count(const std::string& content, const char* needle) {
    size_t count = 0, position = 0;
    const size_t length = std::char_traits<char>::length(needle);
    while ((position = content.find(needle, position)) != std::string::npos) {
        ++count;
        position += length;
    }
    return count;
}

void DriveLoader() {
    // UAL 会通过宿主的 Win32 IAT 调用完成延迟加载；不自行 LoadLibrary 插件，
    // 也不调用插件的首次 InitializeASI 来掩盖 Loader 没有实际工作的问题。
    FILETIME timestamp{};
    GetSystemTimeAsFileTime(&timestamp);
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    Sleep(20);
}

bool CheckForwarding(HMODULE loader) {
    wchar_t systemDirectory[MAX_PATH]{};
    if (!GetSystemDirectoryW(systemDirectory, MAX_PATH)) return Fail("System32 path unavailable");
    const auto systemPath = std::wstring(systemDirectory) + L"\\xinput1_4.dll";
    const auto system = LoadLibraryExW(systemPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!system || system == loader) return Fail("System32 reference is not a separate module");
    using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using SetState = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    const auto getState = reinterpret_cast<GetState>(GetProcAddress(system, "XInputGetState"));
    const auto setState = reinterpret_cast<SetState>(GetProcAddress(system, "XInputSetState"));
    if (!getState || !setState) return Fail("System32 XInput exports missing");
    // 越界索引不对应真实手柄，SetState 同时传零震动；不消费按键或启停输入。
    XINPUT_STATE reference{}, actual{};
    XINPUT_VIBRATION vibration{};
    const auto expectedGet = getState(XUSER_MAX_COUNT, &reference);
    const auto expectedSet = setState(XUSER_MAX_COUNT, &vibration);
    const auto actualGet = PartyHostGetState(XUSER_MAX_COUNT, &actual);
    const auto actualSet = PartyHostSetState(XUSER_MAX_COUNT, &vibration);
    FreeLibrary(system);
    if (expectedGet != actualGet || expectedSet != actualSet)
        return Fail("XInput ordinal forwarding mismatch");
    std::printf("PASS: IAT ordinal 2/3 forwarding matches System32 (%lu / %lu)\n", actualGet, actualSet);
    return true;
}

bool RepeatEntry(const wchar_t* name) {
    const auto module = GetModuleHandleW(name);
    using Initialize = void(*)();
    const auto initialize = module ? reinterpret_cast<Initialize>(GetProcAddress(module, "InitializeASI")) : nullptr;
    if (!initialize) return Fail("UAL did not load an expected InitializeASI module");
    for (int i = 0; i < 8; ++i) initialize();
    return true;
}
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    bool party = false, chest = false, duplicate = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg(argv[i]);
        if (arg == L"--party") party = true;
        else if (arg == L"--chest") chest = true;
        else if (arg == L"--duplicate-party") duplicate = true;
        else { Fail("unknown host argument"); return 2; }
    }
    if ((!party && !chest) || (duplicate && !party)) return 2;
    wchar_t executable[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (!length || length >= MAX_PATH) return 3;
    std::wstring root(executable, length);
    root.resize(root.find_last_of(L"\\/"));
    const auto partyLog = root + L"\\plugins\\Sky2PartyEditor\\party.log";
    const auto chestLog = root + L"\\plugins\\Sky2ChestTracker\\tracker.log";
    const auto loader = GetModuleHandleW(L"xinput1_4.dll");
    if (!loader) { Fail("official UAL was not imported"); return 3; }

    // 首次必须由 UAL 调用入口并产生日志。只看到模块映射成功不算初始化成功。
    const auto deadline = GetTickCount64() + 10000;
    bool ready = false;
    do {
        DriveLoader();
        ready = (!party || (GetModuleHandleW(L"Sky2PartyEditor.asi") && Count(ReadLog(partyLog), kPartyReject) == 1)) &&
                (!chest || (GetModuleHandleW(L"Sky2ChestTracker.asi") && Count(ReadLog(chestLog), kChestReject) == 1)) &&
                (!duplicate || GetModuleHandleW(L"Sky2PartyEditorDuplicate.asi"));
    } while (!ready && GetTickCount64() < deadline);
    if (!ready) { Fail("UAL initialization/rejection deadline exceeded"); return 4; }
    if (!CheckForwarding(loader)) return 5;
    if (party && !RepeatEntry(L"Sky2PartyEditor.asi")) return 6;
    if (chest && !RepeatEntry(L"Sky2ChestTracker.asi")) return 6;
    if (duplicate) {
        if (GetModuleHandleW(L"Sky2PartyEditor.asi") == GetModuleHandleW(L"Sky2PartyEditorDuplicate.asi")) {
            Fail("duplicate copy did not map as a separate module"); return 6;
        }
        if (!RepeatEntry(L"Sky2PartyEditorDuplicate.asi")) return 6;
        std::puts("PASS: duplicate party binaries are separately mapped");
    }
    // 持续观察而非只立即检查，给被错误重复启动的异步工作线程留下执行窗口。
    const auto settleDeadline = GetTickCount64() + 1000;
    do {
        if (party) {
            const auto log = ReadLog(partyLog);
            if (Count(log, kPartyStart) != 1 || Count(log, kPartyReject) != 1 ||
                log.find("assistance active") != std::string::npos ||
                log.find("hook installation failed") != std::string::npos) {
                Fail("party initialized more than once or passed fake-host rejection"); return 7;
            }
        }
        if (chest && Count(ReadLog(chestLog), kChestReject) != 1) {
            Fail("chest rejection count changed after repeated InitializeASI"); return 7;
        }
        Sleep(20);
    } while (GetTickCount64() < settleDeadline);
    std::puts("PASS: UAL first initialization and repeated-entry guards; fake host rejected");
    // 插件按进程生命周期工作，不尝试 FreeLibrary 热卸载；退出销毁整个隔离进程。
    return 0;
}
