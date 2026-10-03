// test/stub_engine.cpp
// FmEngineApi の検証用エンジン。音は出さず、受けた呼び出しを記録する。
// 記録は StubEngine_GetLog() で 1 行 1 呼び出しの文字列として読める。
//
// コンパイル時の定義で作り分ける:
//   (定義なし)                現行の FmEngineApi（部位と外部メモリを名前で指定）
//   STUB_LEGACY               部位と外部メモリを番号で指定する版。現行と同じ名前の
//                             関数を番号の引数でエクスポートし、FmEngine_GetPartCount と
//                             FmEngine_GetMemoryCount は持たない
//   STUB_OMIT_GETPARTNAME     現行の形から FmEngine_GetPartName だけを欠く（仕様違反の DLL）
//   STUB_OMIT_GETMEMORYNAME   現行の形から FmEngine_GetMemoryName だけを欠く（同上）

#if defined(STUB_LEGACY)
// 番号で指定する版は、現行ヘッダと同じ名前の関数を別の引数で定義する。
// C リンケージでは多重定義できないので、現行ヘッダは include せず、要る型だけ書く。
#  include <stdint.h>
#  if defined(_WIN32) || defined(__CYGWIN__)
#    define FMENGINE_API  __declspec(dllexport)
#    define FMENGINE_CALL __cdecl
#  else
#    define FMENGINE_API  __attribute__((visibility("default")))
#    define FMENGINE_CALL
#  endif
typedef enum FmResult {
    FM_OK                =  0,
    FM_ERR_INVALID_ARG   = -1,
    FM_ERR_UNKNOWN_CHIP  = -2,
    FM_ERR_ALLOC         = -3,
    FM_ERR_UNAVAILABLE   = -4,
} FmResult;
struct FmEngineOpaque;
typedef struct FmEngineOpaque* FmEngineHandle;
#else
// 現行の形は正本の写しを include し、定義の引数が宣言と合うことをコンパイラに確かめさせる
#  define FMENGINE_EXPORTS
#  include "fitom/FmEngineApi.h"
#endif

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct PartDef { const char* name; float gain; };

struct ChipDef {
    const char*              name;
    std::vector<PartDef>     parts;
    std::vector<const char*> memories;
};

// 部位の名前と既定値、外部メモリの名前は FmEngineApi 仕様書の表に合わせた。
// SSGS は仕様書の表に無いチップで、メモリの名前はこのエンジンが決めたもの。
// OPNB のメモリを表と逆順に並べているのは、呼び出し側が並びに依らないことを見るため。
const std::vector<ChipDef>& chip_defs() {
    static const std::vector<ChipDef> defs = {
        { "OPNA",  { {"FM", 1.f}, {"SSG", 1.f} },                 { "RHYTHM", "ADPCM_B", "ADPCM_B_ROMMODE" } },
        { "OPNB",  { {"FM", 1.f}, {"SSG", 1.f} },                 { "ADPCM_B", "ADPCM_A" } },
        { "OPL3",  { {"AB", 1.f}, {"CD", 0.f} },                  {} },
        { "OPL4",  { {"DO0", 0.f}, {"DO1", 0.f}, {"DO2", 1.f} },  { "PCM" } },
        { "Y8950", {},                                            { "ADPCM_B", "ADPCM_B_ROMMODE" } },
        { "SSGS",  {},                                            { "PCM" } },
        { "OPM",   {},                                            {} },
    };
    return defs;
}

struct Gain { float l = 1.f; float r = 1.f; };

struct Chip {
    const ChipDef*    def = nullptr;
    Gain              gain;
    std::vector<Gain> part_gains; // def->parts と同じ並び
};

std::string g_log;

void log_line(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_log += buf;
    g_log += '\n';
}

} // namespace

struct FmEngineOpaque {
    uint32_t          sample_rate = 0;
    std::vector<Chip> chips;
};

namespace {

Chip* find_chip(FmEngineHandle engine, uint32_t chip_id) {
    if (!engine || chip_id >= engine->chips.size()) return nullptr;
    return &engine->chips[chip_id];
}

#if !defined(STUB_LEGACY)
Gain* find_part(Chip* chip, const char* part) {
    if (!chip || !part) return nullptr;
    for (size_t i = 0; i < chip->def->parts.size(); ++i)
        if (std::strcmp(chip->def->parts[i].name, part) == 0)
            return &chip->part_gains[i];
    return nullptr;
}

bool has_memory(const Chip* chip, const char* memory) {
    if (!chip || !memory) return false;
    for (const char* m : chip->def->memories)
        if (std::strcmp(m, memory) == 0) return true;
    return false;
}
#endif

} // namespace

extern "C" {

// ── 検証用の読み出し口 ───────────────────────────────────────────────────────
FMENGINE_API const char* FMENGINE_CALL StubEngine_GetLog() {
    return g_log.c_str();
}

// ── 必須の 11 関数 ───────────────────────────────────────────────────────────

FMENGINE_API FmEngineHandle FMENGINE_CALL FmEngine_Create(uint32_t sample_rate) {
    auto* engine = new FmEngineOpaque();
    engine->sample_rate = sample_rate;
    return engine;
}

FMENGINE_API void FMENGINE_CALL FmEngine_Destroy(FmEngineHandle engine) {
    delete engine;
}

FMENGINE_API uint32_t FMENGINE_CALL FmEngine_Inquiry(FmEngineHandle) {
    return static_cast<uint32_t>(chip_defs().size());
}

FMENGINE_API const char* FMENGINE_CALL FmEngine_GetSupportedChip(
    FmEngineHandle, uint32_t index)
{
    return (index < chip_defs().size()) ? chip_defs()[index].name : nullptr;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_AddChip(
    FmEngineHandle engine, const char* name, uint32_t clock, uint32_t* out_id)
{
    if (!engine || !name || !out_id || clock == 0) return FM_ERR_INVALID_ARG;
    for (const auto& def : chip_defs()) {
        if (std::strcmp(def.name, name) != 0) continue;
        Chip chip;
        chip.def = &def;
        for (const auto& part : def.parts)
            chip.part_gains.push_back(Gain{ part.gain, part.gain });
        *out_id = static_cast<uint32_t>(engine->chips.size());
        engine->chips.push_back(std::move(chip));
        log_line("AddChip chip=%u name=%s", *out_id, name);
        return FM_OK;
    }
    return FM_ERR_UNKNOWN_CHIP;
}

FMENGINE_API const char* FMENGINE_CALL FmEngine_GetChipName(
    FmEngineHandle engine, uint32_t chip_id)
{
    Chip* chip = find_chip(engine, chip_id);
    return chip ? chip->def->name : nullptr;
}

FMENGINE_API uint32_t FMENGINE_CALL FmEngine_GetSampleRate(FmEngineHandle engine) {
    return engine ? engine->sample_rate : 0;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_Write(
    FmEngineHandle engine, uint32_t chip_id, uint8_t, uint8_t, uint32_t)
{
    return find_chip(engine, chip_id) ? FM_OK : FM_ERR_INVALID_ARG;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetGain(
    FmEngineHandle engine, uint32_t chip_id, float gain_l, float gain_r)
{
    Chip* chip = find_chip(engine, chip_id);
    if (!chip) return FM_ERR_INVALID_ARG;
    chip->gain = Gain{ gain_l, gain_r };
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_GetGain(
    FmEngineHandle engine, uint32_t chip_id, float* out_gain_l, float* out_gain_r)
{
    Chip* chip = find_chip(engine, chip_id);
    if (!chip || !out_gain_l || !out_gain_r) return FM_ERR_INVALID_ARG;
    *out_gain_l = chip->gain.l;
    *out_gain_r = chip->gain.r;
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_Generate(
    FmEngineHandle engine, float* out_l, float* out_r, uint32_t samples)
{
    if (!engine || !out_l || !out_r) return FM_ERR_INVALID_ARG;
    for (uint32_t i = 0; i < samples; ++i) { out_l[i] = 0.f; out_r[i] = 0.f; }
    return FM_OK;
}

#if !defined(STUB_LEGACY)

// ── 部位ごとのゲイン（現行の形）──────────────────────────────────────────────

FMENGINE_API uint32_t FMENGINE_CALL FmEngine_GetPartCount(
    FmEngineHandle engine, uint32_t chip_id)
{
    Chip* chip = find_chip(engine, chip_id);
    return chip ? static_cast<uint32_t>(chip->def->parts.size()) : 0;
}

#if !defined(STUB_OMIT_GETPARTNAME)
FMENGINE_API const char* FMENGINE_CALL FmEngine_GetPartName(
    FmEngineHandle engine, uint32_t chip_id, uint32_t index)
{
    Chip* chip = find_chip(engine, chip_id);
    if (!chip || index >= chip->def->parts.size()) return nullptr;
    return chip->def->parts[index].name;
}
#endif

FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetPartGain(
    FmEngineHandle engine, uint32_t chip_id, const char* part,
    float gain_l, float gain_r)
{
    Gain* gain = find_part(find_chip(engine, chip_id), part);
    if (!gain) return FM_ERR_INVALID_ARG;
    *gain = Gain{ gain_l, gain_r };
    log_line("SetPartGain chip=%u part=%s l=%g r=%g", chip_id, part, gain_l, gain_r);
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_GetPartGain(
    FmEngineHandle engine, uint32_t chip_id, const char* part,
    float* out_gain_l, float* out_gain_r)
{
    Gain* gain = find_part(find_chip(engine, chip_id), part);
    if (!gain || !out_gain_l || !out_gain_r) return FM_ERR_INVALID_ARG;
    *out_gain_l = gain->l;
    *out_gain_r = gain->r;
    return FM_OK;
}

// ── 外部メモリ（現行の形）────────────────────────────────────────────────────

FMENGINE_API uint32_t FMENGINE_CALL FmEngine_GetMemoryCount(
    FmEngineHandle engine, uint32_t chip_id)
{
    Chip* chip = find_chip(engine, chip_id);
    return chip ? static_cast<uint32_t>(chip->def->memories.size()) : 0;
}

#if !defined(STUB_OMIT_GETMEMORYNAME)
FMENGINE_API const char* FMENGINE_CALL FmEngine_GetMemoryName(
    FmEngineHandle engine, uint32_t chip_id, uint32_t index)
{
    Chip* chip = find_chip(engine, chip_id);
    if (!chip || index >= chip->def->memories.size()) return nullptr;
    return chip->def->memories[index];
}
#endif

FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetMemory(
    FmEngineHandle engine, uint32_t chip_id,
    const char* memory, const uint8_t* data, uint32_t size)
{
    if (!has_memory(find_chip(engine, chip_id), memory)) return FM_ERR_INVALID_ARG;
    if (!data || size == 0) return FM_ERR_INVALID_ARG;
    log_line("SetMemory chip=%u memory=%s size=%u first=%02X last=%02X",
             chip_id, memory, size, data[0], data[size - 1]);
    return FM_OK;
}

#else // STUB_LEGACY

// ── 部位と外部メモリを番号で指定する版 ───────────────────────────────────────
// 呼ばれたことだけを記録する。現行の呼び出し側は、これらを 1 つも呼んではならない。

FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetPartGain(
    FmEngineHandle, uint32_t chip_id, int part, float, float)
{
    log_line("legacy SetPartGain chip=%u part=%d", chip_id, part);
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_GetPartGain(
    FmEngineHandle, uint32_t chip_id, int part, float* out_gain_l, float* out_gain_r)
{
    log_line("legacy GetPartGain chip=%u part=%d", chip_id, part);
    if (out_gain_l) *out_gain_l = 1.f;
    if (out_gain_r) *out_gain_r = 1.f;
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_GetPartMask(
    FmEngineHandle, uint32_t chip_id, uint32_t* out_mask)
{
    log_line("legacy GetPartMask chip=%u", chip_id);
    if (out_mask) *out_mask = 0x3;
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetMemory(
    FmEngineHandle, uint32_t chip_id, int mem_type, const uint8_t*, uint32_t size)
{
    log_line("legacy SetMemory chip=%u type=%d size=%u", chip_id, mem_type, size);
    return FM_OK;
}

FMENGINE_API uint32_t FMENGINE_CALL FmEngine_GetMemorySize(
    FmEngineHandle, uint32_t chip_id, int mem_type)
{
    log_line("legacy GetMemorySize chip=%u type=%d", chip_id, mem_type);
    return 0;
}

#endif // STUB_LEGACY

} // extern "C"
