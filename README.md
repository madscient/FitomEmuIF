# FitomEmuIF

`IHWPlugin` C API を実装した FM エンジン統合 hwif プラグイン。  
`FmEngineApi` 互換 DLL を複数束ね、RtAudio で PCM をオーディオデバイスへ出力する。

## アーキテクチャ

```
FITOM core
  └── HWPort (IPort アダプター)
        └── IHWPlugin C API  ← FitomEmuIF.dll（このライブラリ）
              ├── PluginRegistry  (static singleton, HWPlugin_Init で確定・以後不変)
              │     ├── EngineInstance [YMEngine.dll]
              │     │     ├── FmEngineHandle  (FmEngine_Create)
              │     │     ├── ChipSlot[0]  OPM  chip_id=0
              │     │     ├── ChipSlot[1]  OPNA chip_id=1
              │     │     └── pcm_images   (SetMemory 用データ、エンジンと同寿命)
              │     ├── EngineInstance [OPLEngine.dll]
              │     │     ├── FmEngineHandle
              │     │     └── ChipSlot[0]  OPL3 chip_id=0
              │     └── RtAudio (1 ストリーム, ステレオ float32)
              │           └── audio_callback
              │                 ├── YMEngine.FmEngine_Generate  ┐ 加算
              │                 └── OPLEngine.FmEngine_Generate ┘ ミックス
              └── HWPlugin_Open → HWDeviceOpaque (ChipSlot への参照)
```

### スレッドモデル

| スレッド | 操作 | 排他制御 |
|---|---|---|
| MIDI 処理スレッド | `HWPlugin_Write` → `FmEngine_Write` | なし |
| RtAudio コールバック | `FmEngine_Generate` | なし |

`FmEngine_Write` / `FmEngine_Generate` は `FmEngineApi` の仕様上どちらもスレッドセーフで、
同時に呼び出しても安全なため、mutex 等の明示的な排他制御は行っていない
（`FmEngine_SetMemory` はこれらと異なりスレッドセーフではないため、
オーディオストリーム開始前の一度きりの呼び出しに限定している。後述）。

## プロファイル JSON

ファイル名 `fmemuif_profile.json` を以下の場所に置く（優先順）:

1. 環境変数 `FMEMUIF_PROFILE` で指定したパス
2. `FitomEmuIF.dll` と同じディレクトリ
3. カレントディレクトリ

### フォーマット

```jsonc
{
  "sample_rate":   44100,      // 全エンジン共通サンプルレート
  "buffer_frames": 512,        // RtAudio バッファサイズ兼レイテンシ申告値
  "audio_api":     "auto",     // RtAudio API 名（下表参照）
  "audio_device":  "",         // デバイス名部分一致。空文字でデフォルトデバイス
  "pcm_catalog":   "pcm_images.catalog.json",  // PCM/ADPCM イメージカタログへのパス（省略可、下記参照）

  "engines": [
    {
      "dll": "YMEngine",        // FmEngineApi 互換 DLL 名（拡張子省略可）
      "chips": [
        { "chip": "OPM",  "clock": 3579545, "pan": 0 },
        { "chip": "OPNA", "clock": 7987200, "pan": 0 }
      ]
    },
    {
      "dll": "OPLEngine",
      "chips": [
        { "chip": "OPL3", "clock": 14318181, "pan": 0 }
      ]
    }
  ]
}
```

#### audio_api 値一覧

| 値 | バックエンド |
|---|---|
| `auto` / `unspecified` | OS に合わせて自動選択（推奨） |
| `wasapi` | Windows WASAPI |
| `asio` | Windows ASIO |
| `ds` / `directsound` | Windows DirectSound |
| `core` / `coreaudio` | macOS Core Audio |
| `alsa` | Linux ALSA |
| `pulse` / `pulseaudio` | Linux PulseAudio |
| `jack` | Linux / macOS JACK |

#### chips フィールド

| フィールド | 省略 | 説明 |
|---|---|---|
| `chip` | 必須 | チップ種別（`OPM`, `OPNA`, `OPL3` 等） |
| `clock` | 0 = 標準クロック | マスタークロック [Hz] |
| `pan` | 0 | 0=Stereo, 1=L only, 2=R only |

同一エンジン DLL に複数チップを列挙すると、1 つの `FmEngine` インスタンスに
`FmEngine_AddChip` を複数回呼ぶ。エンジン内部でミックスされる。  
異なるエンジン DLL のチップは、オーディオコールバック内で加算ミックスされる。

同一チップ種別を複数持つ場合は同じ `chip` 名を複数回書く。
`HWPlugin_Open` の `index` フィールド（0 始まり）で区別する。

#### PCM/ADPCM イメージカタログ (`pcm_catalog`)

`pcm_catalog` はプロファイルファイルからの相対パスで指定する（省略可）。
指定した場合、`engines[].chips` のロード時に読み込まれ、チップ種別に応じて
自動的に `FmEngine_SetMemory` が呼ばれる。フォーマットは FITOM_X の
`config_schema/pcm_image_catalog.schema.json`（`*.pcm_image_catalog.json` 慣習）に
準拠する:

```jsonc
{
  "images": {
    "ADPCM-A":      "samples/ym2610b_adpcma.bin",
    "ADPCM-B":      "samples/opna_adpcmb.bin",
    "OPNB_ADPCM-B": "samples/ym2610b_adpcmb.bin",
    "OPNA_RHYTHM":  "samples/opna_rhythm_rom.bin",
    "OPL4AWM":      "samples/opl4_awm_rom.bin"
  }
}
```

`images` の各値（イメージファイルへのパス）は、絶対パスならそのまま、相対パスなら
**カタログファイル自身のディレクトリ**を基準に解決される（`pcm_catalog` がプロファイル
ファイルからの相対パスで解決されるのと同じ考え方。FitomHwIF の `PcmCatalog::load()` と
同じ規則に統一している、2026年7月〜。旧実装は実行時カレントディレクトリ基点だったため、
hwif/emuif 間でカタログの可搬性が無かった）。

チップ種別ごとに参照するカタログキーと `FmMemoryType` の対応（`FmEmuIfImpl.cpp` の
`pcm_mappings_for_chip()` にハードコードされている）:

| チップ | カタログキー | FmMemoryType |
|---|---|---|
| OPNA / YM2608 | `ADPCM-B` | `FM_MEM_ADPCM_B` |
| OPNA / YM2608 | `OPNA_RHYTHM` | `FM_MEM_ADPCM_A`（内蔵リズム音源 ROM） |
| OPNB/OPNBB / YM2610/B | `ADPCM-A` | `FM_MEM_ADPCM_A` |
| OPNB/OPNBB / YM2610/B | `OPNB_ADPCM-B` | `FM_MEM_ADPCM_B` |
| Y8950 | `ADPCM-B` | `FM_MEM_ADPCM_B` |
| OPL4 / YMF278 | `OPL4AWM` | `FM_MEM_PCM` |

OPNB/OPNBB の ADPCM-B は OPNA/Y8950 とメモリのバウンダリ（アドレッシング境界）が異なるため、
`ADPCM-B` を共有せず専用の `OPNB_ADPCM-B` キーを使う（FITOM_X スキーマの規約と同じ）。

カタログにエントリがない種別はスキップされる（エラーにしない）。対応表にないチップ種別
（OPM, OPL3 等 PCM メモリを持たないチップ）には影響しない。同一エンジン内で同じチップ種別を
複数回使う場合、イメージデータはカタログキー単位で一度だけ読み込まれ、`EngineInstance` 内で
チップと同寿命のバッファとして共有される。

`pcm_catalog` 自体が開けない・パースできない場合や、`images` 内の個々のファイルが開けない・
読み込めない・`FmEngine_SetMemory` が失敗する場合も、`HWPlugin_Init` 自体は失敗させない。
該当するケーパビリティ（カタログ全体、またはそのカタログキー）を静かに無効化するだけで、
他のチップ・エンジンの初期化とオーディオ出力は継続する。

`FmEngine_SetMemory` は `FmEngineApi` の仕様上スレッドセーフではなく、オーディオストリーム
開始前に呼ぶ必要があるため、`HWPlugin_Init` の中で `load_engine()`（チップ追加 → PCM イメージ
適用）を全エンジン分終えたあとに RtAudio ストリームを起動する順序になっている。

### 初期化・終了処理のライフサイクル

`PluginRegistry` は `static` シングルトンだが、**DLL ロード時点では自動初期化しない**。
FITOM が `HWPlugin_Init(profile_path)` を明示的に呼んだ時点で初めてプロファイル
（および `pcm_catalog`）を読み込み、各エンジン DLL のロード・チップ追加・PCM イメージ適用を
行い、RtAudio ストリームを起動する。

```
1. FITOM が DLL をロード（この時点では PluginRegistry は未初期化）
2. FITOM が HWPlugin_Init(profile_path) を呼ぶ
     → プロファイル/カタログ読み込み・エンジン起動・RtAudio ストリーム開始が確定
     → 以後 FITOM とのリンク中は構成不変（プロファイルの変更は反映されない）
3. HWPlugin_Enumerate / Open / Write / ... の通常運用
4. FITOM がプロセス終了前に HWPlugin_Shutdown() を一度だけ呼ぶ
     （任意実装だが強く推奨。未実装でも FITOM 側が GetProcAddress/dlsym で
       探索し、見つからなければスキップする）
     → RtAudio ストリームを同期的に stop/close する
       （呼び出しから戻った時点でオーディオコールバックスレッドは join 済み）
```

`HWPlugin_Init` が呼ばれる前（または失敗した後）に他の関数を呼んだ場合はすべて安全な
失敗値を返す: `HWPlugin_Enumerate` → `nullptr`、`HWPlugin_Open` → `HW_ERR_OPEN_FAILED`、
`HWPlugin_GetLatencySamples` → `0` 等。`HWPlugin_Shutdown` は複数回呼んでも安全
（2 回目以降は no-op）。

## ビルド

### 前提: RtAudio submodule の追加

```sh
git submodule add https://github.com/thestk/rtaudio.git extern/rtaudio
git submodule update --init --recursive
```

### ビルド手順

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

テストも含める場合:

```sh
cmake -B build -DBUILD_FITOMEMUIF_TEST=ON
cmake --build build
./build/FitomEmuIF_test
```

### VS2026 (CMake 4.x) 対応について

`extern/rtaudio` は `cmake_minimum_required(VERSION 3.0)` を宣言しているため、
CMake 4.x（VS2026 同梱版）では `add_subdirectory` 時に configure エラーとなる。
`CMakeLists.txt` は RtAudio の `add_subdirectory` 実行中だけ `CMAKE_POLICY_VERSION_MINIMUM`
を一時的に緩和してこれを回避している（自プロジェクト自体のポリシーには影響しない）。
また MSVC ビルドでは `CMAKE_MSVC_RUNTIME_LIBRARY` を明示し、FitomEmuIF と RtAudio 間で
ランタイムライブラリが食い違って `LNK2038` になるのを防いでいる。

### 依存一覧

| 依存 | 取得方法 |
|---|---|
| `nlohmann_json` | システムインストール or FetchContent (v3.11.3) |
| RtAudio | `extern/rtaudio` submodule（static リンク） |
| OS オーディオライブラリ | CMake が自動検出（ALSA / PulseAudio / WASAPI 等） |
| `FmEngineApi` 互換 DLL | 実行時ロード（ビルド時依存なし） |

## HWPlugin_Open params_json

```json
{ "type": "FMHWIF", "engine": "YMEngine", "chip": "OPM", "index": 0, "pan": 0 }
```

| フィールド | 省略 | 説明 |
|---|---|---|
| `engine` | 必須 | プロファイルの `engines[].dll` と一致する名前 |
| `chip` | 必須 | プロファイルの `chip` と一致する名前 |
| `index` | 0 | 同種チップが複数ある場合の通し番号 |
| `pan` | プロファイル値 | 省略時はプロファイルの `pan` を引き継ぐ |

プロファイルに定義されていない `(engine, chip, index)` は `HW_ERR_NOT_FOUND`。

`pan` を指定して Open した場合、`HWPlugin_Close` 時にプロファイル定義の `pan` 値へ
自動的に戻る（`FmEngine_SetGain` を呼び直す）。`pan` を省略して Open した場合
（プロファイル値をそのまま使った場合）は Close 時に何もしない。

## fitom.conf.json 記述例

```json
"hw_plugin": {
  "dll": "FitomEmuIF.dll"
}
```

## addr マッピング

| bits | 意味 | FmEngine_Write 引数 |
|---|---|---|
| `addr >> 8` | ポート番号（OPNA/OPL3 の Bank 等） | `port` |
| `addr & 0xFF` | レジスタアドレス | `reg` |

## レイテンシ同期

`HWPlugin_GetLatencySamples` は、RtAudio の `openStream` 後に確定した
実際の `buffer_frames` を返す（デバイス制約で変更されることがある）。  
FITOM コアが全デバイスの最大レイテンシを決定し `HWPlugin_SetDelaySamples` で通知するが、
FitomEmuIF は `buffer_frames == delay_samples` となるよう設計されているため no-op。
