// FmHwIfImpl.cpp
// FitomEmuIF: IHWPlugin を実装し、FmEngineApi 互換 DLL を複数束ね、
//             RtAudio で PCM をオーディオデバイスへ出力する hwif プラグイン。
//
// ─── アーキテクチャ ──────────────────────────────────────────────────────────
//
//  [DLL ロード時]  ← 構成はここで確定。FITOM とのリンク中は不変。
//    PluginRegistry::instance() が fmhwif_profile.json を読み込み、
//    EngineInstance 群を生成したうえで RtAudio ストリームを 1 本起動する。
//
//  [オーディオコールバック]
//    全 EngineInstance の FmEngine_Generate を呼んで float 加算ミックスし、
//    RtAudio インターリーブバッファに書き出す。
//    FmEngine_Write との排他は各 EngineInstance の generate_mutex で行う。
//
//  [HWPlugin_Write]
//    generate_mutex を取得して FmEngine_Write を呼ぶ。
//    コールバックが走っている間はブロックするが、buffer_frames/sample_rate 秒以内に
//    必ず解放される。
//
//  [HWPlugin_Open / Close]
//    プロファイル定義済みの ChipSlot への参照を返すだけ。
//    エンジン本体・RtAudio ストリームは PluginRegistry が管理する。
//
// ─── プロファイル JSON (fmhwif_profile.json) ─────────────────────────────────
//
//  {
//    "sample_rate":   44100,       // 全エンジン共通サンプルレート
//    "buffer_frames": 512,         // RtAudio バッファサイズ兼レイテンシ申告値
//    "audio_api":     "auto",      // RtAudio API 名 ("auto","alsa","wasapi","core" 等)
//    "audio_device":  "",          // デバイス名部分一致。空文字でデフォルトデバイス
//    "engines": [
//      {
//        "dll":   "YMEngine",
//        "chips": [
//          { "chip": "OPM",  "clock": 3579545, "pan": 0 },
//          { "chip": "OPNA", "clock": 7987200, "pan": 0 }
//        ]
//      },
//      {
//        "dll":   "OPLEngine",
//        "chips": [
//          { "chip": "OPL3", "clock": 14318181, "pan": 0 }
//        ]
//      }
//    ]
//  }
//
// ─── スレッド安全性 ──────────────────────────────────────────────────────────
//
//  FmEngine_Write  : FmEngineApi 仕様でスレッドセーフ。
//  FmEngine_Generate: オーディオコールバックスレッドから排他的に呼ぶ。
//  generate_mutex  : HWPlugin_Write(MIDIスレッド) と コールバック の排他に使う。
//                    同一 EngineInstance にのみ作用するため、異なる DLL 間は並行する。

// FITOM_HW_PLUGIN_EXPORTS は CMake の target_compile_definitions で定義する

#include "fitom/IHWPlugin.h"
#include "fitom/FmEngineApi.h"

#include <RtAudio.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// ════════════════════════════════════════════════════════════════════════════
//  プラットフォーム DLL ユーティリティ
// ════════════════════════════════════════════════════════════════════════════

#if defined(_WIN32)
#  include <windows.h>
using DllHandle = HMODULE;
static DllHandle dll_open(const char* p) { return LoadLibraryA(p); }
static void      dll_close(DllHandle h)  { FreeLibrary(h); }
static void*     dll_sym(DllHandle h, const char* s) {
    return reinterpret_cast<void*>(GetProcAddress(h, s)); }
static std::string dll_dir() {
    char buf[MAX_PATH] = {};
    HMODULE self = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&dll_dir), &self);
    GetModuleFileNameA(self, buf, MAX_PATH);
    std::string p(buf);
    auto slash = p.find_last_of("\\/");
    return (slash != std::string::npos) ? p.substr(0, slash + 1) : "";
}
#else
#  include <dlfcn.h>
using DllHandle = void*;
static DllHandle dll_open(const char* p) { return dlopen(p, RTLD_LAZY | RTLD_LOCAL); }
static void      dll_close(DllHandle h)  { dlclose(h); }
static void*     dll_sym(DllHandle h, const char* s) { return dlsym(h, s); }
static std::string dll_dir() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&dll_dir), &info) && info.dli_fname) {
        std::string p(info.dli_fname);
        auto slash = p.find_last_of('/');
        return (slash != std::string::npos) ? p.substr(0, slash + 1) : "";
    }
    return "";
}
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

// ════════════════════════════════════════════════════════════════════════════
//  FmEngine vtbl
// ════════════════════════════════════════════════════════════════════════════

struct FmEngineVtbl {
    decltype(&FmEngine_Create)           Create           = nullptr;
    decltype(&FmEngine_Destroy)          Destroy          = nullptr;
    decltype(&FmEngine_Inquiry)          Inquiry          = nullptr;
    decltype(&FmEngine_GetSupportedChip) GetSupportedChip = nullptr;
    decltype(&FmEngine_AddChip)          AddChip          = nullptr;
    decltype(&FmEngine_GetChipName)      GetChipName      = nullptr;
    decltype(&FmEngine_GetNativeRate)    GetNativeRate    = nullptr;
    decltype(&FmEngine_GetSampleRate)    GetSampleRate    = nullptr;
    decltype(&FmEngine_Write)            Write            = nullptr;
    decltype(&FmEngine_SetGain)          SetGain          = nullptr;
    decltype(&FmEngine_Generate)         Generate         = nullptr;
};

#define LOAD_SYM(vtbl, dll, name) \
    do { \
        (vtbl).name = reinterpret_cast<decltype(&FmEngine_##name)>( \
            dll_sym((dll), "FmEngine_" #name)); \
        if (!(vtbl).name) \
            throw std::runtime_error("missing symbol: FmEngine_" #name); \
    } while(0)

static FmEngineVtbl load_vtbl(DllHandle h) {
    FmEngineVtbl v;
    LOAD_SYM(v, h, Create);
    LOAD_SYM(v, h, Destroy);
    LOAD_SYM(v, h, Inquiry);
    LOAD_SYM(v, h, GetSupportedChip);
    LOAD_SYM(v, h, AddChip);
    LOAD_SYM(v, h, GetChipName);
    LOAD_SYM(v, h, GetNativeRate);
    LOAD_SYM(v, h, GetSampleRate);
    LOAD_SYM(v, h, Write);
    LOAD_SYM(v, h, SetGain);
    LOAD_SYM(v, h, Generate);
    return v;
}

// ════════════════════════════════════════════════════════════════════════════
//  DLL 名の正規化 / ロード
// ════════════════════════════════════════════════════════════════════════════

static std::string normalize_dll(const std::string& name) {
#if defined(_WIN32)
    if (name.find('.') == std::string::npos) return name + ".dll";
#elif defined(__APPLE__)
    if (name.rfind("lib", 0) != 0) {
        std::string n = "lib" + name;
        if (n.find('.') == std::string::npos) n += ".dylib";
        return n;
    }
#else
    if (name.rfind("lib", 0) != 0) {
        std::string n = "lib" + name;
        if (n.find('.') == std::string::npos) n += ".so";
        return n;
    }
#endif
    return name;
}

static DllHandle load_engine_dll(const std::string& raw_name) {
    std::string norm = normalize_dll(raw_name);
    // 1. FitomEmuIF.dll と同じディレクトリ
    if (DllHandle h = dll_open((dll_dir() + norm).c_str())) return h;
    // 2. システム検索パス
    return dll_open(norm.c_str());
}

// ════════════════════════════════════════════════════════════════════════════
//  ChipSlot: エンジン内の 1 チップ分の設定と chip_id
// ════════════════════════════════════════════════════════════════════════════

struct ChipSlot {
    std::string engine_dll_name; // プロファイル記載名（Enumerate 用）
    std::string chip_name;
    int         index   = 0;     // 同種チップの通し番号
    uint32_t    chip_id = 0;     // FmEngine_AddChip が返した ID
    int         clock   = 0;     // 実クロック [Hz]
    int         panpot  = 0;     // 0=Stereo,1=L only,2=R only
    bool        in_use  = false;
};

// ════════════════════════════════════════════════════════════════════════════
//  EngineInstance: 1 エンジン DLL + FmEngine + ChipSlot 群
// ════════════════════════════════════════════════════════════════════════════

struct EngineInstance {
    std::string    dll_name_raw;
    DllHandle      dll_handle = nullptr;
    FmEngineVtbl   vtbl;
    FmEngineHandle engine = nullptr;

    std::vector<ChipSlot> slots;

    // HWPlugin_Write（MIDIスレッド）と オーディオコールバック の排他ロック。
    // FmEngine_Write 自体はスレッドセーフだが、Generate との同時実行を避けるため
    // コールバック側も同じロックを取る。
    std::mutex generate_mutex;

    // オーディオコールバック用一時バッファ（PluginRegistry が確保する）
    std::vector<float> tmp_l;
    std::vector<float> tmp_r;

    EngineInstance() = default;
    EngineInstance(const EngineInstance&) = delete;
    EngineInstance& operator=(const EngineInstance&) = delete;

    ~EngineInstance() {
        if (engine) { vtbl.Destroy(engine); engine = nullptr; }
        if (dll_handle) { dll_close(dll_handle); dll_handle = nullptr; }
    }

    void resize_tmp(uint32_t frames) {
        tmp_l.resize(frames, 0.f);
        tmp_r.resize(frames, 0.f);
    }
};

// ════════════════════════════════════════════════════════════════════════════
//  RtAudio API 名 → RtAudio::Api 変換
// ════════════════════════════════════════════════════════════════════════════

static RtAudio::Api parse_audio_api(const std::string& name) {
    // 大文字小文字を区別しない簡易マッチング
    auto eq = [&](const char* s) {
        if (name.size() != std::strlen(s)) return false;
        for (size_t i = 0; i < name.size(); ++i)
            if (std::tolower((unsigned char)name[i]) != std::tolower((unsigned char)s[i]))
                return false;
        return true;
    };
    if (eq("auto")     || eq("unspecified")) return RtAudio::UNSPECIFIED;
#if defined(_WIN32)
    if (eq("wasapi"))  return RtAudio::WINDOWS_WASAPI;
    if (eq("asio"))    return RtAudio::WINDOWS_ASIO;
    if (eq("ds")       || eq("directsound")) return RtAudio::WINDOWS_DS;
#elif defined(__APPLE__)
    if (eq("core")     || eq("coreaudio"))   return RtAudio::MACOSX_CORE;
#else
    if (eq("alsa"))    return RtAudio::LINUX_ALSA;
    if (eq("pulse")    || eq("pulseaudio"))  return RtAudio::LINUX_PULSE;
    if (eq("jack"))    return RtAudio::UNIX_JACK;
    if (eq("oss"))     return RtAudio::LINUX_OSS;
#endif
    return RtAudio::UNSPECIFIED;
}

// ════════════════════════════════════════════════════════════════════════════
//  PluginRegistry: DLL ロード時に一度だけ構築、以後不変
// ════════════════════════════════════════════════════════════════════════════

struct PluginRegistry {
    // ── プロファイル由来の設定 ─────────────────────────────────────────────
    uint32_t    sample_rate   = 44100;
    uint32_t    buffer_frames = 512;
    std::string audio_api_name  = "auto";  // parse_audio_api に渡す
    std::string audio_device_name;         // 空 = デフォルトデバイス

    // ── エンジン群（プロファイルの engines[] に 1:1 対応）─────────────────
    std::vector<std::unique_ptr<EngineInstance>> engines;

    // ── RtAudio ───────────────────────────────────────────────────────────
    std::unique_ptr<RtAudio> rtaudio;

    // ── 状態 ──────────────────────────────────────────────────────────────
    bool        initialized = false;
    std::string init_error;

    // ─────────────────────────────────────────────────────────────────────
    static PluginRegistry& instance() {
        static PluginRegistry reg;
        return reg;
    }

    // ── プロファイルパス解決 ──────────────────────────────────────────────
    // 優先順:
    //   1. 環境変数 FMHWIF_PROFILE
    //   2. FitomEmuIF.dll と同じディレクトリの fmhwif_profile.json
    //   3. カレントディレクトリの fmhwif_profile.json
    static std::optional<fs::path> find_profile() {
        if (const char* env = std::getenv("FMHWIF_PROFILE")) {
            fs::path p(env);
            if (fs::exists(p)) return p;
        }
        {
            fs::path p = fs::path(dll_dir()) / "fmhwif_profile.json";
            if (fs::exists(p)) return p;
        }
        {
            fs::path p = fs::current_path() / "fmhwif_profile.json";
            if (fs::exists(p)) return p;
        }
        return std::nullopt;
    }

private:
    PluginRegistry() { load(); }

    // ── メイン初期化 ─────────────────────────────────────────────────────
    void load() {
        auto profile_path = find_profile();
        if (!profile_path) {
            // プロファイルなし → 空構成で初期化済みとする
            initialized = true;
            return;
        }

        try {
            std::ifstream ifs(*profile_path);
            if (!ifs)
                throw std::runtime_error("cannot open: " + profile_path->string());

            json root = json::parse(ifs, nullptr, true, true); // コメント許可

            sample_rate       = root.value("sample_rate",   44100u);
            buffer_frames     = root.value("buffer_frames", 512u);
            audio_api_name    = root.value("audio_api",     std::string("auto"));
            audio_device_name = root.value("audio_device",  std::string(""));

            for (auto& eng_json : root.at("engines"))
                load_engine(eng_json);

            start_audio();

            initialized = true;
        } catch (const std::exception& e) {
            init_error  = e.what();
            initialized = false;
        }
    }

    // ── エンジン 1 つのロード ─────────────────────────────────────────────
    void load_engine(const json& eng_json) {
        std::string dll_raw = eng_json.at("dll").get<std::string>();

        auto inst = std::make_unique<EngineInstance>();
        inst->dll_name_raw = dll_raw;

        inst->dll_handle = load_engine_dll(dll_raw);
        if (!inst->dll_handle)
            throw std::runtime_error("cannot load engine DLL: " + dll_raw);

        inst->vtbl   = load_vtbl(inst->dll_handle);
        inst->engine = inst->vtbl.Create(sample_rate);
        if (!inst->engine)
            throw std::runtime_error("FmEngine_Create failed: " + dll_raw);

        inst->resize_tmp(buffer_frames);

        std::unordered_map<std::string, int> chip_idx_counter;
        for (auto& chip_json : eng_json.at("chips")) {
            std::string chip_name = chip_json.at("chip").get<std::string>();
            uint32_t    clock     = chip_json.value("clock", 0u);
            int         panpot    = chip_json.value("pan",   0);

            uint32_t chip_id = 0;
            FmResult fr = inst->vtbl.AddChip(
                inst->engine, chip_name.c_str(), clock, &chip_id);
            if (fr == FM_ERR_UNKNOWN_CHIP)
                throw std::runtime_error(
                    "unknown chip '" + chip_name + "' in engine " + dll_raw);
            if (fr != FM_OK)
                throw std::runtime_error(
                    "FmEngine_AddChip failed: chip=" + chip_name + " engine=" + dll_raw);

            int actual_clock = clock
                ? static_cast<int>(clock)
                : static_cast<int>(inst->vtbl.GetNativeRate(inst->engine, chip_id));

            ChipSlot slot;
            slot.engine_dll_name = dll_raw;
            slot.chip_name       = chip_name;
            slot.index           = chip_idx_counter[chip_name]++;
            slot.chip_id         = chip_id;
            slot.clock           = actual_clock;
            slot.panpot          = panpot;

            apply_panpot(*inst, slot);
            inst->slots.push_back(std::move(slot));
        }

        engines.push_back(std::move(inst));
    }

    // ── パノラマをエンジンに反映 ───────────────────────────────────────────
    static void apply_panpot(EngineInstance& inst, const ChipSlot& slot) {
        float l = 1.f, r = 1.f;
        if (slot.panpot == 1) r = 0.f;
        else if (slot.panpot == 2) l = 0.f;
        inst.vtbl.SetGain(inst.engine, slot.chip_id, l, r);
    }

    // ── RtAudio ストリーム起動 ────────────────────────────────────────────
    void start_audio() {
        if (engines.empty()) return; // チップなしなら音声出力不要

        RtAudio::Api api = parse_audio_api(audio_api_name);
        rtaudio = std::make_unique<RtAudio>(api);

        // デバイス選択
        unsigned int device_id = select_output_device(*rtaudio, audio_device_name);

        RtAudio::StreamParameters out_params;
        out_params.deviceId     = device_id;
        out_params.nChannels    = 2; // ステレオ固定
        out_params.firstChannel = 0;

        RtAudio::StreamOptions opts;
        opts.flags = RTAUDIO_SCHEDULE_REALTIME;

        uint32_t frames = buffer_frames;
        if (rtaudio->openStream(
                &out_params, nullptr,
                RTAUDIO_FLOAT32,
                sample_rate,
                &frames,
                &PluginRegistry::audio_callback,
                this,
                &opts) != RTAUDIO_NO_ERROR)
        {
            throw std::runtime_error(
                std::string("RtAudio::openStream failed: ") + rtaudio->getErrorText());
        }

        // buffer_frames は RtAudio が変更することがある（デバイス制約）
        buffer_frames = frames;
        // tmp バッファを実際の frames に合わせ直す
        for (auto& inst : engines)
            inst->resize_tmp(buffer_frames);

        if (rtaudio->startStream() != RTAUDIO_NO_ERROR)
            throw std::runtime_error(
                std::string("RtAudio::startStream failed: ") + rtaudio->getErrorText());
    }

    // ── 出力デバイス選択 ─────────────────────────────────────────────────
    // audio_device_name が空 → デフォルトデバイス
    // 非空 → デバイス名に部分一致する最初のデバイス。見つからなければデフォルト。
    static unsigned int select_output_device(RtAudio& rt,
                                             const std::string& name_hint)
    {
        unsigned int default_id = rt.getDefaultOutputDevice();
        if (name_hint.empty()) return default_id;

        std::vector<unsigned int> ids = rt.getDeviceIds();
        for (unsigned int id : ids) {
            RtAudio::DeviceInfo info = rt.getDeviceInfo(id);
            if (info.outputChannels == 0) continue;
            if (info.name.find(name_hint) != std::string::npos)
                return id;
        }
        return default_id; // 一致なし → デフォルト
    }

    // ── RtAudio コールバック ──────────────────────────────────────────────
    // 全 EngineInstance を Generate してミックスし、output へ書き出す。
    // output は RtAudio が確保した RTAUDIO_FLOAT32 インターリーブバッファ。
    static int audio_callback(void*       output,
                              void*       /*input*/,
                              unsigned int n_frames,
                              double      /*stream_time*/,
                              RtAudioStreamStatus /*status*/,
                              void*       user_data)
    {
        auto* self = static_cast<PluginRegistry*>(user_data);
        float* out = static_cast<float*>(output);

        // 出力バッファをゼロクリア
        std::fill(out, out + n_frames * 2u, 0.f);

        for (auto& inst : self->engines) {
            // Generate との排他ロック（HWPlugin_Write と同じロック）
            std::lock_guard<std::mutex> lk(inst->generate_mutex);

            // tmp バッファが n_frames に足りない場合は安全にスキップ
            if (inst->tmp_l.size() < n_frames || inst->tmp_r.size() < n_frames)
                continue;

            FmResult fr = inst->vtbl.Generate(
                inst->engine,
                inst->tmp_l.data(),
                inst->tmp_r.data(),
                n_frames);
            if (fr != FM_OK) continue;

            // 加算ミックス（インターリーブ L/R）
            for (uint32_t i = 0; i < n_frames; ++i) {
                out[i * 2 + 0] += inst->tmp_l[i];
                out[i * 2 + 1] += inst->tmp_r[i];
            }
        }

        // クリッピング：[-1, 1] にクランプ
        for (uint32_t i = 0; i < n_frames * 2u; ++i)
            out[i] = std::clamp(out[i], -1.f, 1.f);

        return 0; // 継続
    }
};

// ════════════════════════════════════════════════════════════════════════════
//  HWDeviceOpaque
// ════════════════════════════════════════════════════════════════════════════

struct HWDeviceOpaque {
    EngineInstance* engine_inst    = nullptr;
    ChipSlot*       slot           = nullptr;
    int             panpot_override = -1; // -1 = プロファイル値を使う
    bool            is_open        = false;
};

static int effective_panpot(const HWDeviceOpaque* dev) {
    return (dev->panpot_override >= 0) ? dev->panpot_override : dev->slot->panpot;
}

// ════════════════════════════════════════════════════════════════════════════
//  IHWPlugin エクスポート
// ════════════════════════════════════════════════════════════════════════════

extern "C" {

FITOM_HWP_API const char* FITOM_HWP_CALL HWPlugin_GetName() {
    return "FitomEmuIF";
}

// ── デバイス列挙 ──────────────────────────────────────────────────────────────

FITOM_HWP_API const char* FITOM_HWP_CALL HWPlugin_Enumerate() {
    json arr = json::array();
    auto& reg = PluginRegistry::instance();
    if (reg.initialized) {
        for (auto& inst : reg.engines) {
            for (auto& slot : inst->slots) {
                json e;
                e["type"]   = "FMHWIF";
                e["engine"] = slot.engine_dll_name;
                e["chip"]   = slot.chip_name;
                e["index"]  = slot.index;
                arr.push_back(std::move(e));
            }
        }
    }
    std::string s = arr.dump();
    char* buf = new char[s.size() + 1];
    std::memcpy(buf, s.c_str(), s.size() + 1);
    return buf;
}

FITOM_HWP_API void FITOM_HWP_CALL HWPlugin_FreeString(const char* str) {
    delete[] str;
}

// ── デバイスオープン ─────────────────────────────────────────────────────────
// params_json: { "type":"FMHWIF", "engine":"YMEngine", "chip":"OPM", "index":0 }

FITOM_HWP_API HWResult FITOM_HWP_CALL HWPlugin_Open(
    const char* params_json, HWHandle* out_handle)
{
    if (!params_json || !out_handle) return HW_ERR_INVALID_ARG;

    auto& reg = PluginRegistry::instance();
    if (!reg.initialized) return HW_ERR_OPEN_FAILED;

    json params;
    try { params = json::parse(params_json); }
    catch (...) { return HW_ERR_INVALID_ARG; }

    {
        auto it = params.find("type");
        if (it == params.end() || it->get<std::string>() != "FMHWIF")
            return HW_ERR_INVALID_ARG;
    }

    auto eit = params.find("engine");
    auto cit = params.find("chip");
    if (eit == params.end() || cit == params.end())
        return HW_ERR_INVALID_ARG;

    std::string want_engine = eit->get<std::string>();
    std::string want_chip   = cit->get<std::string>();
    int         want_index  = params.value("index", 0);

    int pan_override = -1;
    if (auto pit = params.find("pan"); pit != params.end())
        pan_override = pit->get<int>();

    for (auto& inst : reg.engines) {
        if (inst->dll_name_raw != want_engine) continue;
        for (auto& slot : inst->slots) {
            if (slot.chip_name != want_chip || slot.index != want_index) continue;
            if (slot.in_use) return HW_ERR_OPEN_FAILED;

            slot.in_use = true;

            auto* dev           = new HWDeviceOpaque();
            dev->engine_inst    = inst.get();
            dev->slot           = &slot;
            dev->panpot_override = pan_override;
            dev->is_open        = true;

            if (pan_override >= 0) {
                float l = 1.f, r = 1.f;
                if (pan_override == 1) r = 0.f;
                else if (pan_override == 2) l = 0.f;
                inst->vtbl.SetGain(inst->engine, slot.chip_id, l, r);
            }

            *out_handle = dev;
            return HW_OK;
        }
    }
    return HW_ERR_NOT_FOUND;
}

FITOM_HWP_API void FITOM_HWP_CALL HWPlugin_Close(HWHandle handle) {
    if (!handle) return;
    handle->is_open      = false;
    handle->slot->in_use = false;
    delete handle;
}

// ── I/O ──────────────────────────────────────────────────────────────────────
// addr 上位 8bit → port、下位 8bit → reg（IHWPlugin.h 規約）
//
// generate_mutex を取得する。オーディオコールバックが Generate 中の場合は
// そのバッファ（≒ buffer_frames/sample_rate 秒）だけブロックして返す。

FITOM_HWP_API HWResult FITOM_HWP_CALL HWPlugin_Write(
    HWHandle handle, uint16_t addr, uint8_t data)
{
    if (!handle) return HW_ERR_INVALID_ARG;
    if (!handle->is_open) return HW_ERR_IO;

    uint8_t  reg  = static_cast<uint8_t>(addr & 0xFF);
    uint32_t port = static_cast<uint32_t>(addr >> 8);

    auto& inst = *handle->engine_inst;
    std::lock_guard<std::mutex> lk(inst.generate_mutex);
    FmResult fr = inst.vtbl.Write(inst.engine, handle->slot->chip_id, reg, data, port);
    return (fr == FM_OK) ? HW_OK : HW_ERR_IO;
}

FITOM_HWP_API HWResult FITOM_HWP_CALL HWPlugin_WriteBlock(
    HWHandle handle, uint8_t startAddr, const uint8_t* data, size_t len)
{
    if (!handle || !data) return HW_ERR_INVALID_ARG;
    if (!handle->is_open) return HW_ERR_IO;

    auto& inst = *handle->engine_inst;
    std::lock_guard<std::mutex> lk(inst.generate_mutex);
    for (size_t i = 0; i < len; ++i) {
        uint8_t reg = static_cast<uint8_t>((startAddr + i) & 0xFF);
        FmResult fr = inst.vtbl.Write(
            inst.engine, handle->slot->chip_id, reg, data[i], 0);
        if (fr != FM_OK) return HW_ERR_IO;
    }
    return HW_OK;
}

FITOM_HWP_API HWResult FITOM_HWP_CALL HWPlugin_Reset(
    HWHandle handle, unsigned int /*pulse_us*/)
{
    if (!handle) return HW_ERR_INVALID_ARG;
    if (!handle->is_open) return HW_ERR_IO;

    auto& inst = *handle->engine_inst;
    std::lock_guard<std::mutex> lk(inst.generate_mutex);
    for (uint32_t port = 0; port <= 1; ++port)
        for (uint32_t reg = 0; reg <= 0xFF; ++reg)
            inst.vtbl.Write(inst.engine, handle->slot->chip_id,
                            static_cast<uint8_t>(reg), 0, port);
    return HW_OK;
}

// ── メタ情報 ─────────────────────────────────────────────────────────────────

FITOM_HWP_API int FITOM_HWP_CALL HWPlugin_GetClock(HWHandle handle) {
    return handle ? handle->slot->clock : 0;
}
FITOM_HWP_API int FITOM_HWP_CALL HWPlugin_GetPanpot(HWHandle handle) {
    return handle ? effective_panpot(handle) : 0;
}
FITOM_HWP_API bool FITOM_HWP_CALL HWPlugin_IsOpen(HWHandle handle) {
    return handle && handle->is_open;
}

// ── レイテンシ同期 ────────────────────────────────────────────────────────────

FITOM_HWP_API uint32_t FITOM_HWP_CALL HWPlugin_GetLatencySamples(HWHandle handle) {
    // レイテンシ = RtAudio が使う実際の buffer_frames（openStream 後に確定）
    if (!handle) return 0;
    auto& reg = PluginRegistry::instance();
    return reg.buffer_frames;
}

FITOM_HWP_API void FITOM_HWP_CALL HWPlugin_SetDelaySamples(
    HWHandle /*handle*/, uint32_t /*delay_samples*/)
{
    // buffer_frames == delay_samples となるよう FITOM が設定するため no-op。
}

} // extern "C"
