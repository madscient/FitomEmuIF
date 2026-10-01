# CLAUDE.md

## コミュニケーション

- ユーザーへの報告・応答は日本語で行うこと。

## プロジェクト概要

**FitomEmuIF** は FITOM_X の `IHWPlugin` C API を実装する FM 音源エミュレーション統合
hwif プラグイン（Windows/Linux/macOS、C++17）。技術仕様（アーキテクチャ、プロファイル
JSON フォーマット、PCM/ADPCM カタログ、ライフサイクル等）は README.md を参照。

関連リポジトリ（同じ `source/repos` 配下、参照用）:
- `../FITOM_X` — FITOM_X 本体。`plugin_sdk/include/fitom/IHWPlugin.h` が正本、
  `config_schema/pcm_image_catalog.schema.json` が PCM カタログの正式スキーマ、
  `docs/plugin-hwif.md` が hwif プラグイン仕様書。
- `../YMEngine` — `FmEngineApi.h` の正本（`src/fitom/FmEngineApi.h` はそのコピー）。

**上記を含め、`source/repos` 配下の他リポジトリのファイルは、たとえ原因や修正箇所が
そちら側にあると判明しても、ユーザーの明示的な許可なく勝手に編集しないこと。**
（`../YMEngine/extern/ymfm` のような vendor submodule も同様）。調査・原因特定は
自由に行ってよいが、実際にコードを書き換える前に必ずユーザーに確認を取る。

## 重要な命名規則・変更してはいけないもの

過去のセッションで誤りが発生した箇所。**特に注意。**

| 項目 | 扱い |
|---|---|
| `"type": "FMHWIF"` | FITOM_X 側プロトコル識別子。絶対に変更しない（`FMEMUIF` 等への変更は誤り。過去に一度誤って変更し差し戻した） |
| `src/fitom/IHWPlugin.h` | FITOM_X の `plugin_sdk/include/fitom/IHWPlugin.h` の独立コピー。内容は同期させる必要があるが、ファイル名自体は変更しない |
| 環境変数 `FMEMUIF_PROFILE` / ファイル名 `fmemuif_profile.json` | プロジェクト名 `FitomEmuIF` に合わせた名称。`FMHWIF`/`fmhwif` 系の旧名には戻さない |
| ターゲット名 | `FitomEmuIF`（CMake ターゲット名・DLL 名） |

## 未解決の TODO / 既知の課題

1. **`src/fitom/IHWPlugin.h` の同期問題（未解消）**
   FITOM_X の `plugin_sdk/include/fitom/IHWPlugin.h` と内容を同一に保つ必要があるが、
   現状は独立コピーとして手動管理している（`FmEmuIfImpl.cpp` の `#include` 直後に
   TODO コメントあり）。`HWPlugin_Shutdown` 追加時もこちらへ手動追記した。
   FITOM_X 側にも同じ変更が反映されているか要確認。対処案: FITOM_X の `plugin_sdk` を
   submodule 化する、または CI で diff チェックする。
2. **FitomIFTest (`fitom_hw.dll`) 側の `HWPlugin_Init`/`HWPlugin_Shutdown` 対応状況が未確認**
   （`plugin-hwif.md` の要件は物理HW側にも同じ `IHWPlugin.h` 実装を求めている）。
3. **音声出力は未検証**。2026年10月1日に、VS2026（toolset v145、MSVC 19.51）と実 RtAudio
   （WASAPI のみ有効。submodule の記録 e5f0774 ではなく、作業ツリーにあった c0a533d）で
   Release ビルドが通った。実エンジン DLL（YMEngine の `YMFMEngine.dll`）を使って、
   `HWPlugin_Init`（オーディオストリームの起動を含む）→ `Open` → `GetClock` が動くことも
   確認した。音が出るか、正しく鳴るかは聴いていない。Linux/macOS の実機ビルドもしていない。
4. **部位ごとのゲイン調整を中継するインターフェースを後で実装する**。YMEngine に
   `FmEngine_SetPartGain` / `FmEngine_GetPartGain` が追加された（`FM_PART_FM` と
   `FM_PART_SSG`。SSG は OPN/OPNA/OPNB/OPNBB）。実機では FM と SSG の出力をボード上の回路で
   ミックスするので、音量バランスは機種で違う。これを FitomEmuIF から設定できるようにする。
   - 未定：設定の入口（プロファイルのキーか、FITOM_X から呼ぶ IHWPlugin の関数か）。
     どちらも外から見える値なので、実装前に決める
   - 他のエンジン DLL はこの関数を持たないことがある。`LOAD_SYM`（見つからないと例外）
     ではなく、任意のシンボルとして読む
   - `src/fitom/FmEngineApi.h` は追加前の版のままなので、YMEngine の最新版に同期する

## 設計判断の経緯（再度議論が必要な場合の背景）

1. 1 エンジン DLL = 1 `FmEngineHandle`。同一 DLL 内の複数チップは `AddChip` を複数回
   （エンジン内ミックス）。異なる DLL は `EngineInstance` を分離。
2. プロファイル読み込みは DLL ロード時の自動初期化ではなく、`HWPlugin_Init(profile_path)`
   を FITOM_X が明示的に呼ぶ方式を採用（DLL アンロード時のローダーロック問題を避けるため
   `HWPlugin_Shutdown` も同様に明示呼び出し・同期停止とした）。
3. RtAudio は submodule として static リンクし、FitomEmuIF.dll 単体で音声出力まで完結
   させる（FITOM_X 本体は RtAudio に非依存のまま）。
4. `generate_mutex` は撤去済み。`FmEngine_Write`/`Generate` は `FmEngineApi` 仕様上
   スレッドセーフなため、MIDI スレッドとオーディオコールバック間の排他制御は不要と判断
   （コードレビューで「mutex 保持時間が長い」と指摘され、根本原因調査の結果 mutex 自体が
   不要と判明）。
5. `HWPlugin_Open` で `pan` を上書きした場合のみ、`HWPlugin_Close` 時にプロファイルの
   `panpot` 値へ `SetGain` で戻す。
6. **PCM カタログ `images[]` のパス解決基点を、実行時カレントディレクトリ基点から
   カタログファイル自身のディレクトリ基点に変更（2026年7月20日、FITOM_staging側の
   運用検証で発覚）**。旧実装は `pcm_catalog` 自体（プロファイルファイル相対）と
   `images[]` の値（CWD相対）とで解決基点が異なっており、かつ `../FitomHwIF` の
   `PcmCatalog::load()` は最初から「カタログファイル相対」で実装されていたため、
   hwif/emuif 間でカタログ JSON の可搬性が無かった（同じカタログファイルを両方が
   参照する運用が事実上不可能だった）。`FmEmuIfImpl.cpp` の `apply_pcm_images()` を
   `FitomHwIF::PcmCatalog::load()` と同じ規則（`is_absolute() ? そのまま : catalog_dir / path`）
   に統一し、`load_engine()`/`apply_pcm_images()` にカタログディレクトリを引き回すように
   変更した。README.md・`pcm_images.catalog.example.json` の該当記述も修正済み。
7. **プロファイルの `clock` は必須。省略・0 は `HWPlugin_Init` の失敗にする**（2026年10月1日）。
   FITOM_X は `HWPlugin_GetClock` をチップの実マスタークロックとして使い、PSG のトーン周期や
   ADPCM の DeltaN を計算する。`FmEngine_AddChip` に 0 を渡すとエンジンが標準クロックを
   選ぶが、FmEngineApi にはその値を問い合わせる手段が無い。旧実装は代わりに
   `FmEngine_GetNativeRate`（サンプルレート）を返しており、FITOM_X に誤った値が渡っていた。
   標準クロックは代表値に過ぎず、FITOM_X 本体が clock=0 でインスタンスを作ることも無い。
   `GetNativeRate` は使わなくなったので、必須シンボルからも外した。
   前提：FmEngineApi に、エンジンが選んだクロックを返す API が無いこと。
   見送った案：
   - FitomEmuIF に標準クロックの表を持たせる。理由：エンジン側の表と食い違っていく
   - YMEngine に `FmEngine_GetClock` を足す。理由：FitomEmuIF が頼るには全エンジン DLL に
     同じ API が要る。標準クロックを使う運用も無い
   エラー時の戻り値は、他のプロファイル内容のエラー（`chip` 欠落、未知のチップ名）と同じ
   `HW_ERR_OPEN_FAILED`。`IHWPlugin.h` はプロファイル解析失敗を `HW_ERR_INVALID_ARG` と
   定めており、この食い違いは本変更より前からある。
