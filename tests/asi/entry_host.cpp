// 双分发真实 PE 隔离测试。仅在构建目录复制产物、动态加载并观察不支持宿主的拒绝。
// 不链接 XInput，不启动游戏，不附加进程，不读取存档，不操作任何有效手柄索引。
#include <Windows.h>
#include <Xinput.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
constexpr char kStarted[] = "Runtime initialization started.";
constexpr char kRejected[] = "Unsupported executable; no game hooks installed.";

bool Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s (win32=%lu)\n", message, GetLastError());
    return false;
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
size_t Count(const std::string& text, const char* value) {
    size_t total = 0, at = 0;
    while ((at = text.find(value, at)) != std::string::npos) { ++total; at += std::char_traits<char>::length(value); }
    return total;
}
bool WaitForRejection(const std::filesystem::path& log) {
    const auto end = GetTickCount64() + 8000;
    do {
        if (Count(Read(log), kRejected) == 1) return true;
        Sleep(10);
    } while (GetTickCount64() < end);
    return Fail("runtime did not reject fake host");
}
bool StartAsi(HMODULE module) {
    using Initialize = void(*)();
    const auto initialize = module ? reinterpret_cast<Initialize>(GetProcAddress(module, "InitializeASI")) : nullptr;
    if (!initialize || GetProcAddress(module, "XInputGetState")) return Fail("ASI entry/export mismatch");
    initialize();
    return true;
}

bool VerifyForwarding(HMODULE proxy) {
    wchar_t directory[MAX_PATH]{};
    const auto length = GetSystemDirectoryW(directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return Fail("system directory unavailable");
    const auto path = std::wstring(directory, length) + L"\\xinput1_4.dll";
    const auto system = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!system || system == proxy) return Fail("forward target is not separate System32 DLL");
    const struct { const char* name; WORD ordinal; } exports[]{
        {"XInputGetState",2}, {"XInputSetState",3}, {"XInputGetCapabilities",4},
        {"XInputEnable",5}, {"XInputGetBatteryInformation",7}, {"XInputGetKeystroke",8},
        {"XInputGetAudioDeviceIds",10}};
    for (const auto& item : exports) {
        const auto named = GetProcAddress(proxy, item.name);
        if (!named || named != GetProcAddress(proxy, MAKEINTRESOURCEA(item.ordinal)) ||
            !GetProcAddress(system, item.name)) return Fail("named/ordinal export mapping mismatch");
    }
    if (GetProcAddress(proxy, "InitializeASI")) return Fail("standalone unexpectedly exports ASI initializer");

    // 四是越界索引，与本机接入的任何物理/虚拟设备无关。只验证失败结果等价，
    // 不调用 XInputEnable 改变全局输入状态；该 void 导出仅验证名称和 ordinal。
    using State = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using Set = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    using Caps = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    using Battery = DWORD(WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
    using Key = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_KEYSTROKE*);
    using Audio = DWORD(WINAPI*)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);
    XINPUT_STATE stateA{}, stateB{}; XINPUT_VIBRATION vibration{};
    XINPUT_CAPABILITIES capsA{}, capsB{}; XINPUT_BATTERY_INFORMATION batteryA{}, batteryB{};
    XINPUT_KEYSTROKE keyA{}, keyB{}; UINT renderA = 0, renderB = 0, captureA = 0, captureB = 0;
    const bool matches =
        reinterpret_cast<State>(GetProcAddress(proxy,"XInputGetState"))(4,&stateA) ==
            reinterpret_cast<State>(GetProcAddress(system,"XInputGetState"))(4,&stateB) &&
        reinterpret_cast<Set>(GetProcAddress(proxy,"XInputSetState"))(4,&vibration) ==
            reinterpret_cast<Set>(GetProcAddress(system,"XInputSetState"))(4,&vibration) &&
        reinterpret_cast<Caps>(GetProcAddress(proxy,"XInputGetCapabilities"))(4,0,&capsA) ==
            reinterpret_cast<Caps>(GetProcAddress(system,"XInputGetCapabilities"))(4,0,&capsB) &&
        reinterpret_cast<Battery>(GetProcAddress(proxy,"XInputGetBatteryInformation"))(4,BATTERY_DEVTYPE_GAMEPAD,&batteryA) ==
            reinterpret_cast<Battery>(GetProcAddress(system,"XInputGetBatteryInformation"))(4,BATTERY_DEVTYPE_GAMEPAD,&batteryB) &&
        reinterpret_cast<Key>(GetProcAddress(proxy,"XInputGetKeystroke"))(4,0,&keyA) ==
            reinterpret_cast<Key>(GetProcAddress(system,"XInputGetKeystroke"))(4,0,&keyB) &&
        reinterpret_cast<Audio>(GetProcAddress(proxy,"XInputGetAudioDeviceIds"))(4,nullptr,&renderA,nullptr,&captureA) ==
            reinterpret_cast<Audio>(GetProcAddress(system,"XInputGetAudioDeviceIds"))(4,nullptr,&renderB,nullptr,&captureB) &&
        renderA == renderB && captureA == captureB;
    FreeLibrary(system);
    if (!matches) return Fail("System32 forwarding result mismatch");
    std::puts("PASS: seven public named/ordinal exports; six safe System32 forwarding calls");
    return true;
}
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc != 5) return 2;
    try {
        const std::wstring mode(argv[3]);
        if (mode != L"standalone" && mode != L"standalone-first" && mode != L"asi-first") return 2;
        const bool asiFirst = mode == L"asi-first", both = mode != L"standalone";
        // 每次建立唯一新目录，禁止复用旧日志将没有启动的模块误判为成功。
        const std::filesystem::path base(argv[4]);
        std::filesystem::create_directories(base);
        const auto root = base / (mode + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        if (!std::filesystem::create_directory(root)) return 3;
        std::filesystem::create_directory(root / L"plugins");
        const auto standalone = root / L"xinput1_4.dll", asi = root / L"plugins/Sky2PartyEditor.asi";
        std::filesystem::copy_file(argv[1], standalone);
        if (both) std::filesystem::copy_file(argv[2], asi);
        const auto rootLog = root / L"Sky2PartyEditor/party.log";
        const auto asiLog = root / L"plugins/Sky2PartyEditor/party.log";
        HMODULE proxy = nullptr, plugin = nullptr;
        if (asiFirst) {
            plugin = LoadLibraryW(asi.c_str());
            if (!StartAsi(plugin) || !WaitForRejection(asiLog)) return 4;
        }
        proxy = LoadLibraryW(standalone.c_str());
        if (!proxy || (!asiFirst && !WaitForRejection(rootLog))) return 4;
        if (both && !asiFirst) {
            plugin = LoadLibraryW(asi.c_str());
            if (!StartAsi(plugin)) return 4;
        }
        if (!VerifyForwarding(proxy)) return 5;
        for (int i = 0; i < 8; ++i) if (plugin && !StartAsi(plugin)) return 6;
        const auto settle = GetTickCount64() + 1000;
        do {
            const auto logs = Read(rootLog) + Read(asiLog);
            if (Count(logs,kStarted) != 1 || Count(logs,kRejected) != 1 ||
                logs.find("hook installed") != std::string::npos || logs.find("control panel installed") != std::string::npos)
                return Fail("duplicate activation or fake-host hooks") ? 0 : 7;
            Sleep(10);
        } while (GetTickCount64() < settle);
        if (std::filesystem::exists(asiFirst ? rootLog : asiLog))
            return Fail("inactive entry created unexpected data log") ? 0 : 8;
        std::puts("PASS: expected data directory; first entry only; unsupported host rejected; duplicate start suppressed");
        // 生产模块按进程生命周期 pin；不测试不受支持的运行时热卸载。
        return 0;
    } catch (...) { Fail("isolated test setup failed"); return 9; }
}
