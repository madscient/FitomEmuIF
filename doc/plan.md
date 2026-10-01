# plan.md

AI 向けの作業文書。計画、進捗、未解決の TODO と既知の課題、設計判断の経緯、作業記録を
ここに集める。現在の技術仕様（利用者・開発者向け）は README.md、リポジトリのルールは
CLAUDE.md にある。

確度の書き方：走らせて確かめたものは「確認済み」と何で確かめたかを添える。作ったが
動かしていないものは「未検証」。コードを読んだだけの見立ては、その旨と読んだ箇所を添える。

## プロジェクト概要

**FitomEmuIF** は FITOM_X の `IHWPlugin` C API を実装する FM 音源エミュレーション統合
hwif プラグイン（Windows/Linux/macOS、C++17）。`FmEngineApi` 互換のエンジン DLL を複数束ね、
RtAudio で音声を出力する。技術仕様（アーキテクチャ、プロファイル JSON フォーマット、
PCM/ADPCM カタログ、ライフサイクル等）は README.md を参照。

## 関連リポジトリ

同じ `source/repos` 配下にあり、参照用。編集のルールは CLAUDE.md を参照。

- `../FITOM_X` — FITOM_X 本体。`plugin_sdk/include/fitom/IHWPlugin.h` が正本、
  `config_schema/pcm_image_catalog.schema.json` が PCM カタログの正式スキーマ、
  `docs/plugin-hwif.md` が hwif プラグイン仕様書。
- `../YMEngine` — `FmEngineApi.h` の正本（`src/fitom/FmEngineApi.h` はそのコピー）。
  変更の経緯は `doc/CHANGELOG.md`。
- `../FitomHwIF` — 物理 HW 側の hwif。PCM カタログのパス解決規則をこちらと揃えている
  （設計判断 6）。
- `../FitomIFTest` — `fitom_hw.dll`（TODO 2）。
- `../FITOM_staging` — 運用プロファイル。FitomEmuIF 用は
  `config/profiles/hw_plugins/fmemuif_*.json`。

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
   （c0a533d、WASAPI のみ有効）で Release ビルドが通った。実エンジン DLL（YMEngine の `YMFMEngine.dll`）を使って、
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
5. **`HWPlugin_Reset` の後、OPN/OPNA の prescale が 2 になる（対応は未判断）**。
   `HWPlugin_Reset` は port 0/1 のレジスタ 0x00〜0xFF に順に 0 を書く。ymfm は 0x2D/0x2E/0x2F
   へのアドレス書き込みで prescale を 6→3→2 と切り替える（`ymfm_opn.cpp` の
   `ym2203::write_address` / `ym2608::write_address`）ので、リセット後の FM は標準の3倍の
   速さで動く。コードを読んだ見立てで、走らせてはいない。YMEngine の更新前も同じ挙動
   （上流の `generate()` も prescale に従う）。更新後は prescale が変わるたびにリサンプラを
   設定し直し、その瞬間の音は YMEngine 側でも試験されていない。
   現状は表に出ない：FITOM_X の core と apps を `->reset()` で検索した範囲では
   `HWPort::reset()` を能動的に呼ぶ箇所が無く（ポートラッパーの転送のみ）、core/src に
   0x2D〜0x2F を書く OPN ドライバも無かった。
   対処案：OPN 系ではループで 0x2D〜0x2F を飛ばす、または最後に 0x2D を書いて prescale 6 に戻す。
6. **スモークテストのパスが CMakeLists.txt と合っていない**。`BUILD_FITOMEMUIF_TEST` の
   `add_executable` は `src/smoke_test.cpp` を指すが、ファイルは `test/smoke_test.cpp` にある。
   既定は OFF なので表に出ていない。ON にしたときに失敗するかは試していない（未検証）。
7. **`HWPlugin_Init` のエラー戻り値が `IHWPlugin.h` の定めと食い違う**。プロファイル内容の
   エラーも `HW_ERR_OPEN_FAILED` を返すが、`IHWPlugin.h` はプロファイル解析失敗を
   `HW_ERR_INVALID_ARG` と定めている（設計判断 7）。
8. **`audio_api` の `asio` / `ds` が既定のビルドでは使えない**。README.md と
   `fmemuif_profile.example.json` は指定できる値として載せているが、RtAudio の
   `RTAUDIO_API_ASIO` / `RTAUDIO_API_DS` は既定で OFF で、FitomEmuIF の CMakeLists.txt も
   ON にしていない。RtAudio のコンストラクタ（`RtAudio.cpp` の `RtAudio::RtAudio`）は、指定した
   API が組み込まれていないと標準エラーに警告を出し、組み込まれた API（Windows では WASAPI）に
   切り替える。FitomEmuIF はエラーコールバックを渡していないので、警告は FITOM_X 側から
   見えにくい。コードを読んだ見立てで、走らせてはいない。
   対処案：README にビルドオプションが要ることを書く、または指定した API が
   `RtAudio::getCompiledApi` に無ければ Init を失敗させる。後者は外から見える挙動の変更。
   ASIO を有効にするなら、RtAudio は c0a533d 以降が要る（作業記録「RtAudio を c0a533d に更新」）。

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

## 作業記録

### 2026年10月1日 YMEngine 更新（d3e2969〜7fad830）の影響確認

対象は FitomEmuIF の前回コミット（2026年8月16日）より後の YMEngine の変更。確認時点の
`src/fitom/FmEngineApi.h` は YMEngine の d0680be と同一だった（diff で確認済み）。

- **API**：ヘッダの変更は追加のみ（`FmPart`、`FmEngine_SetPartGain` / `GetPartGain`）。
  既存関数のシグネチャと `.def` のエクスポートは変わっていない（diff で確認済み）。
  更新後の YMEngine DLL で FitomEmuIF を走らせてはいない（手元の `YMFMEngine.dll` は
  2026年8月17日のビルド）。
- **`FmEngine_GetNativeRate`**：OPN 系では FM 部のレートを返すようになった（OPNA 標準
  クロックで 998,400 → 55,466）。FitomEmuIF は `clock` 省略時にこの値を `HWPlugin_GetClock`
  で返していたため、設計判断 7 で `clock` を必須にして使わなくした。
- **`LinearResampler` の修正（全チップ）**：呼び出しごとにソースを読み捨てていた穴が
  塞がれ、全チップの出力が変わる（YMEngine の CHANGELOG による。旧実装は 240 サンプルずつ
  の呼び出しで +0.60%）。FitomEmuIF はオーディオコールバックごとに `FmEngine_Generate` を
  呼ぶので影響を受けていた。FitomEmuIF の `buffer_frames` でのずれ量は測っていない。
  FitomEmuIF 側の変更は不要。
- **OPN 系の FM/SSG 分離**：既定の SSG 音量は据え置き（YMEngine 側の試験による。こちらでは
  測っていない）。部位ごとのゲインは TODO 4。
- **d3e2969（キー衝突時の書き込み保留）**：同じチャンネルのキー状態が衝突した書き込みは、
  1回あたり最大約2ms遅れて適用される。この遅れは `HWPlugin_GetLatencySamples`
  （`buffer_frames`）に含まれない。FitomEmuIF 側での扱いは検討していない。
- 副次的に見つけた既存の課題：TODO 5（`HWPlugin_Reset` の prescale）、TODO 6、TODO 7。

### 2026年10月1日 RtAudio を e5f0774 から c0a533d に更新

どちらも 6.0.1 より後のタグなしコミット（6.0.1-72 と 6.0.1-83）。間の変更と影響
（差分を読んで判断。新旧で動かし比べてはいない）：

- #486 ASIO の `bufferSwitchTimeInfo` コールバックを NULL のままにしない。NULL だと、
  それを呼ぶドライバでプロセスが落ちる（上流のコミットメッセージによる。こちらでは
  試していない）。ASIO を組み込んだビルドでだけ効く。今のビルドは `RTAUDIO_API_ASIO=OFF`
- #474 PulseAudio のコードの変数名変更（GCC `-Wshadow` 対策）。挙動は変わらない
- #482 CMake オプション `RTAUDIO_INSTALL`（既定 ON で従来と同じ）
- #484 テストの DLL コピー（`RTAUDIO_BUILD_TESTING=OFF` なので無関係）
- #487 pkg-config の pthread フラグ（`rtaudio.pc` のみ。インストール時だけ関係）

今のビルド構成では挙動に効く変更は無く、取り込みは必須ではなかった。それでも更新した
理由：この日のビルドと Init の確認（TODO 3）は c0a533d で行っており、記録と確かめた版を
揃えられる。ASIO を有効にするときに要る修正も入る。
前提：ASIO を組み込まない限り、e5f0774 と挙動は同じ。
見送った案：記録どおり e5f0774 に戻す。理由：挙動は同じで、確かめた版と記録がずれる。
