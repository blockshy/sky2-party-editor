// 在测试进程自己的私有内存中执行五处候选的 SHR/TEST 片段。
// 不打开游戏文件、不访问游戏进程；验证操作数只影响 0x80 判定的 CPU 语义。
// 此测试不能代替游戏中的选人、战斗、剧情与读档验收。
#include "unavailable_members.h"
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    using Probe = bool(*)(uint32_t);
    size_t verified = 0;
    for (const auto& edit : sky2party::kUnavailableMemberEdits) {
        // 五处原始指令必须都是 SHR EDX, 7; TEST DL, 1；拒绝静默按错误格式执行。
        // Windows x64 首参为 ECX，先 MOV EDX, ECX；复制范围截止到 TEST 立即数，
        // 不复制相对跳转、卡片写入或其他依赖真实游戏地址的后续指令。
        const uint8_t original[]{0xC1,0xEA,0x07,0xF6,0xC2,0x01};
        Require(edit.operand == 5 && edit.replacement == 0 &&
                std::memcmp(edit.expected.data(), original, sizeof(original)) == 0,
                "registered-member site must be SHR EDX,7 and TEST DL,1");
        for (const bool patched : {false, true}) {
            auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096,
                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            Require(code != nullptr, "allocate private instruction page");
            const uint8_t move[]{0x89,0xCA}; // MOV EDX, ECX。
            std::memcpy(code, move, sizeof(move));
            std::memcpy(code + sizeof(move), edit.expected.data(), sizeof(original));
            if (patched) code[sizeof(move) + edit.operand] = edit.replacement;
            const uint8_t tail[]{0x0F,0x95,0xC0,0xC3}; // SETNE AL; RET，返回位是否被识别。
            std::memcpy(code + sizeof(move) + sizeof(original), tail, sizeof(tail));
            DWORD before = 0;
            Require(VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &before) != FALSE,
                    "make private instruction page executable");
            Require(FlushInstructionCache(GetCurrentProcess(), code, 4096) != FALSE,
                    "flush private instruction page");
            auto probe = reinterpret_cast<Probe>(code);
            // 穷举低 16 位全部组合：包含低位分类、0x20、0x40、0x80、0x800 等。
            // 原片段只识别 0x80；修改后片段始终为假。其他门槛位由原生其他指令检查，
            // 本测试不把“此处不读取 0x800”误解为“整个游戏不检查 0x800”。
            for (uint32_t flags = 0; flags < 65536; ++flags) {
                const bool expected = !patched && (flags & 0x80) != 0;
                Require(probe(flags) == expected, "only bit 0x80 controls the original test");
            }
            Require(VirtualFree(code, 0, MEM_RELEASE) != FALSE, "release private instruction page");
            ++verified;
        }
    }
    std::printf("PASS: %zu original/patched unavailable-member instruction variants, 65536 flag combinations each.\n", verified);
}
