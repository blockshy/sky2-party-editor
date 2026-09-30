// 独立窗口的轻量绘制接口。每个 Mod 静态编入自己的实现；此文件仅描述
// Present 线程中的帧信息与控件，不提供 DLL 加载、动作注册或挂钩服务。
// 字符串均为 UTF-8，指针只在本次调用期间借用；调用方不得跨帧保存 UI 对象。
#pragma once
#include <stdint.h>
#define SKY2_CALL __cdecl

typedef struct Sky2Frame {
    uint32_t size;
    // 尺寸与客户区鼠标坐标一致；显示后端通过 DisplayFramebufferScale
    // 处理客户区和后缓冲尺寸不一致的窗口/DPI 场景。
    float width, height, scale;
    uint64_t time_ms;
    int32_t foreground, panel_open, page_active, controller;
    // 固定页头已经绘制时，Main 不再重复绘制顶部页签。
    int32_t header_drawn;
} Sky2Frame;

// 页面仅在对应 Mod 的绘制上下文中调用这份表。控件 ID 在当前页面内
// 必须稳定且唯一；按钮一次按压仅触发一次，不能替代业务的二次确认。
typedef struct Sky2UiApi {
    uint32_t size;
    void (SKY2_CALL *text)(const char* text);
    void (SKY2_CALL *text_wrapped)(const char* text);
    void (SKY2_CALL *separator)();
    void (SKY2_CALL *same_line)();
    void (SKY2_CALL *spacing)();
    int32_t (SKY2_CALL *button)(const char* id, const char* label);
    int32_t (SKY2_CALL *checkbox)(const char* id, const char* label, int32_t* value);
    int32_t (SKY2_CALL *slider_float)(const char* id, const char* label, float* value, float minimum, float maximum, const char* format);
    int32_t (SKY2_CALL *selectable)(const char* id, const char* label, int32_t selected);
    void (SKY2_CALL *begin_disabled)(int32_t disabled);
    void (SKY2_CALL *end_disabled)();
    int32_t (SKY2_CALL *begin_child)(const char* id, float height);
    void (SKY2_CALL *end_child)();
    int32_t (SKY2_CALL *input_text)(const char* id, const char* label, char* buffer, uint32_t capacity);
    void (SKY2_CALL *text_color)(uint32_t rgba, const char* text);
    // 绘制坐标使用帧的显示坐标，颜色为 R | G<<8 | B<<16 | A<<24。
    // 文本与绘制参数当场复制，函数返回后调用方可释放其缓冲区。
    void (SKY2_CALL *line)(float x1,float y1,float x2,float y2,uint32_t rgba,float thickness);
    void (SKY2_CALL *rect)(float x1,float y1,float x2,float y2,uint32_t rgba,float rounding,float thickness,int32_t filled);
    void (SKY2_CALL *circle)(float x,float y,float radius,uint32_t rgba,float thickness,int32_t filled);
    void (SKY2_CALL *triangle)(float x1,float y1,float x2,float y2,float x3,float y3,uint32_t rgba,float thickness,int32_t filled);
    void (SKY2_CALL *draw_text)(float x,float y,float size,uint32_t rgba,const char* text);
    void (SKY2_CALL *measure_text)(const char* text,float size,float* width,float* height);
    void (SKY2_CALL *push_clip)(float x1,float y1,float x2,float y2);
    void (SKY2_CALL *pop_clip)();
    int32_t (SKY2_CALL *font_covers)(const char* text);
    int32_t (SKY2_CALL *is_any_item_active)(); // 滑块拖动期间只更新内存，结束后再落盘。
    // 布局尺寸使用逻辑像素，由独立窗口统一缩放。卡片、列及裁剪域必须
    // 成对结束；精简测试替身可省略布局回调，由 sky2_ui.hpp 回退到线性布局。
    void (SKY2_CALL *section)(const char* title,const char* description);
    void (SKY2_CALL *begin_card)(const char* id);
    void (SKY2_CALL *end_card)();
    int32_t (SKY2_CALL *columns)(const char* id,float minimum_column_width);
    void (SKY2_CALL *next_column)();
    void (SKY2_CALL *end_columns)();
    int32_t (SKY2_CALL *tab)(const char* id,const char* label,int32_t selected);
    void (SKY2_CALL *status)(const char* text,int32_t tone); // 0 信息、1 成功、2 警告、3 错误。
    void (SKY2_CALL *progress)(float fraction,const char* label);
    int32_t (SKY2_CALL *disclosure)(const char* id,const char* label,int32_t default_open);
    // 一次返回顶部页签选中下标，页面再切换状态。窗口负责 LT/RT 和焦点
    // 范围；这不是页内模式选择器，不能将其加入 Main 的黄色焦点队列。
    int32_t (SKY2_CALL *tab_bar)(const char* id,const char* const* labels,int32_t count,int32_t selected);
    // Main 固定可视宽高，单位为显示像素；不受滚动、卡片或嵌套表格影响。
    // 仅在绘制 Main 时有效，其余时段返回 0；输出地址可为空且不会被保留。
    void (SKY2_CALL *content_size)(float* width,float* height);
} Sky2UiApi;
