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
  `docs/interfaces/plugin-hwif.md` が hwif プラグイン仕様書。
- `../FMEngineTest` — FmEngineApi の仕様書（`docs/FmEngineApi.md`）と C ヘッダの正本
  （`include/FmEngineApi.h`）。`src/fitom/FmEngineApi.h` はその写しで、直接編集しない。
  仕様の変更の経緯と、各エンジン・アプリケーションに要る対応は `docs/CHANGELOG.md`。
- `../YMEngine` — エンジン DLL のひとつ（`YMFMEngine.dll`）。変更の経緯は `doc/CHANGELOG.md`。
  2026年10月3日までは、ここの `src/FmEngineApi.h` がヘッダの正本だった。
- `../FitomHwIF` — 物理 HW 側の hwif。PCM カタログのパス解決規則をこちらと揃えている
  （設計判断 6）。
- `../FitomIFTest` — `fitom_hw.dll`（TODO 2）。
- `../DSAemuEngine` — エンジン DLL のひとつ。Y8950 と Y8960 の各部（`OPL2EX` / `OPLLEX` /
  `SSGEX` / `SCCEX`）。外部メモリの名前は README の表にある。
- `../EPSGemuEngine` — エンジン DLL のひとつ。SSGS / SSGS2 / SSGS3 / AMMS-A / PCMD8。外部メモリの
  名前はどれも `PCM`（`72f70d6`）。
- `../FITOM_staging` — 運用プロファイル。FitomEmuIF 用は
  `config/profiles/hw_plugins/fmemuif_*.json`。

## 未解決の TODO / 既知の課題

1. **`src/fitom/IHWPlugin.h` を正本と同一に保つ仕組みが無い**
   FITOM_X の `plugin_sdk/include/fitom/IHWPlugin.h`（正本）の写しを、手動で管理している
   （`FmEmuIfImpl.cpp` の `#include` 直後に TODO コメントあり）。
   2026年10月25日に、写しを FITOM_X `faaf183` の正本と同一にした（diff で一致。確認済み）。
   正本が変わったら、写しをまるごと差し替える。
   対処案: FITOM_X の `plugin_sdk` を submodule 化する、または CI で diff チェックする。
   `src/fitom/FmEngineApi.h`（正本は FMEngineTest）にも同じ課題がある。
2. **FitomIFTest (`fitom_hw.dll`) 側の `HWPlugin_Init`/`HWPlugin_Shutdown` 対応状況が未確認**
   （`plugin-hwif.md` の要件は物理HW側にも同じ `IHWPlugin.h` 実装を求めている）。
3. **音声出力は未検証**。2026年10月1日に、VS2026（toolset v145、MSVC 19.51）と実 RtAudio
   （c0a533d、WASAPI のみ有効）で Release ビルドが通った。実エンジン DLL（YMEngine の `YMFMEngine.dll`）を使って、
   `HWPlugin_Init`（オーディオストリームの起動を含む）→ `Open` → `GetClock` が動くことも
   確認した。音が出るか、正しく鳴るかは聴いていない。Linux/macOS の実機ビルドもしていない。
   2026年10月25日に、オフライン描画（音声デバイスを通らない）で実エンジンの出力に音が
   あることは数値で確かめた（作業記録「マスターボリュームとオフライン描画を実装」）。
   音声デバイスへ出す経路は、引き続き聴いていない。
4. **FmEngineApi の改訂（部位と外部メモリの名前指定）への追従の残り**。FitomEmuIF 側の
   実装は済んだ（設計判断 8・9、作業記録「FmEngineApi の改訂に追従し、部位ゲインの
   インターフェースを追加」）。残っているのは次のとおり。
   - **FITOM_X と実物の FitomEmuIF を組み合わせて動かしていない**。FITOM_X 側の対応は
     `4ab7a56` で入った（正本の `IHWPlugin.h` の宣言、`HWPort` の中継、プロファイルの
     `part_gains` への保存、GUI のスライダー）。宣言と契約が FitomEmuIF の実装と合うことは、
     読んで突き合わせた（作業記録「FITOM_X の正本ヘッダとの突き合わせ」）。動かしたのは、
     FITOM_X は検証用プラグイン、FitomEmuIF は検証用エンジンで、それぞれ別々にだけ
   - **新しい形を実装した実エンジンでは走らせていない**。2026年10月3日の時点で、追従を
     終えたエンジンが無い。確かめたのは検証用エンジン（`test/stub_engine.cpp`）と、
     改訂前の実エンジンまで。追従したエンジンが出たら、`relay_current` の内容を実エンジンで
     確かめる（部位の名前・既定値と、ROM が音に反映されること）
   - **追従していないエンジンには ROM が渡らない**。`FmEngine_GetMemoryCount` を持たない
     DLL は外部メモリを持たないものとして扱うので、追従するまで ADPCM・リズム・AWM は
     鳴らない（仕様側の前提。FMEngineTest の CHANGELOG「外部メモリを名前で指定する」）
   - **この変更より前にビルドした FitomEmuIF を、追従後のエンジンと組み合わせない**。
     `FmEngine_SetMemory` に番号を渡し、エンジンはそれをポインタとして読む。FITOM_staging の
     DLL を入れ替えるときは、FitomEmuIF をエンジンより先か同時に入れ替える
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
6. **（解消、2026年10月3日）スモークテストのパスが CMakeLists.txt と合っていなかった**。
   `add_executable` を `test/smoke_test.cpp` に直した。`BUILD_FITOMEMUIF_TEST=ON` で
   ビルドでき、12 件が通る（確認済み。MSVC 19.51、x64、Release）。
   あわせて見つけて直したこと：`smoke_test.cpp` は `assert` で判定するので、Release 構成では
   `NDEBUG` で判定がすべて消えていた。テストのターゲットだけ `NDEBUG` を外した（exe が
   `_wassert` を import していることを dumpbin で確認済み）。
   残り：手順 4 の `HWPlugin_Init(nullptr)` は、既定の場所にプロファイルがあるかどうかで
   結果が変わる。プロファイルがある環境では走らせていない。
7. **`HWPlugin_Init` のエラー戻り値が `IHWPlugin.h` の定めと食い違う**。プロファイル内容の
   エラーも `HW_ERR_OPEN_FAILED` を返すが、`IHWPlugin.h` はプロファイル解析失敗を
   `HW_ERR_INVALID_ARG` と定めている（設計判断 7）。
   `HWPlugin_InitOffline` も同じ。加えて、`IHWPlugin.h` は扱えない `sample_rate` を
   `HW_ERR_INVALID_ARG` と定めているが、FitomEmuIF が見分けられるのは 0 だけで、エンジンが
   `FmEngine_Create` に失敗するレートは `HW_ERR_OPEN_FAILED` になる（設計判断 12）。
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
   エラーコールバックを渡すようにするなら、RtAudio を ea2c88c 以降にする（作業記録
   「RtAudio ea2c88c（#489）の取り込みを見送り」）。

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
   2026年10月3日の追記：FmEngineApi の仕様でも clock=0 は `FM_ERR_INVALID_ARG` になった
   （FMEngineTest `a17c372`）。この判断は仕様と同じ向きで、上の前提が変わっても成り立つ。
   0 を標準クロックとして受け付けるエンジンが残っている（YMEngine `89cbae1` のヘッダの
   記述による。走らせてはいない）ので、FitomEmuIF 側の検査は残す。
8. **部位ごとのゲインの入口は、IHWPlugin の任意関数 4 本にする**（2026年10月3日）。
   `HWPlugin_GetPartCount` / `GetPartName` / `SetPartGain` / `GetPartGain`。FmEngineApi の
   部位ゲイン 4 関数の `(engine, chip_id)` を `HWHandle` に置き換えた形で、部位は名前の
   文字列で指定する。仕様は README.md「部位ごとのゲイン」。
   利用者と決めたこと：接頭辞を `HWPlugin_` にし、宣言を `src/fitom/IHWPlugin.h` に置く。
   FITOM_X は `HWPlugin_GetPartCount` を `symOptional` で探し、あれば残りの 3 つを必須として
   読む（FITOM_X `4ab7a56` の `core/src/HWPort.cpp` の `HWPluginInstance::load()`。組の先頭が
   あるのに残りが欠けるプラグインは、ロードに失敗する）。
   こちらで決めたこと（利用者と明示的には決めていない。変えるときは、該当する関数 1 か所と
   README の該当行、`test/engine_relay_test.cpp` の該当行で済む）：
   - 部位の一覧と既定値は、`HWPlugin_Init` の中（`FmEngine_AddChip` の直後）でエンジンから
     読んで `ChipSlot::parts` に控える。`HWPlugin_GetPartCount` / `GetPartName` は控えを返す
   - `HWPlugin_GetPartName` の文字列は `HWPlugin_Close` まで有効と約束する（実際の寿命は
     `PluginRegistry` と同じで、約束より長い）
   - 設定したゲインは `HWPlugin_Close` で既定値に戻す。pan（設計判断 5）と同じ扱い。
     `HWPlugin_SetPartGain` が 1 回でも成功したハンドルだけ、そのチップの全部位を戻す
   - `HWPlugin_GetPartGain` は控えではなくエンジンに問い合わせる
   - 控えに無い名前は、エンジンに通さず `HW_ERR_INVALID_ARG` を返す。部位ゲインの関数を
     持たないエンジンでは控えが空なので、関数ポインタの有無を別に調べなくて済む
   - エンジンが `FM_ERR_INVALID_ARG` を返したら `HW_ERR_INVALID_ARG`、それ以外の失敗は
     `HW_ERR_IO`
   - 名前を返さない部位と、ゲインを読めない部位は控えに載せない（既定値が分からず、
     Close で戻せない）
   前提：
   - FmEngineApi が部位を名前で指定すること（FMEngineTest `0c22d67` の仕様）
   - 1 つのチップを同時に開けるハンドルが 1 つであること（`ChipSlot::in_use`）。Close で
     既定値に戻す扱いは、これに依る
   - `FmEngine_SetPartGain` を、別スレッドの `FmEngine_Write` と同時に呼んでよいかは、仕様書に
     書かれていない（書かれているのはオーディオコールバックとの並行だけ）。README には
     仕様書にある範囲だけを書いた。
     FITOM_X（`4ab7a56`）は、演奏中の `HWPlugin_SetPartGain` / `GetPartGain` を MIDI 処理・
     タイマーと同じロックで直列化するので、`HWPlugin_Write` とは並行しない（FITOM_X の
     `docs/interfaces/plugin-hwif.md` による）。FITOM_X 以外のアプリケーションから呼ぶ場合には、
     この未確定が残る
   見送った案：
   - FitomEmuIF 固有の拡張にする（`FmEmuIf_*` を別ヘッダに宣言）。理由：利用者が
     `HWPlugin_*` を選んだ。FITOM_X がエミュレーターと実機を区別しない方針からも外れる
   - プロファイルのキーで部位ゲインを設定する。理由：依頼はアプリケーション向けの
     インターフェース。キーは外に出る値なので、足すときに決める（足すだけなら互換は
     壊れない）。その後、FITOM_X（`4ab7a56`）が自分のプロファイルの `part_gains` に保存し、
     デバイスを開いた直後に適用するようになった（書式は FITOM_X の
     `docs/manuals/profile-reference.md`「チップの出力ごとの音量(`part_gains`)」）。FitomEmuIF 側のキーが要る場面は、今のところ無い
   - 部位の一覧を JSON 文字列で返す（`HWPlugin_Enumerate` と同じ流儀）。理由：FmEngineApi と
     同じ数え上げの形なら、FITOM_X 側に解析が要らない
   やり直しの値段：関数名を変える場合、FitomEmuIF 内は宣言・定義・README・テストの置換で
   済む。FITOM_X が呼び始めた後は FITOM_X にも及ぶ。
9. **FmEngineApi の任意の組は、組の先頭のシンボルの有無だけで判定する**（2026年10月3日）。
   部位ごとのゲインは `FmEngine_GetPartCount`、外部メモリは `FmEngine_GetMemoryCount`。
   判定の方法は仕様書が定めている。番号で指定する版の DLL も `FmEngine_SetPartGain` /
   `FmEngine_SetMemory` を同じ名前でエクスポートしているので、その有無で判定すると、名前の
   ポインタを番号として渡すことになる。
   こちらで決めたこと：
   - 組の先頭があるのに残りが欠けている DLL は、`HWPlugin_Init` を失敗にする
     （`HW_ERR_OPEN_FAILED`。必須シンボルの欠落と同じ扱い）。外部メモリの組については、
     FMEngineTest の `src/main.cpp` も同じ扱いにしている（FMEngineTest の CHANGELOG による）。
     見送った案：欠けた組を無いものとして続行する。理由：仕様に合わない DLL が黙って通る
   - 外部メモリは、エンジンに `FmEngine_GetMemoryCount` / `GetMemoryName` で列挙させ、
     `pcm_mappings_for_chip()` の表（チップ、メモリの名前 → カタログキー）で引く。エンジンが
     報告しなかったメモリには `FmEngine_SetMemory` を呼ばない
   - OPNA / Y8950 の `ADPCM_B_ROMMODE` には何も渡さない。FITOM_X のカタログのスキーマ
     （`config_schema/pcm_image_catalog.schema.json`）のキーは 6 個で、対応するものが無い
     （スキーマを読んで確認済み）
   利用者と決めたこと：SSGS / SSGS2 の行は、名前が決まるまで表から外す。2026年10月23日に
   EPSGemuEngine（`72f70d6`）で `PCM` に決まったので、`SSGS_ADPCM` を渡す行を戻した。
   SSGS3 の `PCM` は AMM 部のメモリで、`SSGS_ADPCM`（YMZ705 / YMZ732 の ADPCM 部）とは別物
   なので表に載せない。
   前提：FITOM_X のカタログのキーが今の 6 個であること（FITOM_X `bdea8cf` でも 6 個。
   スキーマを読んで確認）。キーが増えたら表に行を足す。
10. **OPL2EX の `ADPCM_B` には、Y8950 と同じカタログキー `ADPCM-B` を渡す**（2026年10月23日、
    利用者と決めた）。
    根拠：FITOM_X（`bdea8cf`）は OPL2EX の ADPCM-B を、レジスタ配置もバウンダリ整列も Y8950 と
    同一として扱い（`core/include/fitom/FITOMdefine.h` の `DEVICE_ADPCMB_OPL2EX`、
    `config_schema/profile.schema.json`）、音色の種別も Y8950 と共用している。FITOM_X の
    カタログのスキーマは `additionalProperties: false` で、OPL2EX 用のキーが無い。
    前提：OPL2EX と Y8950 の ADPCM メモリの配置が同じであること。
    見送った案：専用キー（`OPL2EX_ADPCM-B` など）を作る。理由：FITOM_X のスキーマにキーを
    足さないとカタログがスキーマ違反になり、FITOM_X 側の定義（Y8950 と同一）とも合わない。
    やり直しの値段：専用キーに変えるなら、`pcm_mappings_for_chip()` の文字列 1 か所、README の
    表 1 行、`test/engine_relay_test.cpp` のイメージと判定。FITOM_X のスキーマと運用カタログ
    （FITOM_staging）にもキーが要る。
11. **マスターボリュームは振幅 `(v/127)²` で、pan と掛け合わせて `FmEngine_SetGain` に渡す**
    （2026年10月25日）。
    利用者と決めたこと：換算は GM2 のカーブ `40·log10(v/127)` dB を振幅にした `(v/127)²`。
    0 は振幅 0（無音）。FITOM が CC#7 に使う表（FITOM_X の `core/src/VolumeUtils.cpp` の
    `kGM2dB`）とは v=1〜127 で同じカーブになる（表の値と式を突き合わせた。表の v=0 は -96 dB）。
    見送った案：
    - `kGM2dB` と v=0 まで一致させる（-96 dB、振幅 約1.6e-5）。理由：0 で音がわずかに残る
    - 線形 `v/127`。理由：FITOM のチャンネルボリュームと効き方が揃わない
    こちらで決めたこと：
    - 掛ける場所は `FmEngine_SetGain`（チップごとのゲイン）。`FmEngine_Generate` はエンジン内の
      全チップを混ぜて返すので、FitomEmuIF のミキサーではデバイス（チップ）ごとに掛けられない。
      pan もこのゲインで実現しているので、`apply_chip_gain()` が pan とマスターボリュームの両方から
      L/R を計算し、Init・Open（pan の上書き）・Close・SetMasterVolume のすべてがここを通る
    - 値はハンドル（`HWDeviceOpaque::master_volume`）に持つ。Close で 127 に戻す。pan の上書きか
      127 以外の値を持つハンドルの Close だけが `FmEngine_SetGain` を書き直す
    - `FmEngine_SetGain` が失敗したら `HW_ERR_IO` を返し、値を控えない
    前提：FitomEmuIF の中で `FmEngine_SetGain` を書くのが pan とマスターボリュームだけであること。
    やり直しの値段：換算を変えるなら `master_volume_gain()` 1 か所、README の 1 行、テストの
    `master_gain()`（期待値はそこから計算している）。
12. **オフライン描画は、初期化を `HWPlugin_Init` と共通にし、オーディオコールバックと同じ
    ミックスを使う**（2026年10月25日）。
    こちらで決めたこと（利用者と明示的には決めていない）：
    - `PluginRegistry::init()` に任意の `offline_sample_rate` を足し、あるときは
      `start_audio()` を呼ばない。プロファイルの `sample_rate` は引数で置き換え、エンジンの
      `FmEngine_Create` に渡す
    - ミックスは `PluginRegistry::mix()` にまとめ、`audio_callback` はその後でクリップする。
      `HWPlugin_Render` はクリップしない
    - 一時バッファより長い要求は分割して `FmEngine_Generate` を呼ぶ。オフラインの一時バッファは
      4096 フレーム固定（`kOfflineChunkFrames`）で、プロファイルの `buffer_frames` は使わない。
      `audio_callback` も同じ分割を通るようになった。以前は一時バッファより長い要求の
      エンジンを飛ばしていた。RtAudio が `openStream` で確定したフレーム数より長い要求で
      コールバックを呼ぶことがあるかは確かめていない。あるなら、以前はその回だけ無音に
      なっていたエンジンが鳴るようになる
    - `sample_rate` の検査は 0 だけ（TODO 7）。エンジンが扱えるレートを問い合わせる API が無い
    - `FmEngine_Generate` が失敗したエンジンは足さずに他のエンジンを足し、`HW_ERR_IO` を返す
    - `HWPlugin_Init` で初期化した後の `HWPlugin_Render` は `HW_ERR_INVALID_ARG`。
      `HWPlugin_Init` と `HWPlugin_InitOffline` は、どちらか一方を一度だけ受け付ける
    - 書き込みは溜めずに `FmEngine_Write` へ渡すので、FitomEmuIF の側では遅らせない。何フレーム目
      から音に効くかはエンジン次第（YMEngine はキー衝突時に書き込みを遅らせる。作業記録
      「YMEngine 更新（b575a78〜26baa63）の影響確認」）
    前提：FmEngineApi の `FmEngine_Generate` が、呼び出しのフレーム数を問わず続きを生成すること。

## 作業記録

### 2026年10月25日 マスターボリュームとオフライン描画を実装

依頼：FITOM_X のセッションからの依頼文（FITOM_X の `docs/plans/` に置かれ、終われば FITOM_X 側で
消す）。FITOM_X が HW プラグインの契約に任意の `HWPlugin_SetMasterVolume`（`d214ff9`）と、
組の `HWPlugin_InitOffline` / `HWPlugin_Render`（`faaf183`。SMF を WAV へ変換する `fitom_render`
が使う）を足したので、FitomEmuIF に実装し、`IHWPlugin.h` の写しを同期する。要件は FITOM_X
`faaf183` の `docs/interfaces/plugin-hwif.md`「マスターボリューム」「オフライン描画」節。

作業前の状態：FitomEmuIF は origin/main と一致し、作業ツリーには git 管理外の `buildvc2026/` が
あった（触れていない）。FITOM_X は読んだだけで編集していない。

変更：

- `src/fitom/IHWPlugin.h`：FITOM_X `faaf183` の正本に差し替えた（TODO 1）
- `src/FmEmuIfImpl.cpp`：`HWPlugin_SetMasterVolume`（設計判断 11）、`HWPlugin_InitOffline` /
  `HWPlugin_Render`、オフラインでの `HWPlugin_GetLatencySamples` = 0（設計判断 12）
- `test/stub_engine.cpp`：`FmEngine_Generate` が、port 0 のレジスタ 0x00 に書いた値とチップの
  ゲインから決まる直流を出すようにした。音声デバイスを開くテストが音を出さないよう、
  音を出すレジスタは 1 つに限った。`FmEngine_Create` / `FmEngine_SetGain` を記録し、
  オーディオスレッドとの並行のために mutex を入れた
- `test/engine_relay_test.cpp`：`current` にマスターボリュームの判定と、通常の初期化での
  `HWPlugin_Render` の失敗を足した（チップに pan=2 の OPM を 1 つ足した）。`offline` モードを足した
- `test/smoke_test.cpp`：初期化前の `Render`、`InitOffline` の引数誤り、`SetMasterVolume(nullptr)`
- `CMakeLists.txt`：ctest に `relay_offline` を足した（6 件）
- README.md：「マスターボリューム」「オフライン描画」の節、ライフサイクル、レイテンシ、テスト

**確認済み**（VS2026、MSVC 19.51、x64、Release。`build-vs2026`）：

- `ctest` の 6 件が通る。判定数は `relay_current` 72、`relay_legacy` 11、`relay_offline` 32、
  `smoke` 14
- 実装を 1 か所ずつ壊して再ビルドすると、それぞれ `relay_current` か `relay_offline` が落ちる：
  Render でクリップする、Close でマスターボリュームを戻さない、マスターボリュームで pan を
  無視する、オフラインでプロファイルのレートを使う、分割せず長い要求のエンジンを飛ばす、
  オフラインでもレイテンシに `buffer_frames` を返す、128 以上を受け付ける、換算を線形にする
  （8 通り）。戻した後は通る
- 実エンジン：`../YMEngine/build/bin/Release/YMFMEngine.dll`（2026年10月23日のビルド。
  `FmEngine_GetPartCount` / `GetMemoryCount` を持つ。どのコミットからビルドされたかは
  確かめていない）を Python の ctypes から使い、`HWPlugin_InitOffline`（48000 Hz）→ OPM を開く →
  1 音鳴らす → `HWPlugin_Render`。描画の最初の 1ms（48 フレーム）から RMS 0.186 の音が出る。
  レイテンシは 0。マスターボリューム 127→64 で RMS の比は 0.2552（`(64/127)²` は 0.2540）、
  0 でピーク 0、Close の後は元の RMS に戻る。ストリームを開いていない状態でも
  `HWPlugin_Shutdown` は戻る。音は聴いていない（数値だけ）

**未検証**：

- FITOM_X（`fitom_render` と、演奏中の `HWPlugin_SetMasterVolume`）と組み合わせた動作
- 再生中のマスターボリュームの変更を聴いて確かめること。`relay_current` はストリームを開いた
  まま `SetMasterVolume` を呼んでいるが、検証用エンジンの出力は無音
- 部位ごとのゲインがオフライン描画の出力に効くこと。中継は通常の初期化と同じ経路で、
  検証用エンジンの出力は部位ゲインを反映しない
- Linux / macOS でのビルドと実行

### 2026年10月23日 FITOM_X の文書再編に合わせて参照を直す

依頼：FITOM_X 側で `docs/` が読者別のディレクトリ（`manuals/`・`design/`・`interfaces/`・
`proposals/`）に再編されたので、このリポジトリにある FITOM_X の文書への参照を直す。
作業は FITOM_X 側のセッションが、ユーザーの許可を得て行った。

作業前の状態：FitomEmuIF は origin/main と一致し、作業ツリーはクリーンだった。

- 関連リポジトリの節と設計判断 8 の `docs/plugin-hwif.md` を `docs/interfaces/plugin-hwif.md` に、
  設計判断 8 の `docs/config-design.md`「部位ごとのゲイン」を
  `docs/manuals/profile-reference.md`「チップの出力ごとの音量(`part_gains`)」に直した。
  見出しが FITOM_X 側に実在することは grep で確かめた。
- 作業記録の節にある参照は記録なので直していない。ファイル名だけで書いた `plugin-hwif.md` は
  ファイル名が変わっていないのでそのままにした。
- コードとビルドには触れていない。

### 2026年10月23日 OPL2EX と SSGS / SSGS2 の外部メモリにイメージを渡す

依頼：`pcm_mappings_for_chip()` に OPL2EX の ADPCM-B の行が無く、イメージがエンジンへ
渡らない。あわせて、SSGS の外部メモリの名前が EPSGemuEngine で `PCM` に決まった。

作業前の状態：FitomEmuIF は origin/main と一致し、作業ツリーはクリーンだった。

参照した他リポジトリ（いずれも origin/main。読んだだけで編集していない）：

- DSAemuEngine `46c2fbe` の README：OPL2EX のメモリは `ADPCM_B`（RAM 256KB）、Y8950 は
  `ADPCM_B` / `ADPCM_B_ROMMODE`。Y8960 の SSG 部はこのコミットで `SSGS` から `SSGEX` に
  改名され、外部メモリを持たない
- EPSGemuEngine `72f70d6` の `src/EPSGemuEngine.cpp`：`kMemoryName` は `"PCM"` で、外部メモリを
  持つ全チップがこの名前を返す。チップ名の表は `SSGS` / `SSGS2` / `SSGS3` / `AMMS-A` / `PCMD8`
- FITOM_X `bdea8cf`：カタログのスキーマ、OPL2EX の ADPCM-B の扱い（設計判断 10）

変更：

- `src/FmEmuIfImpl.cpp`：表に OPL2EX（`ADPCM_B` → `ADPCM-B`）と SSGS / SSGS2（`PCM` →
  `SSGS_ADPCM`）を足した（設計判断 9・10）
- `test/stub_engine.cpp`：チップ `OPL2EX` / `SSGS2` / `SSGS3` を足した
- `test/engine_relay_test.cpp`：上の 3 チップをプロファイルに足し、`FmEngine_SetMemory` の
  判定を 8 件から 11 件にした。SSGS3 には渡らないことも見る
- README.md の表、`pcm_images.catalog.example.json` の注記

**確認済み**（VS2026、MSVC 19.51、x64、Release。`build-vs2026`）：

- テストだけを直して旧実装のまま走らせると、`relay_current` が新しい 4 判定（SSGS、OPL2EX、
  SSGS2 への受け渡しと件数）で落ちる。ほかの 4 件は通る
- 表を直すと `ctest` の 5 件が通る（`relay_current` は 53 判定）

**未検証**：実エンジン（DSAemuEngine / EPSGemuEngine）との組み合わせ。イメージが音に反映
されることは確かめていない（TODO 4 の「新しい形を実装した実エンジンでは走らせていない」）。

### 2026年10月4日 FmEngineApi.h の写しを更新（FmEngine_GetNativeRate の廃止）

FmEngineApi から `FmEngine_GetNativeRate` が外れた（FMEngineTest `831947d`。必須シンボルは
12 個から 11 個になった）。FMEngineTest の CHANGELOG が FitomEmuIF に求めている対応は、
ヘッダの写しとテスト用の検証用エンジンを直すこと。

- `src/fitom/FmEngineApi.h`：`831947d` の `include/FmEngineApi.h` の写しに差し替えた
  （改行の違いを除いて diff で一致。確認済み）。宣言が 1 つ減っただけ
- `test/stub_engine.cpp`：`FmEngine_GetNativeRate` の定義を外した

FitomEmuIF 本体は、以前からこの関数を読んでいない（設計判断 7）。

**確認済み**（VS2026、MSVC 19.51、x64、Release）：

- 本体のソースに `GetNativeRate` の参照が無い（検索）
- 再ビルドして `ctest` の 5 件が通る。検証用エンジンの 4 つの変種は、どれも
  `FmEngine_GetNativeRate` をエクスポートしていない（dumpbin）
- この関数をまだエクスポートしている実エンジン（10月3日の作業記録と同じ、改訂前の
  `YMFMEngine.dll`）でも、`legacy` の 7 判定が通る

### 2026年10月4日 FITOM_X の正本ヘッダとの突き合わせ

FITOM_X `4ab7a56` で、部位ごとのゲインが正本の `IHWPlugin.h` と FITOM_X 本体（`HWPort`、
プロファイルの `part_gains`、GUI）に入った。こちらの写しと実装に合うかを確かめた。

**確認済み**：

- 宣言：コメントと空行を除いて diff すると、正本と写しは全関数で一致する
- 写しを正本と同一にした（改行の違いを除いて diff で一致）。変わったのは、コメントと
  `HWPlugin_Shutdown` の宣言の位置だけ。差し替えた後に再ビルドし、`ctest` の 5 件が通る
  （VS2026、MSVC 19.51、x64、Release）

読んで突き合わせたこと（FITOM_X と実物の FitomEmuIF を組み合わせて動かしてはいない。
TODO 4）：

- FITOM_X の `docs/plugin-hwif.md`「部位ごとのゲイン」の実装要件は、FitomEmuIF の実装
  （設計判断 8）と一致する：名前の文字列は `HWPlugin_Close` まで有効、設定したゲインは
  `HWPlugin_Close` で既定値に戻す、既定値は `HWPlugin_Open` の直後に読める、戻り値の条件
- FITOM_X の呼び方（`HWPort` のコンストラクタで列挙と既定値の読み取り → `part_gains` の
  適用 → 演奏中は GUI の操作から設定）は、`relay_current` が通している手順（Open → 列挙 →
  既定値 → 設定 → Close → 開き直し）に含まれる
- FITOM_X は `HWPlugin_GetPartCount` の有無で組を判定し、残りが欠けるプラグインをロード失敗に
  する。FitomEmuIF は 4 関数ともエクスポートしている（10月3日に dumpbin で確認）
- スレッド：設計判断 8 の前提に追記した

この文書が参照する他リポジトリのコミットのハッシュは、各リポジトリの現行の履歴のものに
直した。

### 2026年10月3日 FmEngineApi の改訂に追従し、部位ゲインのインターフェースを追加

依頼：アプリケーションから部位ごとのゲインを調整できるインターフェースを提供する。
FmEngineApi の改訂（FMEngineTest `0c22d67`）で、部位と外部メモリを名前の文字列で指定し、
エンジンに列挙させる形になった。`FmPart` / `FmEngine_GetPartMask` / `FmMemoryType` /
`FmEngine_GetMemorySize` は無くなり、外部メモリの関数は必須から任意の組に変わった。
ヘッダの正本は YMEngine から FMEngineTest に移った。

同じ日に、番号で指定する版（YMEngine `89cbae1`）を前提に一度着手した。FmEngineApi が
さらに変わる予定だったので、利用者の判断で中止し、その時点の変更は戻した。

変更：

- `src/fitom/FmEngineApi.h`：FMEngineTest `0c22d67` の `include/FmEngineApi.h` の写しに
  差し替えた（改行の違いを除いて一致することを diff で確認済み）
- `src/fitom/IHWPlugin.h`：部位ゲインの任意関数 4 本を足した（設計判断 8）
- `src/FmEmuIfImpl.cpp`：`FmEngine_SetMemory` を必須シンボルから外し、任意の組を
  組の先頭で判定して読む（設計判断 9）。部位の列挙と 4 関数の中継、`HWPlugin_Close` での
  復帰。外部メモリは名前で渡す。SSGS / SSGS2 の行は外した
- `test/stub_engine.cpp`・`test/engine_relay_test.cpp`：検証用エンジンと中継のテストを足した。
  CMake は `BUILD_FITOMEMUIF_TEST=ON` で ctest に 5 件を登録する（TODO 6 のパスも直した）
- README.md、`fmemuif_profile.example.json`、`pcm_images.catalog.example.json`

**確認済み**（VS2026、MSVC 19.51、x64、Release、RtAudio は WASAPI。`ctest` で 5 件通過。
判定数はテストの出力による）：

- エクスポート（dumpbin）：`FitomEmuIF.dll` に 4 関数がある。検証用エンジンの 4 つの変種は、
  意図したシンボルだけが違う（番号指定の版は `GetPartCount` / `GetPartName` /
  `GetMemoryCount` / `GetMemoryName` を持たず `GetPartMask` / `GetMemorySize` を持つ。
  欠落の 2 変種は `GetPartName` / `GetMemoryName` だけを欠く）
- `relay_current`（現行の形の検証用エンジン、50 判定）：
  - 外部メモリ：OPNA の `RHYTHM` に `OPNA_RHYTHM`、`ADPCM_B` に `ADPCM-B`、OPNB の
    `ADPCM_A` に `ADPCM-A`、`ADPCM_B` に `OPNB_ADPCM-B`、OPL4 の `PCM` に `OPL4AWM`、Y8950 の
    `ADPCM_B` に `ADPCM-B` のイメージが渡る（イメージごとに大きさと先頭・末尾のバイトを
    変えて見分けた）。`FmEngine_SetMemory` の呼び出しは 8 件ちょうどで、`ADPCM_B_ROMMODE` と
    SSGS のメモリには渡らない。エンジンがメモリを仕様書の表と逆順に返しても渡る（OPNB）
  - 部位の列挙：OPNA は `FM` / `SSG`、OPL3 は `AB` / `CD`、OPL4 は `DO0` / `DO1` / `DO2`、
    OPM は 0 個。既定値はエンジンの値が読める（OPL3 の `CD` は 0）
  - 設定と読み戻し：設定した値がエンジンに届き、エンジンから読み戻せる。同じチップの
    別の部位と、同種の別チップ（index 違い）の値は変わらない
  - 引数の誤り：別チップの部位の名前、大文字小文字の違い、`nullptr`（handle / part /
    出力先）は `HW_ERR_INVALID_ARG`。値は変わらない
  - Close：設定したハンドルを閉じると、エンジンに既定値が書き戻される（OPL3 の `CD` は
    1.0 ではなく 0 に戻る）。開き直すと既定値が読める。設定していないハンドルを閉じても、
    部位ゲインは書かない
- `relay_legacy`（番号指定の版の検証用エンジン、11 判定）：`HWPlugin_Init` が通り、部位は
  0 個、`HWPlugin_SetPartGain` / `GetPartGain` は `HW_ERR_INVALID_ARG`。番号で受け取る版の
  関数（`SetPartGain` / `GetPartGain` / `GetPartMask` / `SetMemory` / `GetMemorySize`）は
  1 回も呼ばれない（カタログに OPNA のイメージを置いた状態で、呼び出しの記録が 0 件）。
  対照：同じ記録に `AddChip` は残っており、テストから `FmEngine_SetMemory` を番号で直接
  呼ぶと記録が 1 件になる
- 同じ `legacy` を、改訂前の実エンジンで実行（7 判定）：`../YMEngine` の手元の
  `YMFMEngine.dll`（2026年10月2日のビルド。`GetPartMask` があり `GetPartCount` が無いことを
  dumpbin で確認。どのコミットからビルドされたかは確かめていない）で、`HWPlugin_Init`
  （オーディオストリームの起動を含む）→ `Open`（OPNA）→ 部位 0 個 → `Write` → `Close` →
  `Shutdown` が通る。実エンジンは記録を持たないので、関数が呼ばれていないことは
  この実行では見ていない
- `relay_no_part_name` / `relay_no_memory_name`：組の先頭があり残りを欠く DLL では、
  `HWPlugin_Init` が `HW_ERR_OPEN_FAILED` を返す。対照：欠けていない検証用エンジンを同じ
  モードに渡すと `HWPlugin_Init` が通り、テストは落ちる
- `smoke`：12 件（TODO 6）

**未検証**：

- 新しい形を実装した実エンジンでの動作（TODO 4）。検証用エンジンは音を出さないので、
  部位ゲインが出力の音量に効くこと、渡した ROM が音に反映されることは確かめていない
- FITOM_X からの呼び出し（FITOM_X 側の実装が無い）
- 再生中に別スレッドから `HWPlugin_SetPartGain` を呼ぶこと。テストは 1 スレッドから呼ぶ
- Linux / macOS でのビルドと実行。テストの `dlopen` の分岐と、検証用エンジンのファイル名
  （`lib` 接頭辞）の扱いは、コードを書いただけで動かしていない

### 2026年10月1日 YMEngine 更新（b575a78〜26baa63）の影響確認

対象は FitomEmuIF の前回コミット（2026年8月16日）より後の YMEngine の変更。確認時点の
`src/fitom/FmEngineApi.h` は YMEngine の d65c309 と同一だった（diff で確認済み）。

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
- **b575a78（キー衝突時の書き込み保留）**：同じチャンネルのキー状態が衝突した書き込みは、
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

### 2026年10月1日 RtAudio ea2c88c（#489）の取り込みを見送り

c0a533d の後に、上流で PR #489 がマージされた（ea2c88c、6.0.1-85、タグなし）。`RtAudio` の
コンストラクタに渡したエラーコールバックを、API オブジェクトを作った直後に付けるように
なった。これまでは、コンストラクタの中で別の API に切り替えるときのデバイス調査で出た
警告が、コールバックを渡していても標準エラーに出ていた。protected の `openRtApi` に
引数（既定値付き）が1つ増えた。

FitomEmuIF は `RtAudio(api)` をエラーコールバックなしで作り、`openRtApi` を使わず
`RtAudio` を継承もしていないので、影響は無い（差分とコードを読んで判断。走らせては
いない）。指定した API が組み込まれていないという警告そのものは、c0a533d でも
コールバックに届く。

判断：今は取り込まず c0a533d のままにする。TODO 8 の対策でエラーコールバックを渡すときに
一緒に取り込む。
前提：FitomEmuIF が RtAudio にエラーコールバックを渡さないこと。
