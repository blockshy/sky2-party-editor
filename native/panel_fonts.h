// 队伍面板的八语字体适配。仅使用玩家已安装的本机字体，不读取或分发游戏字库。
#pragma once
struct ImGuiIO;

namespace sky2party {
// 仅在专属 ImGui 上下文创建后、首帧之前调用；不清空调用者已有的字体图集。
// Windows 中/日/韩候选各择优一份，合并为20px字库，由新版DX11后端按需栅格化。
void LoadPanelFonts(ImGuiIO& io);

// 必须在本面板的当前 ImGui 上下文和渲染锁下调用，不跨线程访问字体源。
// 查询合并字库的实际 cmap，不用后备问号冒充字形存在，也不生成CJK整张大纹理。
// 非法UTF-8、当前上下文/字体缺失或任一所需字形不存在时返回false；不切换语言。
bool PanelFontCoversText(const char* utf8) noexcept;
}
