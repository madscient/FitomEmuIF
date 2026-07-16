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
    bool env_initialized = false;
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
        env_initialized = (r == HW_OK);
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

    // 6-9. Open のパラメータ検証テスト。
    //      HWPlugin_Open は「未初期化チェック」を JSON 解析より先に行うため、
    //      Init が失敗している環境（プロファイルなし等）では JSON の内容に
    //      関わらず常に HW_ERR_OPEN_FAILED を返す。
    //      Init が成功している環境（env_initialized、手順4で判定済み）でのみ、
    //      各パラメータ検証の結果が個別に現れる。

    // 6. Open: 不正 JSON
    {
        HWHandle h = nullptr;
        HWResult r = HWPlugin_Open("not json", &h);
        if (env_initialized)
            assert(r == HW_ERR_INVALID_ARG);
        else
            assert(r == HW_ERR_OPEN_FAILED);
        PASS("Open(invalid json) -> expected result for Init state");
    }

    // 7. Open: type != FMHWIF
    {
        HWHandle h = nullptr;
        HWResult r = HWPlugin_Open(R"({"type":"RE1"})", &h);
        if (env_initialized)
            assert(r == HW_ERR_INVALID_ARG);
        else
            assert(r == HW_ERR_OPEN_FAILED);
        PASS("Open(type!=FMHWIF) -> expected result for Init state");
    }

    // 8. Open: engine/chip 欠落
    {
        HWHandle h = nullptr;
        HWResult r = HWPlugin_Open(R"({"type":"FMHWIF"})", &h);
        if (env_initialized)
            assert(r == HW_ERR_INVALID_ARG);
        else
            assert(r == HW_ERR_OPEN_FAILED);
        PASS("Open(no engine/chip) -> expected result for Init state");
    }

    // 9. Open: プロファイル未定義の (engine, chip)
    {
        HWHandle h = nullptr;
        HWResult r = HWPlugin_Open(
            R"({"type":"FMHWIF","engine":"NonExistent","chip":"OPM"})", &h);
        if (env_initialized)
            assert(r == HW_ERR_NOT_FOUND || r == HW_ERR_OPEN_FAILED);
        else
            assert(r == HW_ERR_OPEN_FAILED);
        assert(h == nullptr);
        PASS("Open(unlisted engine/chip) -> expected result for Init state");
    }

    // 10. Shutdown: 未実装環境でも呼べる／複数回呼んでも安全
    {
        HWPlugin_Shutdown();
        HWPlugin_Shutdown(); // 二重呼び出しでも crash しないこと
        PASS("Shutdown safe to call (including twice)");
    }

    printf("\nAll %d smoke tests passed.\n", pass_count);
    return 0;
}
