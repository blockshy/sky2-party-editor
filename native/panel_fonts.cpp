// 新版 ImGui 的字体源允许按需加载字形。这里不调用 Build/GetTexData，也不传入
// GetGlyphRangesChineseFull 等全量区间，避免启动时栅格化数万字的巨大纹理。
#include "panel_fonts.h"
#include "runtime.h"
#include <imgui.h>
#include <array>
#include <cstdint>
#include <string>

namespace sky2party {
namespace {
std::array<uint8_t, 0x10000> glyphCoverage{}; // 0未查、1存在、2缺失；BMP字形只查询一次。
ImFont* coverageFont = nullptr;
int coverageSourceCount = 0;

void ResetCoverage() noexcept {
    glyphCoverage.fill(0);
    coverageFont = nullptr;
    coverageSourceCount = 0;
}

bool HasCodepoint(ImFont* font, uint32_t codepoint) {
    // 换行/制表符不需要可见字形。成对UTF-16代理项已在调用方合成为真正码点；
    // 超出当前ImWchar容量时明确报告缺字，不能截断后误认为另一个BMP字符存在。
    if (codepoint < 0x20 || codepoint == 0x7F) return true;
    if (codepoint > IM_UNICODE_CODEPOINT_MAX) return false;
    if (codepoint >= glyphCoverage.size()) return font->IsGlyphInFont(static_cast<ImWchar>(codepoint));
    auto& cached = glyphCoverage[codepoint];
    if (!cached) cached = font->IsGlyphInFont(static_cast<ImWchar>(codepoint)) ? 1 : 2;
    return cached == 1;
}
}

void LoadPanelFonts(ImGuiIO& io) {
    ResetCoverage();
    if (!io.Fonts || io.Fonts->Locked) {
        Log("Party panel fonts unavailable: atlas missing or still locked.");
        return;
    }
    wchar_t windows[MAX_PATH]{};
    const auto length = GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring folder = length && length < MAX_PATH ?
        std::wstring(windows, length) + L"\\Fonts\\" : std::wstring{};
    bool loaded = io.Fonts->Fonts.Size != 0;
    const auto load = [&](const wchar_t* filename) {
        if (folder.empty()) return false;
        const auto path = folder + filename;
        WIN32_FILE_ATTRIBUTE_DATA information{};
        // AddFontFromFileTTF 默认会为不存在/不可读文件报断言：先确认候选是实际
        // 存在的字体文件，再用NoLoadError覆盖检查后文件消失或读权限改变的情况。
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &information) ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (information.nFileSizeHigh == 0 && information.nFileSizeLow <= 100)) return false;
        char utf8[MAX_PATH * 3]{};
        if (!WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8, sizeof(utf8), nullptr, nullptr)) return false;
        ImFontConfig config;
        config.Flags |= ImFontFlags_NoLoadError;
        config.MergeMode = loaded;
        // 字形区间保留nullptr：项目锁定的ImGui/DX11后端支持动态纹理更新，任何
        // 实际显示的本机字形均按需加载，不必为语言切换重新构建字库或列表状态。
        if (!io.Fonts->AddFontFromFileTTF(utf8, 20.0f, &config, nullptr)) return false;
        loaded = true;
        return true;
    };
    if (!load(L"segoeui.ttf") && !loaded) {
        // 即使系统字体目录异常，也保留内置拉丁字体，让缺字提示能够正常显示。
        ImFontConfig config;
        config.SizePixels = 20.0f;
        loaded = io.Fonts->AddFontDefault(&config) != nullptr;
    }
    load(L"seguisym.ttf"); // 补充方向符号/圈号，不能假定英文界面只有ASCII。
    bool chinese = false, japanese = false, korean = false;
    for (const auto* name : {L"msyh.ttc", L"msyh.ttf", L"simhei.ttf", L"simsun.ttc",
            L"Deng.ttf", L"msjh.ttc", L"mingliu.ttc", L"NotoSansSC-Regular.ttf"})
        if (load(name)) { chinese = true; break; }
    for (const auto* name : {L"YuGothM.ttc", L"YuGothR.ttc", L"meiryo.ttc", L"msgothic.ttc", L"NotoSansJP-Regular.ttf"})
        if (load(name)) { japanese = true; break; }
    for (const auto* name : {L"malgun.ttf", L"gulim.ttc", L"batang.ttc", L"NotoSansKR-Regular.ttf"})
        if (load(name)) { korean = true; break; }
    if (!chinese || !japanese || !korean)
        for (const auto* name : {L"NotoSansCJK-Regular.ttc", L"arialuni.ttf"})
            if (load(name)) break;
    if (!loaded) Log("Party panel fonts unavailable: no usable font source.");
    // 候选文件加载成功不等于覆盖完整语言；实际文案/原生名由CoversText逐项核验。
    if (io.Fonts->Fonts.Size && !io.FontDefault) io.FontDefault = io.Fonts->Fonts[0];
}

bool PanelFontCoversText(const char* utf8) noexcept {
    try {
        if (!utf8 || !ImGui::GetCurrentContext()) return false;
        auto& io = ImGui::GetIO();
        ImFont* font = io.FontDefault ? io.FontDefault :
            (io.Fonts && io.Fonts->Fonts.Size ? io.Fonts->Fonts[0] : nullptr);
        if (!font || !font->IsLoaded()) return false;
        if (coverageFont != font || coverageSourceCount != font->Sources.Size) {
            ResetCoverage();
            coverageFont = font;
            coverageSourceCount = font->Sources.Size;
        }
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
        if (!count) return false;
        std::wstring wide(static_cast<size_t>(count), L'\0');
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide.data(), count)) return false;
        for (size_t index = 0; index + 1 < wide.size(); ++index) {
            uint32_t codepoint = static_cast<uint16_t>(wide[index]);
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                if (index + 2 >= wide.size()) return false;
                const auto low = static_cast<uint16_t>(wide[++index]);
                if (low < 0xDC00 || low > 0xDFFF) return false;
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
            } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) return false;
            if (!HasCodepoint(font, codepoint)) return false;
        }
        return true;
    } catch (...) { return false; }
}
}
