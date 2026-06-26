// test/smoke_test.cpp
// fitom_fmhwif スモークテスト
// プロファイルなし・実エンジン DLL なしで動くエラーパステスト。

#include "fitom/IHWPlugin.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static int pass_count = 0;
#define PASS(msg) do { printf("[PASS] %s\n", (msg)); ++pass_count; } while(0)

int main() {
    // 1. プラグイン名
    {
        const char* name = HWPlugin_GetName();
        assert(name && std::strcmp(name, "FitomEmuIF") == 0);
        PASS("HWPlugin_GetName == \"FitomEmuIF\"");
    }

    // 2. デバイス列挙 - プロファイルなし環境では空配列
    {
        const char* j = HWPlugin_Enumerate();
        assert(j != nullptr);
        // 空配列 [] か、プロファイルがあれば非空
        printf("      HWPlugin_Enumerate: %s\n", j);
        HWPlugin_FreeString(j);
        PASS("HWPlugin_Enumerate returns non-null");
    }

    // 3. nullptr ハンドルでの nullptr 安全性
    {
        assert(!HWPlugin_IsOpen(nullptr));
        assert(HWPlugin_GetClock(nullptr) == 0);
        assert(HWPlugin_GetPanpot(nullptr) == 0);
        assert(HWPlugin_GetLatencySamples(nullptr) == 0);
        assert(HWPlugin_Write(nullptr, 0, 0)       == HW_ERR_INVALID_ARG);
        assert(HWPlugin_WriteBlock(nullptr, 0, nullptr, 0) == HW_ERR_INVALID_ARG);
        assert(HWPlugin_Reset(nullptr, 0)          == HW_ERR_INVALID_ARG);
        HWPlugin_Close(nullptr);           // crash しないこと
        HWPlugin_SetDelaySamples(nullptr, 0); // crash しないこと
        PASS("nullptr safety");
    }

    // 4. 不正 JSON → HW_ERR_INVALID_ARG
    {
        HWHandle h = nullptr;
        assert(HWPlugin_Open("not json", &h) == HW_ERR_INVALID_ARG);
        assert(h == nullptr);
        PASS("Open(invalid json) -> HW_ERR_INVALID_ARG");
    }

    // 5. type != FMHWIF → HW_ERR_INVALID_ARG
    {
        HWHandle h = nullptr;
        assert(HWPlugin_Open(R"({"type":"RE1"})", &h) == HW_ERR_INVALID_ARG);
        PASS("Open(type!=FMHWIF) -> HW_ERR_INVALID_ARG");
    }

    // 6. engine/chip 欠落 → HW_ERR_INVALID_ARG
    {
        HWHandle h = nullptr;
        assert(HWPlugin_Open(R"({"type":"FMHWIF"})", &h) == HW_ERR_INVALID_ARG);
        PASS("Open(no engine/chip) -> HW_ERR_INVALID_ARG");
    }

    // 7. プロファイル未定義の (engine, chip) → HW_ERR_NOT_FOUND または HW_ERR_OPEN_FAILED
    //    （プロファイルなし環境では HW_ERR_OPEN_FAILED、プロファイルあり環境では HW_ERR_NOT_FOUND）
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
