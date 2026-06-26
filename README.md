# FitomEmuIF

`IHWPlugin` C API を実装した FM エンジン統合 hwif プラグイン。  
`FmEngineApi` 互換 DLL を複数束ね、RtAudio で PCM をオーディオデバイスへ出力する。

## アーキテクチャ

```
FITOM core
  └── HWPort (IPort アダプター)
        └── IHWPlugin C API  ← FitomEmuIF.dll（このライブラリ）
              ├── PluginRegistry  (static singleton, DLL ロード時確定・以後不変)
              │     ├── EngineInstance [YMEngine.dll]
              │     │     ├── FmEngineHandle  (FmEngine_Create)
              │     │     ├── ChipSlot[0]  OPM  chip_id=0
              │     │     └── ChipSlot[1]  OPNA chip_id=1
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

| スレッド | 操作 | ロック |
|---|---|---|
| MIDI 処理スレッド | `HWPlugin_Write` → `FmEngine_Write` | `EngineInstance::generate_mutex` |
| RtAudio コールバック | `FmEngine_Generate` | `EngineInstance::generate_mutex` |

`generate_mutex` はエンジン DLL ごとに独立するため、異なる DLL への Write は並行実行される。

## プロファイル JSON

ファイル名 `fmhwif_profile.json` を以下の場所に置く（優先順）:

1. 環境変数 `FMHWIF_PROFILE` で指定したパス
2. `FitomEmuIF.dll` と同じディレクトリ
3. カレントディレクトリ

### フォーマット

```jsonc
{
  "sample_rate":   44100,      // 全エンジン共通サンプルレート
  "buffer_frames": 512,        // RtAudio バッファサイズ兼レイテンシ申告値
  "audio_api":     "auto",     // RtAudio API 名（下表参照）
  "audio_device":  "",         // デバイス名部分一致。空文字でデフォルトデバイス

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

### 構成確定タイミング

`PluginRegistry` は `static` シングルトン。DLL がプロセスにロードされた時点で
プロファイルを読み込み、エンジンを起動し、RtAudio ストリームを開始する。  
FITOM とのリンク中はプロファイルの変更は反映されない。

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
