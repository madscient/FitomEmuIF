// test/smoke_test.cpp
// FitomEmuIF スモークテスト
// 実エンジン DLL なしで動くエラーパステスト。

#include "fitom/IHWPlugin.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static int pass_count = 0;
#define PASS(msg) do { printf("[PASS] %s\n", (msg)); ++pass_count; } while(0)

int main() {
    // 1. プラグイン名（Init 前でも取得できる唯一の関数）
    {
        const char* name = HWPlugin_GetName();
        assert(name && std::strcmp(name, "FitomEmuIF") == 0);
        PASS("HWPlugin_GetName == \"FitomEmuIF\"");
    }

    // 2. Init 前はすべての関数が失敗を返す
    {
        // Enumerate → nullptr
        assert(HWPlugin_Enumerate() == nullptr);
        PASS("Enumerate before Init -> nullptr");

        // Open → HW_ERR_OPEN_FAILED（初期化前）または HW_ERR_INVALID_ARG（引数不正）
        HWHandle h = nullptr;
        HWResult r = HWPlugin_Open(
            R"({"type":"FMHWIF","engine":"YMEngine","chip":"OPM"})", &h);
        assert(r == HW_ERR_OPEN_FAILED);
        assert(h == nullptr);
        PASS("Open before Init -> HW_ERR_OPEN_FAILED");

        // メタ情報・レイテンシ（nullptr ハンドル）
        assert(HWPlugin_GetLatencySamples(nullptr) == 0);
        assert(!HWPlugin_IsOpen(nullptr));
        PASS("meta functions with nullptr handle -> safe defaults");
    }

    // 3. Init: 存在しないパスを明示指定 → HW_ERR_INVALID_ARG
    {
        HWResult r = HWPlugin_Init("/nonexistent/path/profile.json");
        assert(r == HW_ERR_INVALID_ARG);
        PASS("Init(nonexistent path) -> HW_ERR_INVALID_ARG");
    }

    // 4. Init: nullptr（デフォルト探索）→ プロファイルなし環境では HW_ERR_INVALID_ARG
    //    プロファイルが存在する環境では HW_OK になる場合がある
    {
        HWResult r = HWPlugin_Init(nullptr);
        printf("      Init(nullptr): %s\n",
               r == HW_OK             ? "HW_OK (profile found)" :
               r == HW_ERR_INVALID_ARG ? "HW_ERR_INVALID_ARG (no profile)" :
               r == HW_ERR_OPEN_FAILED ? "HW_ERR_OPEN_FAILED (already initialized or engine error)" :
               "other");
        // プロファイルなし環境: HW_ERR_INVALID_ARG
        // プロファイルあり環境: HW_OK または HW_ERR_OPEN_FAILED（エンジン DLL なし）
        assert(r == HW_OK || r == HW_ERR_INVALID_ARG || r == HW_ERR_OPEN_FAILED);
        PASS("Init(nullptr) -> expected result for environment");
    }

    // 5. nullptr ハンドルへの操作は常に安全
    {
        assert(!HWPlugin_IsOpen(nullptr));
        assert(HWPlugin_GetClock(nullptr) == 0);
        assert(HWPlugin_GetPanpot(nullptr) == 0);
        assert(HWPlugin_GetLatencySamples(nullptr) == 0);
        assert(HWPlugin_Write(nullptr, 0, 0)              == HW_ERR_INVALID_ARG);
        assert(HWPlugin_WriteBlock(nullptr, 0, nullptr, 0) == HW_ERR_INVALID_ARG);
        assert(HWPlugin_Reset(nullptr, 0)                 == HW_ERR_INVALID_ARG);
        HWPlugin_Close(nullptr);           // crash しないこと
        HWPlugin_SetDelaySamples(nullptr, 0); // crash しないこと
        PASS("nullptr safety");
    }

    // 6. Open: 不正 JSON → HW_ERR_INVALID_ARG（Init 状態に関わらず）
    {
        HWHandle h = nullptr;
        assert(HWPlugin_Open("not json", &h) == HW_ERR_INVALID_ARG);
        PASS("Open(invalid json) -> HW_ERR_INVALID_ARG");
    }

    // 7. Open: type != FMHWIF → HW_ERR_INVALID_ARG
    {
        HWHandle h = nullptr;
        assert(HWPlugin_Open(R"({"type":"RE1"})", &h) == HW_ERR_INVALID_ARG);
        PASS("Open(type!=FMHWIF) -> HW_ERR_INVALID_ARG");
    }

    // 8. Open: engine/chip 欠落 → HW_ERR_INVALID_ARG
    {
        HWHandle h = nullptr;
        assert(HWPlugin_Open(R"({"type":"FMHWIF"})", &h) == HW_ERR_INVALID_ARG);
        PASS("Open(no engine/chip) -> HW_ERR_INVALID_ARG");
    }

    // 9. Open: プロファイル未定義の (engine, chip) → HW_ERR_NOT_FOUND または HW_ERR_OPEN_FAILED
    {
        HWHandle h = nullptr;
        HWResult r = HWPlugin_Open(
            R"({"type":"FMHWIF","engine":"NonExistent","chip":"OPM"})", &h);
        assert(r == HW_ERR_NOT_FOUND || r == HW_ERR_OPEN_FAILED);
        assert(h == nullptr);
        PASS("Open(unlisted engine/chip) -> HW_ERR_NOT_FOUND or HW_ERR_OPEN_FAILED");
    }

    printf("\nAll %d smoke tests passed.\n", pass_count);
    return 0;
}
