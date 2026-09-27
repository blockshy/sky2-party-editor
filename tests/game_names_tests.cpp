// 原生名称读取的公开合成回归：自建最小FPAC/TBL，不复制/分发游戏表，不打开游戏。
// 验证ID/键配对、UTF-8与范围边界、整语失败及一次发布后的指针稳定性。
#include "game_names_internal.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

using namespace sky2party;
using namespace sky2party::game_names_detail;
namespace {
int checks = 0, failed = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { ++failed; std::fprintf(stderr, "FAILED: %s\n", message); }
}
template<class T> void Put(std::vector<uint8_t>& bytes, size_t offset, T value) {
    for (size_t i = 0; i < sizeof(T); ++i) bytes.at(offset + i) = static_cast<uint8_t>(value >> (8 * i));
}
uint64_t Get64(const std::vector<uint8_t>& bytes, size_t offset) {
    uint64_t result = 0;
    for (size_t i = 0; i < 8; ++i) result |= uint64_t(bytes.at(offset + i)) << (8 * i);
    return result;
}
uint64_t Append(std::vector<uint8_t>& bytes, const std::string& text) {
    const auto start = bytes.size(); bytes.insert(bytes.end(), text.begin(), text.end()); bytes.push_back(0); return start;
}
std::vector<uint8_t> Table(const char* name, uint32_t stride, uint32_t count) {
    std::vector<uint8_t> bytes(88 + static_cast<size_t>(stride) * count);
    std::memcpy(bytes.data(), "#TBL", 4); Put<uint32_t>(bytes, 4, 1);
    std::memcpy(bytes.data() + 8, name, std::strlen(name));
    Put<uint32_t>(bytes, 76, 88); Put(bytes, 80, stride); Put(bytes, 84, count);
    return bytes;
}
std::string Role(unsigned language, uint32_t id) { return "role-" + std::to_string(language) + "-" + std::to_string(id) + u8"名한α"; }
std::string Term(unsigned language, size_t index) { return "term-" + std::to_string(language) + "-" + std::to_string(index); }
Tables Fixture(unsigned language = 0) {
    Tables tables;
    tables.names = Table("NameTableData", 104, static_cast<uint32_t>(kCharacterIds.size()));
    tables.text = Table("TextTableData", 16, static_cast<uint32_t>(kTermKeys.size()));
    // 故意反转记录顺序，防止解析器按物理行号把人物或术语配错。
    for (size_t row = 0; row < kCharacterIds.size(); ++row) {
        const auto id = kCharacterIds[kCharacterIds.size() - row - 1];
        Put<uint16_t>(tables.names, 88 + row * 104, static_cast<uint16_t>(id));
        const auto pointer = Append(tables.names, Role(language, id));
        Put(tables.names, 88 + row * 104 + 8, pointer);
    }
    for (size_t row = 0; row < kTermKeys.size(); ++row) {
        const auto index = kTermKeys.size() - row - 1;
        auto pointer = Append(tables.text, kTermKeys[index]); Put(tables.text, 88 + row * 16, pointer);
        pointer = Append(tables.text, Term(language, index)); Put(tables.text, 88 + row * 16 + 8, pointer);
    }
    return tables;
}
std::vector<uint8_t> Package(const Tables& tables, const std::string& prefix = "table_sc") {
    std::vector<uint8_t> bytes(80); // header16 + 两条32字节索引。
    std::memcpy(bytes.data(), "FPAC", 4); Put<uint32_t>(bytes, 4, 2); Put<uint32_t>(bytes, 12, 1);
    const auto name = Append(bytes, prefix + "/t_name.tbl"), text = Append(bytes, prefix + "/t_text.tbl");
    const auto start = bytes.size(); Put<uint32_t>(bytes, 8, static_cast<uint32_t>(start));
    Put(bytes, 24, name); Put<uint64_t>(bytes, 32, tables.names.size()); Put<uint64_t>(bytes, 40, start);
    Put(bytes, 56, text); Put<uint64_t>(bytes, 64, tables.text.size()); Put<uint64_t>(bytes, 72, start + tables.names.size());
    bytes.insert(bytes.end(), tables.names.begin(), tables.names.end());
    bytes.insert(bytes.end(), tables.text.begin(), tables.text.end());
    return bytes;
}
void Reject(const Tables& tables, const char* message) {
    Catalog sentinel;
    sentinel.characters.fill("unchanged-name"); sentinel.terms.fill("unchanged-term");
    const auto before = sentinel;
    Check(!ParseTables(tables, sentinel), message);
    Check(sentinel.characters == before.characters && sentinel.terms == before.terms,
          "解析失败不能发布半份角色名或覆盖已有输出");
}
struct Files {
    std::filesystem::path root;
    std::vector<std::filesystem::path> created;
    Files() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() / ("sky2-party-name-tests-" + std::to_string(stamp));
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("test directory collision");
        std::filesystem::create_directories(root / "pac" / "steam");
    }
    void Write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
        // 所有传入文件都由本测试从root拼接，名称不来自资源表或玩家输入。
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) throw std::runtime_error("fixture write failed");
        if (std::find(created.begin(), created.end(), path) == created.end()) created.push_back(path);
    }
    ~Files() {
        // 只清理明确创建过的文件及已空的已知目录，不递归删除临时根目录。
        std::error_code error;
        for (const auto& file : created) std::filesystem::remove(file, error);
        std::filesystem::remove(root / "pac" / "steam", error);
        std::filesystem::remove(root / "pac", error);
        std::filesystem::remove(root, error);
    }
};
}

int main() {
    Check(!GameNamesReady(Language::Chinese) && !CharacterNameFor(Language::Chinese, 0), "初始化前不泄漏默认中文名称");
    for (unsigned language = 0; language < kLanguageCount; ++language) {
        Catalog catalog;
        Check(ParseTables(Fixture(language), catalog), "八语合成UTF-8原文可解析");
        for (size_t index = 0; index < kCharacterIds.size(); ++index)
            Check(catalog.characters[index] == Role(language, kCharacterIds[index]), "角色按稳定ID配对且原文字节保持");
        for (size_t index = 0; index < kTermKeys.size(); ++index)
            Check(catalog.terms[index] == Term(language, index), "术语按稳定字符串键配对");
    }
    for (bool names : {false, true}) {
        for (size_t truncated : {size_t(0),size_t(4),size_t(8),size_t(72),size_t(87)}) {
            auto fixture = Fixture(); (names ? fixture.names : fixture.text).resize(truncated);
            Reject(fixture, "表头截断必须拒绝");
        }
        for (const auto offset : {size_t(0),size_t(4),size_t(8),size_t(76),size_t(80),size_t(84)}) {
            auto fixture = Fixture(); auto& bytes = names ? fixture.names : fixture.text;
            Put<uint32_t>(bytes, offset, 0xFFFFFFFF);
            Reject(fixture, "魔数/节名/节数/记录起点/跨度/数量改变必须拒绝");
        }
        auto fixture = Fixture(); auto& bytes = names ? fixture.names : fixture.text;
        const auto rowPointer = size_t(88 + 8);
        for (uint64_t bad : {uint64_t(0),uint64_t(88),uint64_t(bytes.size()),uint64_t(0xFFFFFFFFFFFFFFFFull)}) {
            auto changed = fixture;
            Put(names ? changed.names : changed.text, rowPointer, bad);
            Reject(changed, "名称必须在记录后的字符串池内，不能越界或伪指向表头");
        }
    }
    {
        auto fixture = Fixture(); Put<uint16_t>(fixture.names, 88, 0);
        Reject(fixture, "重复角色ID或缺失目标角色不得任选第一条");
    }
    {
        auto fixture = Fixture(); Put(fixture.text, 88, Get64(fixture.text, 104));
        Reject(fixture, "重复术语键或缺少目标术语不得静默通过");
    }
    const std::vector<std::string> invalid{{""},{"bad\nname"},{"\xC0\xAF",2},{"\xED\xA0\x80",3},
        {"\xF4\x90\x80\x80",4},{"\xE4\x80",2},{"\x80",1},{"\xC2\x85",2},std::string(193,'x')};
    for (const auto& value : invalid) {
        for (bool names : {false,true}) {
            auto fixture = Fixture(); auto& bytes = names ? fixture.names : fixture.text;
            Put(bytes, 96, Append(bytes, value));
            Reject(fixture, "空值/控制符/坏UTF-8/超长原文必须拒绝，不用替代字符伪造名称");
        }
    }
    {
        auto fixture = Fixture(); fixture.names.pop_back(); Reject(fixture, "最后一个名称缺少NUL不能越界读取");
    }

    Files files;
    const auto path = files.root / "fixture.pac";
    const auto original = Fixture(); const auto good = Package(original);
    files.Write(path, good);
    Tables loaded;
    Check(ReadPackage(path, "table_sc", loaded) && loaded.names == original.names && loaded.text == original.text,
        "FPAC按两条资源名提取精确表字节");
    Check(!ReadPackage(path, "../escape", loaded), "资源前缀不能成为任意文件路径");
    for (unsigned corruption = 0; corruption < 13; ++corruption) {
        auto bytes = good;
        if (corruption == 0) bytes.resize(15);
        if (corruption == 1) bytes[0] = 0;
        if (corruption == 2) Put<uint32_t>(bytes, 4, 8193);
        if (corruption == 3) Put<uint32_t>(bytes, 12, 2);
        if (corruption == 4) Put<uint32_t>(bytes, 8, 20);
        if (corruption == 5) Put<uint32_t>(bytes, 8, 2 * 1024 * 1024 + 1);
        if (corruption == 6) Put<uint64_t>(bytes, 24, 0);
        if (corruption == 7) Put<uint64_t>(bytes, 32, 4 * 1024 * 1024 + 1);
        if (corruption == 8) Put<uint64_t>(bytes, 40, bytes.size());
        if (corruption == 9) Put<uint64_t>(bytes, 56, Get64(bytes,24));
        if (corruption == 10) Put<uint64_t>(bytes,72,Get64(bytes,40));
        if (corruption == 11) bytes[80] = 'x';
        if (corruption == 12) bytes.pop_back();
        files.Write(path, bytes); Tables unchanged{{1,2,3},{4,5,6}};
        Check(!ReadPackage(path, "table_sc", unchanged), "FPAC损坏索引/重复项/重叠载荷/缺失资源必须拒绝");
        Check(unchanged.names == std::vector<uint8_t>({1,2,3}) && unchanged.text == std::vector<uint8_t>({4,5,6}),
            "包读取失败不能留下部分载荷");
    }
    constexpr std::array<const char*, kLanguageCount> packages{{
        "table_sc","table","table_en","table_tc","table_de","table_fr","table_es","table_ko"
    }};
    for (unsigned language = 0; language < kLanguageCount - 1; ++language)
        files.Write(files.root / "pac" / "steam" / (std::string(packages[language]) + ".pac"), Package(Fixture(language), packages[language]));
    Check(!InitializeGameNames(files.root), "缺少某语包时报告非全八语成功");
    for (unsigned language = 0; language < kLanguageCount - 1; ++language) {
        const auto selection = static_cast<Language>(language);
        Check(GameNamesReady(selection), "其他完整语言独立可用");
        Check(std::string(CharacterNameFor(selection, 119)) == Role(language, 119), "选择的语言返回该语精确角色名");
        Check(std::string(GameTermFor(selection, GameTerm::Level)) == Term(language, 2), "等级术语不混入别种语言");
    }
    Check(!GameNamesReady(Language::Korean) && !CharacterNameFor(Language::Korean, 0) && !GameTermFor(Language::Korean, GameTerm::Level),
        "失败语言返回nullptr，不回退成英文或旧中文");
    Check(!GameNamesReady(static_cast<Language>(255)) && !CharacterNameFor(Language::Chinese, 999) &&
        !GameTermFor(Language::Chinese, GameTerm::Count), "未知语言/角色/术语不会越界");
    const auto pointer = CharacterNameFor(Language::English, 0);
    files.Write(files.root / "pac" / "steam" / "table_ko.pac", Package(Fixture(7), "table_ko"));
    Check(!InitializeGameNames(files.root) && !GameNamesReady(Language::Korean) && pointer == CharacterNameFor(Language::English, 0),
        "初始化只执行一次，后续文件改变不会使已发布指针失效");
    std::printf("%d game-name parser and cache checks, %d failures\n", checks, failed);
    return failed ? 1 : 0;
}
