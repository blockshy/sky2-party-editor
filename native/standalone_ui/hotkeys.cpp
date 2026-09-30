// 独立快捷键引擎：输入线程只读本 DLL 的短锁快照；文件 I/O 和跨 DLL
// 冲突事务仅发生在初始化、显式保存或恢复默认时。共享区只含固定大小 POD。
#include "hotkeys.h"
#include "input.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstring>
#include <mutex>
#include <sstream>
#include <vector>

namespace sky2solo {
namespace {
constexpr DWORD kMagic = 0x484B5931;
constexpr size_t kModules = 16, kMaxBytes = 256 * 1024;
constexpr WORD kPadButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X |
    XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
    XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_LEFT_SHOULDER |
    XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
struct WireBinding { uint16_t key, pad; uint8_t modifiers; uint8_t reserved[3]; };
struct WireAction { char id[64], label[160]; WireBinding active, pending; };
struct WireModule {
    uint32_t owner, count; uint64_t reservation;
    char label[160]; WireAction actions[MaxHotkeys];
};
struct Registry { DWORD magic, size; uint64_t sequence; WireModule modules[kModules]; };
struct SharedRegistry { HANDLE mutex = nullptr, mapping = nullptr; Registry* value = nullptr; };
std::mutex operations, noticeMutex;
SRWLOCK snapshotLock = SRWLOCK_INIT;
HotkeySnapshot current;
uint32_t localOwner = 0;
std::wstring directory;
std::string notice;
std::array<std::string, MaxHotkeys> ids, labels;
std::array<HotkeyDefinition, MaxHotkeys> definitions{};
std::atomic<size_t> definitionCount{0};

struct SharedLock {
    HANDLE handle = nullptr; bool locked = false;
    explicit SharedLock(HANDLE value, DWORD timeout = 5000) : handle(value) {
        if (handle) { const auto result = WaitForSingleObject(handle, timeout);
            locked = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED; }
    }
    ~SharedLock() { if (locked) ReleaseMutex(handle); }
};
SharedRegistry& Shared() noexcept {
    // 每份 DLL 保留自己的映射句柄至进程结束；初始化用同一互斥量串行，
    // 不依赖 DLL 加载顺序，也不触碰既有 Input.v1 的所有权和 TLS 布局。
    static SharedRegistry result = []() noexcept {
        SharedRegistry result;
        wchar_t name[96]{}, mutexName[96]{};
        swprintf_s(name, L"Local\\Sky2Standalone.Hotkeys.v1.%lu", GetCurrentProcessId());
        swprintf_s(mutexName, L"Local\\Sky2Standalone.Hotkeys.Lock.v1.%lu", GetCurrentProcessId());
        result.mutex = CreateMutexW(nullptr, FALSE, mutexName);
        SharedLock lock(result.mutex);
        if (lock.locked) {
            result.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Registry), name);
            result.value = result.mapping ? static_cast<Registry*>(MapViewOfFile(result.mapping,
                FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Registry))) : nullptr;
            if (result.value && !result.value->magic) {
                result.value->size = sizeof(Registry); result.value->magic = kMagic;
            }
            if (result.value && (result.value->magic != kMagic || result.value->size != sizeof(Registry))) {
                UnmapViewOfFile(result.value); result.value = nullptr;
            }
        }
        return result;
    }();
    return result;
}
WireBinding Wire(const HotkeyBinding& value) noexcept { return {value.key, value.pad, value.modifiers, {0,0,0}}; }
bool Overlap(const WireBinding& a, const WireBinding& b) noexcept {
    return (a.key && a.key == b.key && a.modifiers == b.modifiers) || (a.pad && a.pad == b.pad);
}
void Publish(HotkeySnapshot value) noexcept {
    AcquireSRWLockExclusive(&snapshotLock); value.revision = current.revision + 1;
    current = value; ReleaseSRWLockExclusive(&snapshotLock);
}
void SetNotice(std::string value) { std::lock_guard<std::mutex> lock(noticeMutex); notice = std::move(value); }
std::string BasicError(size_t index, const HotkeyBinding& value) {
    if (index >= definitionCount.load()) return "Unknown action.";
    if (value.modifiers & ~(HotkeyCtrl | HotkeyAlt | HotkeyShift)) return "Unsupported modifier.";
    if ((!value.key && value.modifiers) || (value.key && !ValidHotkeyKey(value.key))) return "This keyboard key is reserved.";
    if (!ValidHotkeyPad(value.pad)) return "Use View plus one supported controller button.";
    if (definitions[index].opensWindow && !value.key) return "The window must keep a keyboard shortcut.";
    // Alt+F4 即使再加 Ctrl/Shift 仍拒绝，避免不同窗口/映射软件继续解释系统关闭。
    if (value.key == VK_F4 && (value.modifiers & HotkeyAlt)) return "Alt+F4 is reserved by Windows.";
    if (value.key == VK_DELETE && (value.modifiers & (HotkeyCtrl | HotkeyAlt)) == (HotkeyCtrl | HotkeyAlt))
        return "Ctrl+Alt+Delete is reserved by Windows.";
    if (value.key == VK_BACK && (value.modifiers & HotkeyAlt)) return "Alt+Backspace is reserved.";
    return {};
}
std::string CheckGroup(const Registry& registry, const HotkeySnapshot& proposed) {
    for (size_t i = 0; i < proposed.count; ++i) {
        if (auto error = BasicError(i, proposed.bindings[i]); !error.empty()) return error;
        const auto candidate = Wire(proposed.bindings[i]);
        for (size_t j = 0; j < i; ++j) if (Overlap(candidate, Wire(proposed.bindings[j])))
            return std::string("Conflict: ") + definitions[j].label;
        for (const auto& module : registry.modules) if (module.owner && module.owner != localOwner)
            for (size_t j = 0; j < module.count; ++j) {
                const auto& action = module.actions[j];
                if (Overlap(candidate, action.active) || (module.reservation && Overlap(candidate, action.pending)))
                    return std::string("Conflict: ") + module.label + " / " + action.label;
            }
    }
    return {};
}
WireModule* OwnModule(Registry& registry) noexcept {
    for (auto& module : registry.modules) if (module.owner == localOwner) return &module;
    return nullptr;
}
struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
bool PlainDirectory(std::wstring path) noexcept {
    // 仅接受运行时选定的本地绝对目录，逐级拒绝 junction/symlink。不得创建
    // 任意父目录；目录不存在或不安全时仅用内存默认值，显式保存会报错。
    if (path.size() < 3 || path[1] != L':' || path[2] != L'\\') return false;
    for (;;) {
        const auto attr = GetFileAttributesW(path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY) || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        if (path.size() <= 3) return true;
        const auto slash = path.find_last_of(L'\\');
        if (slash == std::wstring::npos) return false;
        path.resize(slash <= 2 ? 3 : slash);
    }
}
bool ReadConfig(std::string& bytes, bool& exists, std::string& error) {
    if (!PlainDirectory(directory)) { error = "Shortcut directory is unavailable or unsafe."; return false; }
    File file;
    file.handle = CreateFileW((directory + L"\\shortcuts.ini").c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file.handle == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) { bytes.clear(); exists = false; return true; }
        error = "Cannot read shortcuts.ini."; return false;
    }
    exists = true; BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER length{};
    if (!GetFileInformationByHandle(file.handle, &info) || info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileSizeEx(file.handle, &length) || length.QuadPart < 0 || length.QuadPart > kMaxBytes) {
        error = "shortcuts.ini is not a supported plain file."; return false;
    }
    bytes.resize(static_cast<size_t>(length.QuadPart)); DWORD read = 0;
    if ((!bytes.empty() && !ReadFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) || read != bytes.size()) {
        error = "Cannot read the complete shortcuts.ini."; return false;
    }
    if (bytes.find('\0') != std::string::npos) { error = "shortcuts.ini must use UTF-8 text."; return false; }
    return true;
}
std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}
std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> result; std::istringstream stream(bytes);
    std::string line;
    while (std::getline(stream, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); result.push_back(std::move(line)); }
    if (!result.empty() && result[0].compare(0,3,"\xEF\xBB\xBF") == 0) result[0].erase(0,3);
    return result;
}
bool Number(const std::string& value, unsigned& number) noexcept {
    const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
    return !value.empty() && result.ec == std::errc{} && result.ptr == value.data() + value.size();
}
bool ParseBinding(const std::string& value, HotkeyBinding& result) {
    const auto first = value.find(','), second = first == std::string::npos ? first : value.find(',', first + 1);
    if (first == std::string::npos || second == std::string::npos) return false;
    unsigned key = 0, modifiers = 0, pad = 0;
    if (!Number(Trim(value.substr(0, first)), key) || !Number(Trim(value.substr(first + 1, second - first - 1)), modifiers) ||
        !Number(Trim(value.substr(second + 1)), pad) || key > 65535 || modifiers > 255 || pad > 65535) return false;
    result = {static_cast<uint16_t>(key), static_cast<uint8_t>(modifiers), static_cast<uint16_t>(pad)}; return true;
}
bool ReadEntries(const std::string& bytes, HotkeySnapshot& value, std::string& warning) {
    bool inKeys = false; std::array<bool, MaxHotkeys> seen{};
    for (const auto& line : Lines(bytes)) {
        const auto trimmed = Trim(line);
        if (!trimmed.empty() && trimmed.front() == '[') { inKeys = trimmed == "[Hotkeys]"; continue; }
        if (!inKeys || trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;
        const auto equal = trimmed.find('='); if (equal == std::string::npos) continue;
        const auto name = Trim(trimmed.substr(0, equal)), entry = Trim(trimmed.substr(equal + 1));
        if (name == "Schema" && entry != "1") { warning = "Unsupported shortcut schema; defaults are active."; return false; }
        for (size_t i = 0; i < value.count; ++i) if (name == definitions[i].id) {
            HotkeyBinding parsed;
            if (seen[i] || !ParseBinding(entry, parsed) || !BasicError(i, parsed).empty()) {
                value.bindings[i] = definitions[i].defaults;
                warning += std::string("Invalid shortcut: ") + definitions[i].label + ". Defaults used. ";
            } else value.bindings[i] = parsed;
            seen[i] = true;
        }
    }
    return true;
}
std::string Serialize(const std::string& previous, const HotkeySnapshot& value) {
    // 只替换本模块认识的键；未知节、未知动作及注释均保留，便于版本回退。
    std::string result; bool inKeys = false, found = false, written = false;
    const auto appendKnown = [&]() {
        if (written) return; written = true; result += "Schema=1\r\n";
        for (size_t i = 0; i < value.count; ++i) {
            const auto& binding = value.bindings[i];
            result += std::string(definitions[i].id) + "=" + std::to_string(binding.key) + "," +
                std::to_string(binding.modifiers) + "," + std::to_string(binding.pad) + "\r\n";
        }
    };
    for (const auto& line : Lines(previous)) {
        const auto trimmed = Trim(line);
        if (!trimmed.empty() && trimmed.front() == '[') {
            if (inKeys) appendKnown(); inKeys = trimmed == "[Hotkeys]"; found |= inKeys;
        }
        if (inKeys) {
            const auto equal = trimmed.find('=');
            if (equal != std::string::npos) {
                const auto name = Trim(trimmed.substr(0, equal)); bool known = name == "Schema";
                for (size_t i = 0; i < value.count; ++i) known |= name == definitions[i].id;
                if (known) continue;
            }
        }
        result += line + "\r\n";
    }
    if (!found) result += "[Hotkeys]\r\n";
    appendKnown(); return result;
}
bool SaveConfig(const HotkeySnapshot& value, std::string& error) {
    std::string before; bool existed = false;
    if (!ReadConfig(before, existed, error)) return false;
    HotkeySnapshot probe = value; std::string schema;
    if (!ReadEntries(before, probe, schema)) { error = schema; return false; }
    const auto bytes = Serialize(before, value);
    if (bytes.size() > kMaxBytes) { error = "shortcuts.ini is too large."; return false; }
    static std::atomic<uint64_t> serial{0};
    const auto temporary = directory + L"\\shortcuts.ini.tmp." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(serial.fetch_add(1));
    struct Temporary { std::wstring path; ~Temporary() { if (!path.empty()) DeleteFileW(path.c_str()); } } cleanup{temporary};
    {
        File file; file.handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        DWORD written = 0;
        if (file.handle == INVALID_HANDLE_VALUE || !WriteFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
            written != bytes.size() || !FlushFileBuffers(file.handle)) { error = "Cannot write shortcut settings."; return false; }
    }
    // 覆盖前重新核对目录、文件类型和内容，拒绝覆盖写入期间出现的外部变更。
    std::string latest; bool nowExists = false;
    if (!ReadConfig(latest, nowExists, error)) return false;
    if (existed != nowExists || latest != before) { error = "Shortcut settings changed externally; retry."; return false; }
    if (!MoveFileExW(temporary.c_str(), (directory + L"\\shortcuts.ini").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot replace shortcut settings."; return false;
    }
    cleanup.path.clear(); return true;
}
bool CommitGroup(const HotkeySnapshot& proposed, std::string& error) {
    auto& shared = Shared(); uint64_t ticket = 0;
    {
        SharedLock lock(shared.mutex);
        if (!lock.locked || !shared.value) { error = "Shortcut registry is unavailable."; return false; }
        if (!(error = CheckGroup(*shared.value, proposed)).empty()) return false;
        auto* own = OwnModule(*shared.value);
        if (!own) { error = "Shortcut module is not registered."; return false; }
        ticket = ++shared.value->sequence; if (!ticket) ticket = ++shared.value->sequence;
        own->reservation = ticket;
        for (size_t i = 0; i < proposed.count; ++i) own->actions[i].pending = Wire(proposed.bindings[i]);
    }
    // 共享锁绝不跨文件 I/O。其它 DLL 同时校验时会同时看到旧生效值和预留值，
    // 因而两个保存事务无法同时占到同一组合；写失败只撤销本事务的预留。
    bool saved = false;
    try { saved = SaveConfig(proposed, error); }
    catch (...) { error = "Cannot save shortcut settings."; }
    {
        SharedLock lock(shared.mutex, INFINITE);
        auto* own = OwnModule(*shared.value);
        if (own && own->reservation == ticket) {
            if (saved) for (size_t i = 0; i < proposed.count; ++i) own->actions[i].active = own->actions[i].pending;
            own->reservation = 0;
        }
        if (saved) Publish(proposed); // 此后仅 POD 赋值，不再分配或执行可能失败的 I/O。
    }
    if (saved) { std::lock_guard<std::mutex> lock(noticeMutex); notice.clear(); }
    return saved;
}
}

bool InitializeHotkeys(uint32_t owner, const char* moduleLabel, const wchar_t* dataDirectory,
    const HotkeyDefinition* source, size_t count) noexcept {
    try {
        std::lock_guard<std::mutex> operation(operations);
        if (localOwner || !owner || !moduleLabel || std::strlen(moduleLabel) >= 160 || !source || !count || count > MaxHotkeys || !dataDirectory) return false;
        for (size_t i = 0; i < count; ++i) {
            if (!source[i].id || !*source[i].id || std::strlen(source[i].id) >= 64 || !source[i].label || std::strlen(source[i].label) >= 160) return false;
            for (const unsigned char* p = reinterpret_cast<const unsigned char*>(source[i].id); *p; ++p)
                if (!( (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')) return false;
            for (size_t j = 0; j < i; ++j) if (std::strcmp(source[i].id, source[j].id) == 0) return false;
            ids[i] = source[i].id; labels[i] = source[i].label;
            definitions[i] = {ids[i].c_str(), labels[i].c_str(), source[i].defaults, source[i].opensWindow};
        }
        definitionCount.store(count);
        wchar_t absolute[32768]{};
        const auto length = GetFullPathNameW(dataDirectory, static_cast<DWORD>(std::size(absolute)), absolute, nullptr);
        if (!length || length >= std::size(absolute)) return false;
        directory = absolute; while (directory.size() > 3 && directory.back() == L'\\') directory.pop_back();
        HotkeySnapshot initial; initial.count = count;
        for (size_t i = 0; i < count; ++i) {
            if (!BasicError(i, definitions[i].defaults).empty()) return false;
            initial.bindings[i] = definitions[i].defaults;
        }
        std::string bytes, warning; bool exists = false;
        if (ReadConfig(bytes, exists, warning)) {
            auto configured = initial;
            if (ReadEntries(bytes, configured, warning)) initial = configured;
        }
        auto& shared = Shared(); SharedLock lock(shared.mutex);
        if (!lock.locked || !shared.value) return false;
        WireModule* slot = nullptr;
        for (auto& module : shared.value->modules) {
            if (module.owner == owner) return false;
            if (!module.owner && !slot) slot = &module;
        }
        if (!slot) return false;
        localOwner = owner;
        // 启动不重写玩家配置。冲突的普通动作回退默认或取消绑定；窗口动作
        // 必须找到一个可用键盘入口。三修饰键备用仅用于坏配置的启动恢复。
        HotkeySnapshot accepted; accepted.count = count;
        for (size_t i = 0; i < count; ++i) {
            auto checkCandidate = [&](HotkeyBinding binding) {
                accepted.bindings[i] = binding;
                // 未处理的窗口行不能参与“必须有键盘”检查；仅检查已处理前缀。
                auto prefix = accepted; prefix.count = i + 1; return CheckGroup(*shared.value, prefix);
            };
            auto error = checkCandidate(initial.bindings[i]);
            if (!error.empty()) {
                warning += std::string("Adjusted ") + definitions[i].label + ": " + error + " ";
                error = checkCandidate(definitions[i].defaults);
                if (!error.empty()) {
                    if (!definitions[i].opensWindow) accepted.bindings[i] = {};
                    else {
                        bool found = false;
                        for (uint16_t key = VK_F1; key <= VK_F24; ++key) {
                            HotkeyBinding fallback{key, HotkeyCtrl | HotkeyAlt | HotkeyShift, 0};
                            if (checkCandidate(fallback).empty()) { found = true; break; }
                        }
                        if (!found) { localOwner = 0; return false; }
                    }
                }
            }
        }
        if (!warning.empty()) for (size_t i = 0; i < count; ++i) if (definitions[i].opensWindow)
            warning += std::string("Window shortcut: ") + HotkeyKeyboardText(accepted.bindings[i]) +
                " / " + HotkeyPadText(accepted.bindings[i]) + ". ";
        SetNotice(std::move(warning));
        // 所有可能分配的工作结束后才公布固定结构，避免初始化失败留下幽灵占用。
        *slot = {}; slot->owner = owner; slot->count = static_cast<uint32_t>(count);
        strcpy_s(slot->label, moduleLabel);
        for (size_t i = 0; i < count; ++i) {
            strcpy_s(slot->actions[i].id, definitions[i].id); strcpy_s(slot->actions[i].label, definitions[i].label);
            slot->actions[i].active = Wire(accepted.bindings[i]);
        }
        Publish(accepted); return true;
    } catch (...) { return false; }
}
HotkeySnapshot ReadHotkeys() noexcept {
    AcquireSRWLockShared(&snapshotLock); const auto value = current; ReleaseSRWLockShared(&snapshotLock); return value;
}
size_t HotkeyCount() noexcept { return definitionCount.load(); }
const HotkeyDefinition* HotkeyInfo(size_t index) noexcept { return index < definitionCount.load() ? &definitions[index] : nullptr; }
std::string HotkeyNotice() { std::lock_guard<std::mutex> lock(noticeMutex); return notice; }
std::string ValidateHotkey(size_t index, const HotkeyBinding& candidate) {
    std::lock_guard<std::mutex> operation(operations);
    auto value = ReadHotkeys(); if (index >= value.count) return "Unknown action."; value.bindings[index] = candidate;
    auto& shared = Shared(); SharedLock lock(shared.mutex);
    return lock.locked && shared.value ? CheckGroup(*shared.value, value) : "Shortcut registry is unavailable.";
}
bool CommitHotkey(size_t index, const HotkeyBinding& candidate, std::string& error) {
    std::lock_guard<std::mutex> operation(operations);
    auto value = ReadHotkeys(); if (index >= value.count) { error = "Unknown action."; return false; }
    value.bindings[index] = candidate; return CommitGroup(value, error);
}
bool RestoreDefaultHotkeys(std::string& error) {
    std::lock_guard<std::mutex> operation(operations); auto value = ReadHotkeys();
    if (!value.count) { error = "Shortcut module is not registered."; return false; }
    for (size_t i = 0; i < value.count; ++i) value.bindings[i] = definitions[i].defaults;
    return CommitGroup(value, error);
}
bool ValidHotkeyKey(uint16_t key) noexcept {
    // 不接受导航、IME、媒体/浏览器、鼠标或仅修饰键。可见选择器与文件校验
    // 共用同一白名单，手工 INI 无法绕过规则；系统组合另由 BasicError 拒绝。
    return key == 0 || (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') ||
        (key >= VK_F1 && key <= VK_F24) || (key >= VK_NUMPAD0 && key <= VK_DIVIDE) ||
        key == VK_INSERT || key == VK_DELETE || key == VK_BACK ||
        (key >= VK_OEM_1 && key <= VK_OEM_3) || (key >= VK_OEM_4 && key <= VK_OEM_8) || key == VK_OEM_102;
}
bool ValidHotkeyPad(uint16_t button) noexcept { return !button || (!(button & ~kPadButtons) && !(button & (button - 1))); }
std::string HotkeyKeyName(uint16_t key) {
    if (!key) return "Unbound";
    if (key >= '0' && key <= '9') return std::string(1, static_cast<char>(key));
    if (key >= 'A' && key <= 'Z') return std::string(1, static_cast<char>(key));
    if (key >= VK_F1 && key <= VK_F24) return "F" + std::to_string(key - VK_F1 + 1);
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) return "Num " + std::to_string(key - VK_NUMPAD0);
    switch (key) {
    case VK_INSERT: return "Insert"; case VK_DELETE: return "Delete"; case VK_BACK: return "Backspace";
    case VK_MULTIPLY: return "Num *"; case VK_ADD: return "Num +"; case VK_SUBTRACT: return "Num -";
    case VK_DECIMAL: return "Num ."; case VK_DIVIDE: return "Num /"; case VK_SEPARATOR: return "Num separator";
    default: {
        // OEM 标点随键盘布局变化，优先使用 Windows 对当前扫描码的本地化
        // 名称；失败时给出常见键帽符号，避免选择器只出现难以辨认的 VK 数字。
        wchar_t name[128]{}; char utf8[512]{};
        const auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
        if (scan && GetKeyNameTextW(static_cast<LONG>(scan << 16), name, static_cast<int>(std::size(name))) > 0 &&
            WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, static_cast<int>(std::size(utf8)), nullptr, nullptr) > 0)
            return utf8;
        switch (key) {
        case VK_OEM_1: return "; / :"; case VK_OEM_PLUS: return "+ / ="; case VK_OEM_COMMA: return ", / <";
        case VK_OEM_MINUS: return "- / _"; case VK_OEM_PERIOD: return ". / >"; case VK_OEM_2: return "/ / ?";
        case VK_OEM_3: return "` / ~"; case VK_OEM_4: return "[ / {"; case VK_OEM_5: return "\\ / |";
        case VK_OEM_6: return "] / }"; case VK_OEM_7: return "' / \""; case VK_OEM_102: return "OEM < / >";
        default: return "OEM " + std::to_string(key);
        }
    }
    }
}
std::string HotkeyKeyboardText(const HotkeyBinding& binding) {
    std::string result;
    if (binding.modifiers & HotkeyCtrl) result += "Ctrl + ";
    if (binding.modifiers & HotkeyAlt) result += "Alt + ";
    if (binding.modifiers & HotkeyShift) result += "Shift + ";
    return result + HotkeyKeyName(binding.key);
}
std::string HotkeyPadText(const HotkeyBinding& binding) {
    const char* button = nullptr;
    switch (binding.pad) {
    case 0: return "Unbound";
    case XINPUT_GAMEPAD_A: button="A"; break; case XINPUT_GAMEPAD_B: button="B"; break;
    case XINPUT_GAMEPAD_X: button="X"; break; case XINPUT_GAMEPAD_Y: button="Y"; break;
    case XINPUT_GAMEPAD_DPAD_UP: button="D-pad Up"; break; case XINPUT_GAMEPAD_DPAD_DOWN: button="D-pad Down"; break;
    case XINPUT_GAMEPAD_DPAD_LEFT: button="D-pad Left"; break; case XINPUT_GAMEPAD_DPAD_RIGHT: button="D-pad Right"; break;
    case XINPUT_GAMEPAD_LEFT_SHOULDER: button="LB"; break; case XINPUT_GAMEPAD_RIGHT_SHOULDER: button="RB"; break;
    case XINPUT_GAMEPAD_LEFT_THUMB: button="LS"; break; case XINPUT_GAMEPAD_RIGHT_THUMB: button="RS"; break;
    default: return "Invalid";
    }
    return std::string("View + ") + button;
}
HotkeyKeyboardState ReadHotkeyKeyboardState() noexcept {
    HotkeyKeyboardState state;
    for (unsigned key = 1; key < 256; ++key) if (IsPhysicalVirtualKey(key))
        state.down[key] = (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
    if (state.down[VK_CONTROL] || state.down[VK_LCONTROL] || state.down[VK_RCONTROL]) state.modifiers |= HotkeyCtrl;
    if (state.down[VK_MENU] || state.down[VK_LMENU] || state.down[VK_RMENU]) state.modifiers |= HotkeyAlt;
    if (state.down[VK_SHIFT] || state.down[VK_LSHIFT] || state.down[VK_RSHIFT]) state.modifiers |= HotkeyShift;
    state.windows = state.down[VK_LWIN] || state.down[VK_RWIN]; return state;
}
bool AnyHotkeyKeyboardDown(const HotkeyKeyboardState& state) noexcept {
    for (unsigned key = 1; key < 256; ++key) if (IsPhysicalVirtualKey(key) && state.down[key]) return true;
    return state.windows || state.modifiers != 0;
}
bool HotkeyKeyHeld(const HotkeyBinding& binding, const HotkeyKeyboardState& state) noexcept {
    return binding.key && binding.key < state.down.size() && state.down[binding.key] && !state.windows && binding.modifiers == state.modifiers;
}
bool HotkeyPadHeld(const HotkeyBinding& binding, WORD buttons) noexcept {
    // 精确匹配全部数字按钮；Start、斜方向或多按钮均不能顺便触发动作。
    return binding.pad && buttons == static_cast<WORD>(XINPUT_GAMEPAD_BACK | binding.pad);
}
uint32_t HotkeyPadHeldMask(const HotkeySnapshot& snapshot, WORD buttons) noexcept {
    uint32_t result = 0;
    for (size_t i = 0; i < std::min(snapshot.count, MaxHotkeys); ++i) if (HotkeyPadHeld(snapshot.bindings[i], buttons)) result |= 1u << i;
    return result;
}
uint32_t HotkeyPadPressedMask(const HotkeySnapshot& snapshot, WORD buttons, WORD previous) noexcept {
    uint32_t result = HotkeyPadHeldMask(snapshot, buttons);
    for (size_t i = 0; i < std::min(snapshot.count, MaxHotkeys); ++i) if (!(snapshot.bindings[i].pad & (buttons & ~previous))) result &= ~(1u << i);
    return result;
}
uint32_t HotkeyKeyboardTracker::Update(const HotkeySnapshot& snapshot, const HotkeyKeyboardState& state, bool available) noexcept {
    if (revision_ != snapshot.revision || !available) { revision_ = snapshot.revision; armed_ = false; }
    uint32_t result = 0;
    if (available && armed_) {
        for (size_t i = 0; i < std::min(snapshot.count, MaxHotkeys); ++i) {
            const auto& binding = snapshot.bindings[i];
            if (binding.key < previous_.size() && !previous_[binding.key] && HotkeyKeyHeld(binding, state)) result |= 1u << i;
        }
    } else if (available && !AnyHotkeyKeyboardDown(state)) armed_ = true;
    previous_ = state.down; return result;
}
}
