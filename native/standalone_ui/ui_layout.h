// 独立布局组件仅在 Present 线程使用；不读游戏、不写配置，也不持有业务数据。
#pragma once
#include <cstdint>
namespace sky2solo::layout {
void Section(const char* title,const char* description);
void BeginCard(const char* id);
void EndCard();
int32_t Columns(const char* id,float minimumWidth);
void NextColumn();
void EndColumns();
int32_t Tab(const char* id,const char* label,int32_t selected);
void Status(const char* text,int32_t tone);
void Progress(float fraction,const char* label);
int32_t Disclosure(const char* id,const char* label,int32_t defaultOpen);
bool WrappedButton(const char* label,float width=0);
void Muted(const char* text);
void Heading(const char* text,float factor=1.3f);
void Theme();
}
