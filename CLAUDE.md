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
3. **実エンジン DLL・実 RtAudio submodule でのビルド・音声出力は未検証**
   （検証済みなのは構文チェックとスタブ構成でのビルド・スモークテストのみ）。
4. **VS2026 (toolset v145) 実機ビルドは未検証**（Linux + CMake 3.28 でのロジック検証のみ）。

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
