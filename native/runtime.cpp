// 宿主识别与插件生命周期。只在已验证的游戏构建中启用编成菜单挂钩，
// 两种入口共用控制面板与输入，数据目录跟随入口所在目录，保留前一层调用链。
#include "runtime.h"
#include "control_state.h"
#include "panel.h"
#include "game_language.h"
#include "game_names.h"
#include <bcrypt.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace sky2party {
namespace {
HMODULE moduleHandle = nullptr;
HANDLE instanceGuard = nullptr;
std::wstring dataFolder;
SRWLOCK logLock = SRWLOCK_INIT;
constexpr char kExecutableHash[] = "d8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf";
constexpr LONGLONG kLogLimit = 1024 * 1024;

bool PlainDirectory(const std::wstring& path) noexcept {
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool PlainParentChain(std::wstring path) {
    // 安装指南限定本地绝对目录；逐级检查直到盘符根，避免上级 junction 将日志
    // 目录重定向到插件目录树之外。这里仅做一次启动检查，不在游戏帧循环执行。
    if (path.size() < 3 || path[1] != L':' || (path[2] != L'\\' && path[2] != L'/')) return false;
    for (;;) {
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        if (path.size() <= 3) return true;
        const auto separator = path.find_last_of(L"\\/");
        if (separator == std::wstring::npos) return false;
        path.resize(separator <= 2 ? 3 : separator);
    }
}

bool PrepareFolder() {
    wchar_t path[MAX_PATH]{};
    const auto length = GetModuleFileNameW(moduleHandle, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    const std::wstring modulePath(path, length);
    const auto separator = modulePath.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return false;
    const auto parent = modulePath.substr(0, separator);
    // 不跟随插件目录或数据目录本身的链接，避免日志写入到用户未预期的位置。
    if (!PlainParentChain(parent)) return false;
    dataFolder = parent + L"\\Sky2PartyEditor";
    return PlainDirectory(dataFolder);
}

bool SupportedExecutable() {
    wchar_t path[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const auto size = input.tellg();
    // 限制输入范围，既避免错误宿主的大文件分配，也不把单纯文件名当作版本证明。
    if (size <= 0 || size > 128 * 1024 * 1024) return false;
    input.seekg(0);
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    std::array<unsigned char, 32> digest{};
    const auto status = BCryptHash(algorithm, nullptr, 0, bytes.data(), static_cast<ULONG>(bytes.size()),
                                  digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return false;
    char hash[65]{};
    for (size_t i = 0; i < digest.size(); ++i) sprintf_s(hash + i * 2, sizeof(hash) - i * 2, "%02x", digest[i]);
    return std::strcmp(hash, kExecutableHash) == 0;
}

DWORD WINAPI Initialize(void*) noexcept {
    try {
        // 保持初始化线程与单实例保护的生命周期，不支持运行中卸载或重新加载。
        // 主菜单挂钩会跳转回本模块，因此不能在游戏运行时卸载插件。
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&Start), &pinned)) return 0;
        wchar_t guardName[96]{};
        swprintf_s(guardName, L"Local\\Sky2PartyEditor.Runtime.%lu", GetCurrentProcessId());
        instanceGuard = CreateMutexW(nullptr, FALSE, guardName);
        const auto guardError = GetLastError();
        if (!instanceGuard) return 0;
        if (guardError == ERROR_ALREADY_EXISTS) {
            CloseHandle(instanceGuard);
            instanceGuard = nullptr;
            return 0;
        }
        if (!PrepareFolder()) return 0;
        Log("Sky2PartyEditor " SKY2_PARTY_VERSION "; Runtime initialization started.");
        if (!SupportedExecutable()) {
            Log("Unsupported executable; no game hooks installed.");
            return 0;
        }
        const auto settings = dataFolder + L"\\settings.ini";
        if (!GetPrivateProfileIntW(L"Party", L"Enabled", 1, settings.c_str())) {
            Log("Disabled by settings.ini; restart after changing configuration.");
            return 0;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        // 语言字段只对上方完整 SHA-256 已验证的宿主有效。此读取不写游戏设置或
        // 存档；渲染线程后续刷新同一检测器，以游戏文字语言统一界面与原生角色名称。
        InitializeGameLanguage(base);
        // 原生名称来自已核验宿主旁的资源包，而非插件工作目录。只在启动时读取
        // 必要的两张表并发布不可变缓存；失败仅影响原文显示，不阻塞其余功能。
        wchar_t executablePath[MAX_PATH]{};
        const auto executableLength = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
        const bool namesReady = executableLength && executableLength < MAX_PATH &&
            InitializeGameNames(std::filesystem::path(executablePath).parent_path());
        Log(namesReady ? "Native character names and menu terms ready for all eight text languages." :
            "Native names unavailable for one or more languages; affected labels use explicit character IDs.");
        uint32_t initialFeatures = 0;
        const struct { const wchar_t* key; uint32_t bit; int initial; } options[]{
            {L"UnlockFixedMembers", FeatureFixedMembers, 1},
            {L"AnywhereFormation", FeatureAnywhere, 1},
            {L"UnlockUnavailableMembers", FeatureUnavailable, 1},
            {L"AllowUnjoinedMembers", FeatureUnjoined, 0}};
        for (const auto& option : options)
            if (GetPrivateProfileIntW(L"Party", option.key, option.initial, settings.c_str())) initialFeatures |= option.bit;
        // 初始化线程只安装已验证的常驻协调入口。真实设置与加入操作由安全游戏帧执行。
        if (!ConfigureRosterService(base) || !InstallPartyHooks(base) || !InstallControlService(base, initialFeatures)) {
            Log("Party control validation or hook installation failed; no initial feature request applied.");
            return 0;
        }
        Log(InstallPanel(moduleHandle, base) ? "Party control panel installed; F11 or View + LS." :
            "Party panel initialization failed; inspect diagnostics.");
    } catch (...) {
        Log("Initialization failed; no further installation attempted.");
    }
    return 0;
}
}

void ConfigureModule(HMODULE module) noexcept { moduleHandle = module; }

bool SaveFeaturePreferences(uint32_t features) noexcept {
    try {
        if (dataFolder.empty() || !PlainParentChain(dataFolder)) return false;
        const std::wstring path = dataFolder + L"\\settings.ini";
        // 配置属于玩家可编辑文件：保留其它节和键。拒绝链接文件，避免设置写入
        // 被目录/文件重解析点或硬链接带到插件目录之外；它不保存任何角色数据。
        HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION information{};
        const bool plain = GetFileInformationByHandle(file, &information) && information.nNumberOfLinks == 1 &&
            !(information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
        CloseHandle(file);
        if (!plain) return false;
        const struct { const wchar_t* key; uint32_t bit; } options[]{
            {L"UnlockFixedMembers", FeatureFixedMembers}, {L"AnywhereFormation", FeatureAnywhere},
            {L"UnlockUnavailableMembers", FeatureUnavailable}, {L"AllowUnjoinedMembers", FeatureUnjoined}};
        bool saved = true;
        for (const auto& option : options)
            saved &= WritePrivateProfileStringW(L"Party", option.key,
                (features & option.bit) ? L"1" : L"0", path.c_str()) != FALSE;
        return saved;
    } catch (...) { return false; }
}

void Log(const char* message) noexcept {
    if (!message || dataFolder.empty()) return;
    AcquireSRWLockExclusive(&logLock);
    HANDLE file = INVALID_HANDLE_VALUE;
    try {
        const auto path = dataFolder + L"\\party.log";
        file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION info{};
        LARGE_INTEGER size{};
        if (file != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file, &info) &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
            info.nNumberOfLinks == 1 && GetFileSizeEx(file, &size)) {
            SYSTEMTIME now{};
            GetLocalTime(&now);
            char line[1536]{};
            const auto count = _snprintf_s(line, sizeof(line), _TRUNCATE,
                "%04u-%02u-%02u %02u:%02u:%02u %s\r\n", now.wYear, now.wMonth, now.wDay,
                now.wHour, now.wMinute, now.wSecond, message);
            if (count > 0) {
                LARGE_INTEGER zero{};
                const bool reset = size.QuadPart < 0 || size.QuadPart > kLogLimit - count;
                // 单模块进程锁以及跨进程拒绝共享写入，共同避免并发截断日志。
                if (SetFilePointerEx(file, zero, nullptr, reset ? FILE_BEGIN : FILE_END) &&
                    (!reset || SetEndOfFile(file))) {
                    DWORD written = 0;
                    WriteFile(file, line, static_cast<DWORD>(count), &written, nullptr);
                }
            }
        }
    } catch (...) { /* 诊断失败不抛入游戏调用栈。 */ }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    ReleaseSRWLockExclusive(&logLock);
}

void Start() noexcept {
    static std::once_flag once;
    try {
        std::call_once(once, [] {
            if (HANDLE thread = CreateThread(nullptr, 0, Initialize, nullptr, 0, nullptr)) CloseHandle(thread);
        });
    } catch (...) { /* ASI 装载入口不得把 C++ 异常传播给通用 Loader。 */ }
}
}
