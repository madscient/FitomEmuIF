// test/engine_relay_test.cpp
// 部位ゲインと外部メモリが FmEngineApi のエンジンへ中継されることを確かめる。
//
//   FitomEmuIF_relay_test <mode> <エンジン DLL のパス>
//
// mode:
//   current     現行の FmEngineApi のエンジン（test/stub_engine.cpp）
//   legacy      部位と外部メモリを番号で指定する版のエンジン。
//               検証用エンジンの STUB_LEGACY 版のほか、その版の実エンジンも渡せる
//               （OPNA / OPL3 / OPM を持つもの）
//   incomplete  任意の組の一部を欠くエンジン。HWPlugin_Init が失敗すること
//
// エンジン DLL は FitomEmuIF と同じディレクトリに置く（FitomEmuIF はファイル名で探す）。
// HWPlugin_Init がオーディオストリームを開くので、出力デバイスが要る。
// PluginRegistry はプロセスに 1 つなので、mode ごとにプロセスを分けて実行する。

#include "fitom/IHWPlugin.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
static void* engine_sym(const char* dll_path, const char* name) {
    HMODULE h = LoadLibraryA(dll_path);
    return h ? reinterpret_cast<void*>(GetProcAddress(h, name)) : nullptr;
}
#else
#  include <dlfcn.h>
static void* engine_sym(const char* dll_path, const char* name) {
    void* h = dlopen(dll_path, RTLD_LAZY | RTLD_LOCAL);
    return h ? dlsym(h, name) : nullptr;
}
#endif

namespace fs = std::filesystem;

static int g_pass = 0;
static int g_fail = 0;

// assert は Release ビルドで消えるので使わない
#define CHECK(cond) \
    do { \
        if (cond) { ++g_pass; } \
        else { ++g_fail; std::printf("[FAIL] line %d: %s\n", __LINE__, #cond); } \
    } while (0)

// ── 記録の読み取り ───────────────────────────────────────────────────────────

static std::vector<std::string> split_lines(const char* text) {
    std::vector<std::string> lines;
    std::string cur;
    for (const char* p = text; p && *p; ++p) {
        if (*p == '\n') { lines.push_back(cur); cur.clear(); }
        else cur += *p;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

static bool has_line(const std::vector<std::string>& lines, const std::string& line) {
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

static int count_prefix(const std::vector<std::string>& lines, const std::string& prefix) {
    int n = 0;
    for (const auto& l : lines)
        if (l.compare(0, prefix.size(), prefix) == 0) ++n;
    return n;
}

// ── テスト用のプロファイルとカタログ ─────────────────────────────────────────

struct ImageFile { const char* catalog_key; const char* file; uint8_t first; int size; };

// 取り違えを見分けられるよう、キーごとに先頭バイトと大きさを変える。
// バイト i の値は first + i。
static const ImageFile kImages[] = {
    { "ADPCM-A",      "adpcm_a.bin",      0xA0, 5 },
    { "ADPCM-B",      "adpcm_b.bin",      0xB0, 3 },
    { "OPNA_RHYTHM",  "opna_rhythm.bin",  0xC0, 2 },
    { "OPNB_ADPCM-B", "opnb_adpcm_b.bin", 0xD0, 4 },
    { "OPL4AWM",      "opl4_awm.bin",     0xE0, 6 },
    { "SSGS_ADPCM",   "ssgs_adpcm.bin",   0xF0, 7 },
};

static const char* const kProfileFile = "profile.json";
static const char* const kCatalogFile = "catalog.json";

static void write_files(const fs::path& dir, const std::string& engine_file,
                        const std::string& chips_json)
{
    fs::create_directories(dir);

    for (const auto& img : kImages) {
        std::ofstream ofs(dir / img.file, std::ios::binary);
        for (int i = 0; i < img.size; ++i)
            ofs.put(static_cast<char>(img.first + i));
    }
    {
        std::ofstream ofs(dir / kCatalogFile);
        ofs << "{ \"images\": {";
        bool first = true;
        for (const auto& img : kImages) {
            ofs << (first ? "" : ",") << "\n  \"" << img.catalog_key << "\": \"" << img.file << "\"";
            first = false;
        }
        ofs << "\n} }\n";
    }
    {
        std::ofstream ofs(dir / kProfileFile);
        ofs << "{\n"
            << "  \"sample_rate\": 44100,\n"
            << "  \"buffer_frames\": 512,\n"
            << "  \"pcm_catalog\": \"" << kCatalogFile << "\",\n"
            << "  \"engines\": [ { \"dll\": \"" << engine_file << "\", \"chips\": [\n"
            << chips_json
            << "  ] } ]\n"
            << "}\n";
    }
}

static void remove_files(const fs::path& dir) {
    std::error_code ec;
    for (const auto& img : kImages) fs::remove(dir / img.file, ec);
    fs::remove(dir / kCatalogFile, ec);
    fs::remove(dir / kProfileFile, ec);
    fs::remove(dir, ec); // 空のときだけ消える
}

static HWHandle open_chip(const std::string& engine, const char* chip, int index) {
    std::string params = std::string("{\"type\":\"FMHWIF\",\"engine\":\"") + engine
        + "\",\"chip\":\"" + chip + "\",\"index\":" + std::to_string(index) + "}";
    HWHandle h = nullptr;
    if (HWPlugin_Open(params.c_str(), &h) != HW_OK) return nullptr;
    return h;
}

static std::vector<std::string> part_names(HWHandle h) {
    std::vector<std::string> names;
    const uint32_t n = HWPlugin_GetPartCount(h);
    for (uint32_t i = 0; i < n; ++i) {
        const char* name = HWPlugin_GetPartName(h, i);
        names.push_back(name ? name : "(null)");
    }
    std::sort(names.begin(), names.end());
    return names;
}

static bool gain_is(HWHandle h, const char* part, float l, float r) {
    float gl = -1.f, gr = -1.f;
    if (HWPlugin_GetPartGain(h, part, &gl, &gr) != HW_OK) return false;
    return gl == l && gr == r;
}

// ── mode: current ────────────────────────────────────────────────────────────

static const char* const kCurrentChips =
    "    { \"chip\": \"OPNA\",  \"clock\": 7987200 },\n"   // chip_id 0
    "    { \"chip\": \"OPNB\",  \"clock\": 8000000 },\n"   // 1
    "    { \"chip\": \"OPL3\",  \"clock\": 14318180 },\n"  // 2
    "    { \"chip\": \"OPL4\",  \"clock\": 33868800 },\n"  // 3
    "    { \"chip\": \"Y8950\", \"clock\": 3579545 },\n"   // 4
    "    { \"chip\": \"SSGS\",  \"clock\": 4000000 },\n"   // 5
    "    { \"chip\": \"OPM\",   \"clock\": 3579545 },\n"   // 6
    "    { \"chip\": \"OPNA\",  \"clock\": 7987200 }\n";   // 7

static void test_current(const fs::path& profile, const std::string& engine,
                         const char* (*get_log)())
{
    CHECK(HWPlugin_Init(profile.string().c_str()) == HW_OK);
    CHECK(get_log != nullptr);
    if (g_fail || !get_log) return;

    // ── 外部メモリ ──
    {
        auto log = split_lines(get_log());
        // テストが読んでいる記録が、FitomEmuIF がロードしたエンジンのものであること
        CHECK(has_line(log, "AddChip chip=7 name=OPNA"));

        CHECK(has_line(log, "SetMemory chip=0 memory=RHYTHM size=2 first=C0 last=C1"));
        CHECK(has_line(log, "SetMemory chip=0 memory=ADPCM_B size=3 first=B0 last=B2"));
        CHECK(has_line(log, "SetMemory chip=1 memory=ADPCM_B size=4 first=D0 last=D3"));
        CHECK(has_line(log, "SetMemory chip=1 memory=ADPCM_A size=5 first=A0 last=A4"));
        CHECK(has_line(log, "SetMemory chip=3 memory=PCM size=6 first=E0 last=E5"));
        CHECK(has_line(log, "SetMemory chip=4 memory=ADPCM_B size=3 first=B0 last=B2"));
        CHECK(has_line(log, "SetMemory chip=7 memory=RHYTHM size=2 first=C0 last=C1"));
        CHECK(has_line(log, "SetMemory chip=7 memory=ADPCM_B size=3 first=B0 last=B2"));
        // 上の 8 件のほかには渡していない（ADPCM_B_ROMMODE と SSGS のメモリを含む）
        CHECK(count_prefix(log, "SetMemory ") == 8);
        CHECK(count_prefix(log, "SetMemory chip=5 ") == 0);
        // Init の時点では部位ゲインを書かない
        CHECK(count_prefix(log, "SetPartGain ") == 0);
    }

    // ── 部位の列挙と既定値 ──
    HWHandle opna0 = open_chip(engine, "OPNA", 0);
    HWHandle opna1 = open_chip(engine, "OPNA", 1);
    HWHandle opl3  = open_chip(engine, "OPL3", 0);
    HWHandle opl4  = open_chip(engine, "OPL4", 0);
    HWHandle opm   = open_chip(engine, "OPM",  0);
    CHECK(opna0 && opna1 && opl3 && opl4 && opm);
    if (g_fail) return;

    CHECK(part_names(opna0) == (std::vector<std::string>{ "FM", "SSG" }));
    CHECK(part_names(opl3)  == (std::vector<std::string>{ "AB", "CD" }));
    CHECK(part_names(opl4)  == (std::vector<std::string>{ "DO0", "DO1", "DO2" }));
    CHECK(HWPlugin_GetPartCount(opm) == 0);
    CHECK(HWPlugin_GetPartName(opm, 0) == nullptr);
    CHECK(HWPlugin_GetPartName(opna0, 2) == nullptr);

    CHECK(gain_is(opna0, "FM",  1.f, 1.f));
    CHECK(gain_is(opna0, "SSG", 1.f, 1.f));
    CHECK(gain_is(opl3,  "AB",  1.f, 1.f));
    CHECK(gain_is(opl3,  "CD",  0.f, 0.f));

    // ── 設定と読み戻し ──
    CHECK(HWPlugin_SetPartGain(opna0, "SSG", 0.25f, 0.5f) == HW_OK);
    CHECK(gain_is(opna0, "SSG", 0.25f, 0.5f));
    CHECK(gain_is(opna0, "FM",  1.f, 1.f));    // 他の部位は変わらない
    CHECK(gain_is(opna1, "SSG", 1.f, 1.f));    // 同種の別チップは変わらない
    CHECK(has_line(split_lines(get_log()), "SetPartGain chip=0 part=SSG l=0.25 r=0.5"));

    CHECK(HWPlugin_SetPartGain(opl3, "CD", 1.f, 1.f) == HW_OK);
    CHECK(gain_is(opl3, "CD", 1.f, 1.f));

    // ── 引数の誤り ──
    float l = 0.f, r = 0.f;
    CHECK(HWPlugin_SetPartGain(opna0, "CD",  1.f, 1.f) == HW_ERR_INVALID_ARG); // 別チップの部位
    CHECK(HWPlugin_SetPartGain(opna0, "ssg", 1.f, 1.f) == HW_ERR_INVALID_ARG); // 大文字小文字
    CHECK(HWPlugin_SetPartGain(opna0, nullptr, 1.f, 1.f) == HW_ERR_INVALID_ARG);
    CHECK(HWPlugin_SetPartGain(opm,   "FM",  1.f, 1.f) == HW_ERR_INVALID_ARG); // 部位なし
    CHECK(HWPlugin_SetPartGain(nullptr, "FM", 1.f, 1.f) == HW_ERR_INVALID_ARG);
    CHECK(HWPlugin_GetPartGain(opna0, "CD", &l, &r) == HW_ERR_INVALID_ARG);
    CHECK(HWPlugin_GetPartGain(opna0, "SSG", nullptr, &r) == HW_ERR_INVALID_ARG);
    CHECK(HWPlugin_GetPartGain(opna0, "SSG", &l, nullptr) == HW_ERR_INVALID_ARG);
    CHECK(HWPlugin_GetPartGain(nullptr, "SSG", &l, &r) == HW_ERR_INVALID_ARG);
    CHECK(HWPlugin_GetPartCount(nullptr) == 0);
    CHECK(HWPlugin_GetPartName(nullptr, 0) == nullptr);
    CHECK(gain_is(opna0, "SSG", 0.25f, 0.5f)); // 失敗した呼び出しは値を変えない

    // ── Close で既定値に戻る ──
    HWPlugin_Close(opna0);
    HWPlugin_Close(opl3);
    HWPlugin_Close(opl4); // ゲインを設定していないハンドル
    {
        auto log = split_lines(get_log());
        CHECK(has_line(log, "SetPartGain chip=0 part=SSG l=1 r=1"));
        CHECK(has_line(log, "SetPartGain chip=2 part=CD l=0 r=0")); // 1.0 ではなくエンジンの既定値
        CHECK(count_prefix(log, "SetPartGain chip=3 ") == 0);       // 設定していなければ書かない
    }
    opna0 = open_chip(engine, "OPNA", 0);
    opl3  = open_chip(engine, "OPL3", 0);
    CHECK(opna0 && opl3);
    if (opna0) CHECK(gain_is(opna0, "SSG", 1.f, 1.f));
    if (opl3)  CHECK(gain_is(opl3,  "CD",  0.f, 0.f));

    if (opna0) HWPlugin_Close(opna0);
    if (opl3)  HWPlugin_Close(opl3);
    HWPlugin_Close(opna1);
    HWPlugin_Close(opm);
    HWPlugin_Shutdown();
}

// ── mode: legacy ─────────────────────────────────────────────────────────────

static const char* const kLegacyChips =
    "    { \"chip\": \"OPNA\", \"clock\": 7987200 },\n"
    "    { \"chip\": \"OPL3\", \"clock\": 14318180 },\n"
    "    { \"chip\": \"OPM\",  \"clock\": 3579545 }\n";

static void test_legacy(const fs::path& profile, const std::string& engine,
                        const fs::path& engine_path, const char* (*get_log)())
{
    CHECK(HWPlugin_Init(profile.string().c_str()) == HW_OK);
    if (g_fail) return;

    HWHandle opna = open_chip(engine, "OPNA", 0);
    CHECK(opna != nullptr);
    if (!opna) return;

    // 部位を持たないエンジンとして扱う
    float l = 0.f, r = 0.f;
    CHECK(HWPlugin_GetPartCount(opna) == 0);
    CHECK(HWPlugin_GetPartName(opna, 0) == nullptr);
    CHECK(HWPlugin_SetPartGain(opna, "SSG", 0.5f, 0.5f) == HW_ERR_INVALID_ARG);
    CHECK(HWPlugin_GetPartGain(opna, "SSG", &l, &r) == HW_ERR_INVALID_ARG);

    CHECK(HWPlugin_Write(opna, 0x0007, 0x38) == HW_OK);
    HWPlugin_Close(opna);
    HWPlugin_Shutdown();

    if (!get_log) {
        std::printf("      (StubEngine_GetLog が無いエンジン: 呼び出しの記録は見ていない)\n");
        return;
    }
    auto log = split_lines(get_log());
    CHECK(has_line(log, "AddChip chip=0 name=OPNA"));
    // 番号で受け取る版の関数を、名前のポインタで呼んでいない
    CHECK(count_prefix(log, "legacy ") == 0);
    for (const auto& line : log)
        if (line.compare(0, 7, "legacy ") == 0) std::printf("      %s\n", line.c_str());

    // 対照: 上の 0 件が「記録されない」せいではないこと。同じ関数を番号で直接呼ぶと残る
    using LegacySetMemoryFn = int (*)(void*, uint32_t, int, const uint8_t*, uint32_t);
    auto legacy_set_memory = reinterpret_cast<LegacySetMemoryFn>(
        engine_sym(engine_path.string().c_str(), "FmEngine_SetMemory"));
    CHECK(legacy_set_memory != nullptr);
    if (legacy_set_memory) {
        legacy_set_memory(nullptr, 0, 2, nullptr, 0);
        CHECK(count_prefix(split_lines(get_log()), "legacy ") == 1);
    }
}

// ── mode: incomplete ─────────────────────────────────────────────────────────

static void test_incomplete(const fs::path& profile) {
    CHECK(HWPlugin_Init(profile.string().c_str()) == HW_ERR_OPEN_FAILED);
    CHECK(HWPlugin_Enumerate() == nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc != 3) {
        std::printf("usage: %s <current|legacy|incomplete> <engine dll path>\n", argv[0]);
        return 2;
    }
    const std::string mode        = argv[1];
    const fs::path    engine_path = argv[2];
    // FitomEmuIF は拡張子付きの名前をそのままファイル名として探す
    const std::string engine      = engine_path.filename().string();

    const bool is_current    = (mode == "current");
    const bool is_legacy     = (mode == "legacy");
    const bool is_incomplete = (mode == "incomplete");
    if (!is_current && !is_legacy && !is_incomplete) {
        std::printf("unknown mode: %s\n", mode.c_str());
        return 2;
    }

    const fs::path dir = fs::temp_directory_path()
        / ("fitomemuif_relay_test_" + mode + "_" + engine_path.stem().string());
    write_files(dir, engine, is_legacy ? kLegacyChips : kCurrentChips);
    const fs::path profile = dir / kProfileFile;

    using GetLogFn = const char* (*)();
    auto get_log = reinterpret_cast<GetLogFn>(
        engine_sym(engine_path.string().c_str(), "StubEngine_GetLog"));

    if (is_current)         test_current(profile, engine, get_log);
    else if (is_legacy)     test_legacy(profile, engine, engine_path, get_log);
    else                    test_incomplete(profile);

    remove_files(dir);

    std::printf("%s (%s): %d passed, %d failed\n",
                mode.c_str(), engine.c_str(), g_pass, g_fail);
    return g_fail ? 1 : 0;
}
