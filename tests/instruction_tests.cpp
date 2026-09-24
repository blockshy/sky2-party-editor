// 在新分配的私有可执行页验证真实 x64 TEST 指令效果；不加载或执行游戏 EXE。
// 这只能验证四处操作数修改的CPU语义，不能替代原生菜单交换的实机回归。
#include "patch_plan.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    using Probe = bool(*)(uint32_t);
    size_t verified = 0;
    for (const auto& edit : sky2party::kFixedMemberEdits) {
        // Windows x64第一参数ECX；将它移入原指令使用的EDX或R9D，然后只执行
        // 移位与TEST，最后SETNE返回测试结果。复制范围明确排除游戏的相对跳转。
        const bool r9 = edit.expected[0] == 0x41;
        const std::array<uint8_t, 3> move = r9 ? std::array<uint8_t,3>{0x41,0x89,0xC9} :
                                                             std::array<uint8_t,3>{0x89,0xCA,0x90};
        const size_t prefix = r9 ? 3 : 2;
        const size_t testedBytes = static_cast<size_t>(edit.operand) + 1;
        for (const bool patched : {false, true}) {
            auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            Require(code != nullptr, "allocate isolated code");
            std::memcpy(code, move.data(), prefix);
            std::memcpy(code + prefix, edit.expected.data(), testedBytes);
            if (patched) code[prefix + edit.operand] = edit.replacement;
            const uint8_t tail[]{0x0F,0x95,0xC0,0xC3}; // SETNE AL; RET。
            std::memcpy(code + prefix + testedBytes, tail, sizeof(tail));
            DWORD before = 0;
            Require(VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &before) != FALSE, "make isolated code executable");
            Require(FlushInstructionCache(GetCurrentProcess(), code, 4096) != FALSE, "flush isolated code");
            auto probe = reinterpret_cast<Probe>(code);
            for (uint32_t flags = 0; flags < 65536; ++flags) {
                const bool expected = !patched && (flags & 0x20) != 0;
                Require(probe(flags) == expected, "only the fixed-member bit controls original test");
            }
            VirtualFree(code, 0, MEM_RELEASE);
            ++verified;
        }
    }
    std::printf("PASS: %zu original/patched instruction variants, 65536 flag combinations each.\n", verified);
}
