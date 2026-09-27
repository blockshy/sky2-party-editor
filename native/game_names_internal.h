// 有界资源解析器的内部测试接口。只处理文件/字节，不访问原生对象或调用游戏函数。
// 公开CI可用合成FPAC/TBL验证结构边界，不需要原版资源或预生成名称文件。
#pragma once
#include "game_names.h"
#include <array>
#include <string>
#include <vector>

namespace sky2party::game_names_detail {
inline constexpr std::array<uint32_t, 14> kCharacterIds{{0,1,2,3,4,5,6,7,119,100,101,106,107,112}};
inline constexpr std::array<const char*, static_cast<size_t>(GameTerm::Count)> kTermKeys{{
    "TXT_CAMP_PARTY_PARTY_MEMBER", "TXT_CAMP_PARTY_RESERVE_MEMBER", "TXT_CAMP_PARTY_LEVEL",
    "TXT_STEAM_OPT_TEXT_COMMAND_BATTLE_CRAFT", "TXT_CAMP_STATUS_SCRAFTS",
    "TXT_CAMP_TOP_KEY_HELP_FORMATION_TEXT", "TXT_CAMP_PARTY_KEY_HELP_SWAP_TEXT"
}};
struct Catalog {
    std::array<std::string, kCharacterIds.size()> characters;
    std::array<std::string, kTermKeys.size()> terms;
};
struct Tables { std::vector<uint8_t> names, text; };

// 失败时输出保持原样。只读取索引和两份目标表，不把约10MB的整个语言包载入内存。
bool ReadPackage(const std::filesystem::path& path, const std::string& prefix, Tables& output) noexcept;
bool ParseTables(const Tables& tables, Catalog& output) noexcept;
}
