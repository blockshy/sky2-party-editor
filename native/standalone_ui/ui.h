// 三个独立 Mod 共用的窗口组件，以相同版本随各仓库源码分发并静态编译。
// 每个 Mod 独立管理图形上下文，业务页通过轻量绘制表统一布局和操作行为。
#pragma once
#include "sky2_ui.h"
#include <Windows.h>
#include <Xinput.h>
#include <imgui.h>

namespace sky2solo {
struct WindowState {
    int section = 0;
    bool resetFocus = true;
    ImVec2 position{};
    bool positioned = false, dragging = false, editing = false;
    ImVec2 dragOffset{};
};
struct WindowSpec {
    const char* id = nullptr;
    const char* title = nullptr;
    const char* description = nullptr;
    const char* const* sections = nullptr;
    int sectionCount = 0;
    void* user = nullptr;
    // Header 只放页签等固定内容；Main 回调内的控件才进入黄色焦点候选。
    void (*header)(void*, const Sky2Frame&, int) = nullptr;
    void (*draw)(void*, const Sky2Frame&, int) = nullptr;
    // 切侧栏时同步撤销旧页确认，之后才绘制新页；不得直接执行游戏写入。
    void (*changed)(void*, int) = nullptr;
    int language = 0; // 0 简中、1 繁中、2 日、3 英、4 德、5 法、6 西、7 韩。
};
const Sky2UiApi* UiApi();
void ConfigureTheme();
// 在后端 NewFrame 之后、ImGui::NewFrame 之前传入游戏 IAT 所见样本。必须禁用
// Win32 后端另行轮询 XInput，避免 Steam 映射设备与物理设备混用。
void FeedGamepad(const XINPUT_GAMEPAD* pad, bool enabled);
// 使用物理客户区坐标；style.FontScaleDpi 与 frame.scale 应一致。调用方负责
// ImGui 上下文、前台/健康帧判断及消息队列。返回 false 表示用户请求关闭。
bool DrawWindow(WindowState&, const WindowSpec&, const Sky2Frame&);
// 仅由壳在 Main 范围内设置，用于业务列表按可用高度布局。
void SetContentViewport(ImVec2 size);
}
