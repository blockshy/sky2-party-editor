// 原生角色名和菜单术语只来自玩家安装的游戏资源，不在插件里手工翻译或内置游戏表。
// 初始化完成后缓存不再改变，返回指针可由渲染线程长期只读使用。
#pragma once
#include "localization.h"
#include <cstdint>
#include <filesystem>

namespace sky2party {
enum class GameTerm : uint8_t {
    PartyMembers, ReserveMembers, Level, Crafts, SCrafts, ChangeFormation, Swap, Count
};

// 调用方必须先校验游戏EXE，再传其所在目录。只在第一次调用时读取各语言的
// t_name/t_text，不扫描其他资源正文，不写游戏。返回值表示八种语言是否全部可用。
bool InitializeGameNames(const std::filesystem::path& validatedGameDirectory) noexcept;
bool GameNamesReady(Language language) noexcept;
// 该语言资源损坏、所需项缺失、ID不在支持范围或尚未初始化时返回nullptr；
// 绝不退到另一语言或旧中文。界面可用本地化“角色ID”说明代替缺失名称。
const char* CharacterNameFor(Language language, uint32_t id) noexcept;
const char* GameTermFor(Language language, GameTerm term) noexcept;
}
