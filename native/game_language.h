// 原生文本语言的只读适配层。与游戏语音、Steam 客户端或 Windows 区域设置分离，
// 避免玩家使用日语语音、中文文本时，Mod 被错误切换到日语界面。
#pragma once
#include "localization.h"
#include <cstdint>

namespace sky2party {

// 原生资源语言表（受支持 EXE 的 RVA 0xAA5F30）按以下顺序排列：
// jp、en、de、fr、es、tc、sc、ko。不要把 Mod 自身的 Language 枚举当成原生编号。
// 八种有效游戏语言均有独立界面与名称资源，不把繁体中文或其他语言合并到回退项。
// 返回 false 表示无效/未初始化的数据，此时保持 output 原值，由调用方保留上一语言。
inline bool ResolveNativeTextLanguage(uint8_t native, Language& output) noexcept {
    switch (native) {
    case 0: output = Language::Japanese; return true;
    case 1: output = Language::English; return true;
    case 2: output = Language::German; return true;
    case 3: output = Language::French; return true;
    case 4: output = Language::Spanish; return true;
    case 5: output = Language::TraditionalChinese; return true;
    case 6: output = Language::Chinese; return true;
    case 7: output = Language::Korean; return true;
    default: return false;
    }
}

// 只能在完整 EXE 哈希校验成功后传入游戏模块基址；传入 0 停止检测。
// 本函数不安装挂钩、不写游戏内存，也不读取或修改存档。初次初始化会立即尝试检测。
void InitializeGameLanguage(uintptr_t validatedBase) noexcept;

// 推荐在每次绘制 Mod 窗口之前调用，使同一帧的界面和游戏地点名称采用相同语言。
// 内部最多每 250 毫秒读取一次；支持多个调用线程，读取失败时保留最后一次有效结果。
void RefreshGameLanguage() noexcept;

} // namespace sky2party
