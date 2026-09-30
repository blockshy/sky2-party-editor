// 装载真实模块但不进入游戏：ABI 查询应纯读，错误宿主必须在安装 hook 前退出。
#include <Windows.h>
#include "sky2_hub.h"
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <filesystem>
#include <string>

namespace {
int failures = 0, hookCalls = 0, actions = 0;
bool rejectRegistration = false;
void Check(bool value, const char* what) { if (!value) { ++failures; std::fprintf(stderr, "FAILED: %s\n", what); } }
void SKY2_CALL Text(const char*) {}
void SKY2_CALL Empty() {}
int32_t SKY2_CALL Button(const char*, const char*) { return 0; }
int32_t SKY2_CALL Checkbox(const char*, const char*, int32_t*) { return 0; }
int32_t SKY2_CALL Select(const char*, const char*, int32_t) { return 0; }
void SKY2_CALL Disable(int32_t) {}
int32_t SKY2_CALL Child(const char*, float) { return 0; }
void SKY2_CALL Log(void*, const char*) {}
void SKY2_CALL Open(void*) {}
int32_t SKY2_CALL Register(void*, const Sky2Action*) { ++actions; return rejectRegistration ? 0 : 1; }
int32_t SKY2_CALL Language() { return 0; }
int32_t SKY2_CALL Create(void*, void*, void*, void**) { ++hookCalls; return 0; }
int32_t SKY2_CALL Hook(void*, void*) { ++hookCalls; return 0; }
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) return 2;
    rejectRegistration = argc == 3;
    const auto module = LoadLibraryW(argv[1]);
    Check(module != nullptr, "real module can load without private graphics dependencies");
    if (!module) return 1;
    const auto query = reinterpret_cast<Sky2ModuleQuery>(GetProcAddress(module, "Sky2Module_Query"));
    Check(query != nullptr, "module query is exported");
    if (!query) return 1;
    Check(!GetProcAddress(module, "InitializeASI") && !GetProcAddress(module, "XInputGetState"), "module has no legacy entry points");
    Sky2ModuleApi api{}; api.size = sizeof(api);
    Check(query(SKY2_HUB_ABI, &api) != 0, "supported ABI query succeeds");
    Check(api.request_enabled&&api.activity_state&&api.activity_message,"real module exposes optional asynchronous lifecycle");
    Check(api.draw_header!=nullptr,"real module exposes optional fixed header");
    Check(std::strcmp(api.id, "party") == 0 && std::strcmp(api.legacy_asi, "Sky2PartyEditor.asi") == 0,
        "module identity and duplicate protection metadata are stable");
    Check(!query(SKY2_HUB_ABI + 1, &api) && !query(SKY2_HUB_ABI, nullptr), "invalid ABI and null output rejected");
    Sky2ModuleApi shortApi{}; shortApi.size = sizeof(uint32_t);
    Check(!query(SKY2_HUB_ABI, &shortApi), "short ABI output rejected before overwrite");
    Sky2ModuleApi legacyApi{};legacyApi.size=offsetof(Sky2ModuleApi,request_enabled);
    legacyApi.request_enabled=reinterpret_cast<decltype(legacyApi.request_enabled)>(uintptr_t{1});
    Check(query(SKY2_HUB_ABI,&legacyApi)&&legacyApi.initialize&&
        legacyApi.request_enabled==reinterpret_cast<decltype(legacyApi.request_enabled)>(uintptr_t{1}),
        "legacy module output prefix succeeds without overwriting optional tail");
    Sky2ModuleApi oldHeaderApi{};oldHeaderApi.size=offsetof(Sky2ModuleApi,draw_header);
    oldHeaderApi.draw_header=reinterpret_cast<decltype(oldHeaderApi.draw_header)>(uintptr_t{1});
    Check(query(SKY2_HUB_ABI,&oldHeaderApi)&&oldHeaderApi.activity_state&&
        oldHeaderApi.draw_header==reinterpret_cast<decltype(oldHeaderApi.draw_header)>(uintptr_t{1}),
        "lifecycle-era host output does not overwrite unallocated header callback");
    Check(!api.initialize(nullptr), "null host rejected");
    Sky2HostApi host{}; host.size = sizeof(host); host.abi = SKY2_HUB_ABI;
    Check(!api.initialize(&host), "incomplete host rejected without pinning state");
    // 用原 v1 的长度证明布局尾部是可选项；动作注册仍须发生在版本门前，
    // 若错误要求 sizeof(扩展表)，下方六次注册断言会失败。
    Sky2UiApi ui{}; ui.size = offsetof(Sky2UiApi, section); ui.text = ui.text_wrapped = &Text; ui.separator = &Empty;
    ui.checkbox = &Checkbox; ui.button = &Button; ui.selectable = &Select;
    ui.begin_disabled = &Disable; ui.end_disabled = &Empty; ui.begin_child = &Child; ui.end_child = &Empty;
    wchar_t temporary[MAX_PATH]{}; GetTempPathW(MAX_PATH, temporary);
    const auto temporaryRoot = std::filesystem::absolute(temporary).lexically_normal();
    const auto folder = temporaryRoot / (L"Sky2PartyHubTest-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()));
    if (std::filesystem::exists(folder)) return 2; // 已存在目录不归当前测试所有。
    host.owner = &host; host.module_handle = module; host.game_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    host.data_directory = folder.c_str(); host.ui = &ui; host.log = &Log; host.register_action = &Register;
    host.open_page = &Open; host.language = &Language; host.create_hook = &Create;
    host.enable_hook = host.disable_hook = host.remove_hook = &Hook;
    Check(!api.initialize(&host), "real module rejects failed registration or unsupported executable hash");
    Check(hookCalls == 0 && actions == (rejectRegistration ? 1 : 6), "all successful registrations must precede game hooks");
    if (rejectRegistration) {
        // 注册失败必须在业务初始化之前退出；此时连运行时命名互斥都不应创建。
        const auto name = L"Local\\Sky2PartyEditor.Runtime." + std::to_wstring(GetCurrentProcessId());
        const auto guard = CreateMutexW(nullptr, FALSE, name.c_str());
        Check(guard && GetLastError() != ERROR_ALREADY_EXISTS, "registration failure never enters business runtime");
        if (guard) CloseHandle(guard);
    }
    // 仅删除本测试在系统临时目录中创建的单级随机目录，不接触玩家目录或配置。
    std::error_code ignored;
    if (folder.parent_path() == temporaryRoot) std::filesystem::remove_all(folder, ignored);
    std::printf("Party Hub module tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
