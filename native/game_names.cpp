// FPAC和TBL均按已核对的固定布局有界读取，只保留当前Mod使用的14名角色与7个术语。
// 不载入原版脚本、模型或存档，不运行游戏代码，也不把原始资源写入插件分发目录。
#include "game_names_internal.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <type_traits>

namespace sky2party {
namespace game_names_detail {
namespace {
constexpr size_t kMaxTableBytes = 4 * 1024 * 1024;
constexpr size_t kMaxMetadataBytes = 2 * 1024 * 1024;
constexpr uint32_t kMaxEntries = 8192;

template<class T> bool Little(const std::vector<uint8_t>& bytes, size_t offset, T& value) noexcept {
    static_assert(std::is_unsigned_v<T>);
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) return false;
    value = 0;
    for (size_t index = 0; index < sizeof(T); ++index)
        value |= static_cast<T>(bytes[offset + index]) << (8 * index);
    return true;
}
bool ValidUtf8(const std::string& text) noexcept {
    // 拒绝替代解码、过长编码、代理码位、控制字符及超过Unicode范围的值。
    // 角色名原文不作简繁转换、截断、大小写转换或自动清理。
    for (size_t index = 0; index < text.size();) {
        const auto first = static_cast<uint8_t>(text[index++]);
        uint32_t code = first;
        unsigned following = 0;
        if (first < 0x80) { if (first < 0x20 || first == 0x7F) return false; continue; }
        if (first >= 0xC2 && first <= 0xDF) { code = first & 0x1F; following = 1; }
        else if (first >= 0xE0 && first <= 0xEF) { code = first & 0x0F; following = 2; }
        else if (first >= 0xF0 && first <= 0xF4) { code = first & 7; following = 3; }
        else return false;
        if (following > text.size() - index) return false;
        for (unsigned part = 0; part < following; ++part) {
            const auto next = static_cast<uint8_t>(text[index++]);
            if ((next & 0xC0) != 0x80) return false;
            code = (code << 6) | (next & 0x3F);
        }
        if ((following == 1 && code < 0x80) || (following == 2 && code < 0x800) ||
            (following == 3 && code < 0x10000) || code > 0x10FFFF ||
            (code >= 0xD800 && code <= 0xDFFF) || (code >= 0x80 && code <= 0x9F)) return false;
    }
    return true;
}
bool StringAt(const std::vector<uint8_t>& bytes, uint64_t offset, size_t floor, size_t limit,
              std::string& output, bool utf8) {
    if (offset < floor || offset >= bytes.size()) return false;
    const auto start = static_cast<size_t>(offset);
    const auto maximum = (std::min)(limit + 1, bytes.size() - start);
    const auto begin = bytes.begin() + start;
    const auto end = std::find(begin, begin + maximum, uint8_t(0));
    if (end == begin || end == begin + maximum) return false;
    std::string value(begin, end);
    if (utf8 ? !ValidUtf8(value) : std::any_of(value.begin(), value.end(), [](char c) {
            return static_cast<uint8_t>(c) < 0x20 || static_cast<uint8_t>(c) >= 0x7F;
        })) return false;
    output = std::move(value);
    return true;
}
struct TableLayout { uint32_t start = 0, count = 0, stride = 0; size_t pool = 0; };
bool Layout(const std::vector<uint8_t>& bytes, const char* section, uint32_t stride, TableLayout& output) {
    uint32_t sections = 0, start = 0, actualStride = 0, count = 0;
    if (bytes.size() < 88 || bytes.size() > kMaxTableBytes || std::memcmp(bytes.data(), "#TBL", 4) ||
        !Little(bytes, 4, sections) || sections != 1 ||
        !Little(bytes, 76, start) || !Little(bytes, 80, actualStride) || !Little(bytes, 84, count)) return false;
    const auto sectionEnd = std::find(bytes.begin() + 8, bytes.begin() + 72, uint8_t(0));
    if (sectionEnd == bytes.begin() + 72 || std::string(bytes.begin() + 8, sectionEnd) != section ||
        actualStride != stride || count == 0 || count > kMaxEntries || start < 88 || start > bytes.size() ||
        static_cast<uint64_t>(count) * stride > bytes.size() - start) return false;
    output = {start, count, stride, start + static_cast<size_t>(count) * stride};
    return true;
}
bool ParseNames(const std::vector<uint8_t>& bytes, Catalog& output) {
    TableLayout layout{};
    if (!Layout(bytes, "NameTableData", 104, layout)) return false;
    std::array<bool, kCharacterIds.size()> seen{};
    for (uint32_t row = 0; row < layout.count; ++row) {
        const auto offset = layout.start + static_cast<size_t>(row) * layout.stride;
        uint16_t id = 0;
        if (!Little(bytes, offset, id)) return false;
        const auto found = std::find(kCharacterIds.begin(), kCharacterIds.end(), id);
        if (found == kCharacterIds.end()) continue;
        const auto index = static_cast<size_t>(found - kCharacterIds.begin());
        uint64_t pointer = 0;
        if (seen[index] || !Little(bytes, offset + 8, pointer) ||
            !StringAt(bytes, pointer, layout.pool, 128, output.characters[index], true)) return false;
        seen[index] = true;
    }
    return std::all_of(seen.begin(), seen.end(), [](bool value) { return value; });
}
bool ParseText(const std::vector<uint8_t>& bytes, Catalog& output) {
    TableLayout layout{};
    if (!Layout(bytes, "TextTableData", 16, layout)) return false;
    std::array<bool, kTermKeys.size()> seen{};
    for (uint32_t row = 0; row < layout.count; ++row) {
        const auto offset = layout.start + static_cast<size_t>(row) * layout.stride;
        uint64_t keyPointer = 0, textPointer = 0;
        std::string key;
        // TextTableData不是数值ID表：+0和+8分别是稳定键名和本语言文本的字符串偏移。
        // 各语言记录数不同，不能按行号或物理字符串偏移跨语配对。
        if (!Little(bytes, offset, keyPointer) ||
            !StringAt(bytes, keyPointer, layout.pool, 192, key, false)) return false;
        const auto found = std::find_if(kTermKeys.begin(), kTermKeys.end(), [&](const char* value) { return key == value; });
        if (found == kTermKeys.end()) continue;
        const auto index = static_cast<size_t>(found - kTermKeys.begin());
        if (seen[index] || !Little(bytes, offset + 8, textPointer) ||
            !StringAt(bytes, textPointer, layout.pool, 192, output.terms[index], true)) return false;
        seen[index] = true;
    }
    return std::all_of(seen.begin(), seen.end(), [](bool value) { return value; });
}
bool ReadAt(std::ifstream& stream, uint64_t fileSize, uint64_t offset, size_t length,
            std::vector<uint8_t>& output) {
    if (offset > fileSize || length > fileSize - offset || length > kMaxTableBytes ||
        offset > static_cast<uint64_t>((std::numeric_limits<std::streamoff>::max)())) return false;
    std::vector<uint8_t> bytes(length);
    stream.clear(); stream.seekg(static_cast<std::streamoff>(offset));
    if (!stream || !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length))) return false;
    output = std::move(bytes);
    return true;
}
}

bool ParseTables(const Tables& tables, Catalog& output) noexcept {
    try {
        Catalog candidate;
        if (!ParseNames(tables.names, candidate) || !ParseText(tables.text, candidate)) return false;
        output = std::move(candidate);
        return true;
    } catch (...) { return false; }
}
bool ReadPackage(const std::filesystem::path& path, const std::string& prefix, Tables& output) noexcept {
    try {
        // prefix由固定八语映射提供；额外限制内部测试入口，绝不把包内路径当磁盘路径。
        if (prefix.empty() || prefix.size() > 16 || prefix.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string::npos)
            return false;
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) return false;
        const auto end = stream.tellg();
        if (end < 16) return false;
        const auto fileSize = static_cast<uint64_t>(end);
        std::vector<uint8_t> header, metadata;
        if (!ReadAt(stream, fileSize, 0, 16, header) || std::memcmp(header.data(), "FPAC", 4)) return false;
        uint32_t count = 0, firstData = 0, version = 0;
        if (!Little(header, 4, count) || !Little(header, 8, firstData) || !Little(header, 12, version) ||
            version != 1 || count == 0 || count > kMaxEntries || firstData < 16 + uint64_t(count) * 32 ||
            firstData > kMaxMetadataBytes || !ReadAt(stream, fileSize, 0, firstData, metadata)) return false;
        const std::array<std::string, 2> wanted{{prefix + "/t_name.tbl", prefix + "/t_text.tbl"}};
        std::array<uint64_t, 2> offsets{}, sizes{};
        std::array<bool, 2> found{};
        for (uint32_t index = 0; index < count; ++index) {
            const size_t row = 16 + static_cast<size_t>(index) * 32;
            uint64_t namePointer = 0, size = 0, offset = 0;
            std::string name;
            if (!Little(metadata, row + 8, namePointer) || !Little(metadata, row + 16, size) ||
                !Little(metadata, row + 24, offset) || offset < firstData || offset > fileSize || size > fileSize - offset ||
                !StringAt(metadata, namePointer, 16 + static_cast<size_t>(count) * 32, 256, name, false)) return false;
            for (size_t type = 0; type < wanted.size(); ++type) {
                if (name != wanted[type]) continue;
                if (found[type] || size < 88 || size > kMaxTableBytes) return false;
                found[type] = true; offsets[type] = offset; sizes[type] = size;
            }
        }
        if (!found[0] || !found[1] ||
            (offsets[0] < offsets[1] + sizes[1] && offsets[1] < offsets[0] + sizes[0])) return false;
        Tables candidate;
        if (!ReadAt(stream, fileSize, offsets[0], static_cast<size_t>(sizes[0]), candidate.names) ||
            !ReadAt(stream, fileSize, offsets[1], static_cast<size_t>(sizes[1]), candidate.text)) return false;
        output = std::move(candidate);
        return true;
    } catch (...) { return false; }
}
}

namespace {
std::once_flag initialization;
std::array<game_names_detail::Catalog, kLanguageCount> catalogs;
std::array<bool, kLanguageCount> available{};
std::atomic<bool> published{false};
}
bool InitializeGameNames(const std::filesystem::path& directory) noexcept {
    try {
        std::call_once(initialization, [&] {
            constexpr std::array<const char*, kLanguageCount> packages{{
                "table_sc", "table", "table_en", "table_tc", "table_de", "table_fr", "table_es", "table_ko"
            }};
            for (size_t language = 0; language < packages.size(); ++language) {
                game_names_detail::Tables tables;
                const std::string package = packages[language];
                available[language] = game_names_detail::ReadPackage(directory / "pac" / "steam" / (package + ".pac"), package, tables) &&
                    game_names_detail::ParseTables(tables, catalogs[language]);
            }
            // release/acquire发布整套不可变缓存；在此之前渲染层只会得到nullptr。
            published.store(true, std::memory_order_release);
        });
        return std::all_of(available.begin(), available.end(), [](bool value) { return value; });
    } catch (...) { return false; }
}
bool GameNamesReady(Language language) noexcept {
    const auto index = static_cast<size_t>(language);
    return published.load(std::memory_order_acquire) && index < available.size() && available[index];
}
const char* CharacterNameFor(Language language, uint32_t id) noexcept {
    if (!GameNamesReady(language)) return nullptr;
    const auto& ids = game_names_detail::kCharacterIds;
    const auto found = std::find(ids.begin(), ids.end(), id);
    if (found == ids.end()) return nullptr;
    return catalogs[static_cast<size_t>(language)].characters[static_cast<size_t>(found - ids.begin())].c_str();
}
const char* GameTermFor(Language language, GameTerm term) noexcept {
    const auto index = static_cast<size_t>(term);
    if (!GameNamesReady(language) || index >= static_cast<size_t>(GameTerm::Count)) return nullptr;
    return catalogs[static_cast<size_t>(language)].terms[index].c_str();
}
}
