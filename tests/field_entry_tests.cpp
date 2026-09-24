// 在私有可执行页运行精确地点/119比较片段，验证只放宽编成入口而不写地图/剧情数据。
// 本测试既不连接游戏，也不执行游戏资源；其他原生剧情和战斗门槛仍须实机回归。
#include "patch_plan.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void Require(bool value,const char* message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1); }
}
int main() {
    constexpr size_t fragmentLength=9;
    using Probe=bool(*)(const void*);
    for (const auto& edit : sky2party::kFieldLocationEdits) {
    // Windows x64第一个参数位于RCX。动作/保持片段读取RAX，出现片段直接读取
    // RCX；按已审核的ModR/M选择MOV RAX,RCX或MOV RCX,RCX，不改原始TEST。
    Require(edit.expected[1]==0x40 || edit.expected[1]==0x41,"audited place register");
    const uint8_t prefix[]{0x48,0x89,static_cast<uint8_t>(edit.expected[1]==0x40 ? 0xC8 : 0xC9)};
    for (bool patched : {false,true}) {
        auto* code=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        Require(code!=nullptr,"allocate isolated probe");
        std::memset(code,0xCC,4096);
        std::memcpy(code,prefix,sizeof(prefix));
        std::memcpy(code+sizeof(prefix),edit.expected.data(),fragmentLength);
        if (patched) code[sizeof(prefix)+edit.operand]=edit.replacement;
        const uint8_t allow[]{0xB0,0x01,0xC3}; // MOV AL,1; RET，表示继续原生入口检查。
        const uint8_t reject[]{0x32,0xC0,0xC3}; // XOR AL,AL; RET，表示地点条件跳过入口。
        std::memcpy(code+sizeof(prefix)+fragmentLength,allow,sizeof(allow));
        std::memcpy(code+sizeof(prefix)+fragmentLength+edit.expected[edit.operand],reject,sizeof(reject));
        DWORD old=0;
        Require(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old)!=FALSE,"protect executable page");
        Require(FlushInstructionCache(GetCurrentProcess(),code,4096)!=FALSE,"flush executable page");
        const auto probe=reinterpret_cast<Probe>(code);
        for(uint32_t flags=0;flags<65536;++flags) {
            std::array<uint8_t,0x60> place{};
            const uint32_t value=flags|0xA5000000;
            std::memcpy(place.data()+0x58,&value,sizeof(value));
            const auto before=place;
            Require(probe(place.data())==(patched || (flags&0x800)!=0),"only location gate is bypassed");
            Require(place==before,"map record remains byte-for-byte unchanged");
        }
        VirtualFree(code,0,MEM_RELEASE);
    }
    }
    for (const auto& edit : sky2party::kFieldEntry119Edits) {
        Require(edit.length==9 && edit.operand==6 && edit.expected[0]==0x80 && edit.expected[1]==0xB8 &&
                edit.expected[6]==0 && edit.replacement==0x80 &&
                (edit.expected[7]==0x7C || edit.expected[7]==0x7D), "audited signed-byte CMP and branch");
        for (bool patched : {false,true}) {
            auto* code=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
            Require(code!=nullptr,"allocate isolated story-gate probe");
            std::memset(code,0xCC,4096);
            const uint8_t prefix[]{0x48,0x89,0xC8}; // MOV RAX,RCX，指向本测试的剧情字节副本。
            std::memcpy(code,prefix,sizeof(prefix));
            std::memcpy(code+sizeof(prefix),edit.expected.data(),edit.length);
            if (patched) code[sizeof(prefix)+edit.operand]=edit.replacement;
            const uint8_t allow[]{0xB0,0x01,0xC3}, reject[]{0x32,0xC0,0xC3};
            const bool jumpAllows=edit.expected[7]==0x7D;
            std::memcpy(code+sizeof(prefix)+edit.length,jumpAllows ? reject : allow,3);
            std::memcpy(code+sizeof(prefix)+edit.length+edit.expected[8],jumpAllows ? allow : reject,3);
            DWORD old=0;
            Require(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old)!=FALSE,"protect story-gate probe");
            Require(FlushInstructionCache(GetCurrentProcess(),code,4096)!=FALSE,"flush story-gate probe");
            const auto probe=reinterpret_cast<Probe>(code);
            for (uint32_t byte=0;byte<256;++byte) {
                std::array<uint8_t,0x110> story{};
                story.fill(0x5A); story[0x10E]=static_cast<uint8_t>(byte);
                const auto before=story;
                Require(probe(story.data())==(patched || (byte&0x80)==0),"only entry119 test is bypassed");
                Require(story==before,"all actual story flags remain unchanged");
            }
            VirtualFree(code,0,MEM_RELEASE);
        }
    }
    std::puts("PASS: 6 original/patched field/HUD gates; 65536 place values or 256 story bytes, no data writes.");
}
