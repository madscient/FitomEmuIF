# CLAUDE.md

## コミュニケーション

- ユーザーへの報告・応答は日本語で行うこと。

## 文書の役割

| 文書 | 読者 | 書くこと |
|---|---|---|
| `CLAUDE.md` | AI | リポジトリのルールだけ |
| `doc/plan.md` | AI | プロジェクト概要、関連リポジトリ、計画、進捗、未解決の TODO・既知の課題、設計判断の経緯、作業記録 |
| `README.md` | 利用者・開発者 | 現在の技術仕様。経緯は書かない |

計画・進捗・仕様・経緯をこのファイルに書かない。書きたくなったら `doc/plan.md`
（仕様の現状なら README.md）へ書く。作業を始める前に `doc/plan.md` を読む。

## 他リポジトリの編集

**`source/repos` 配下の他リポジトリ（関連リポジトリは `doc/plan.md` を参照）のファイルは、
たとえ原因や修正箇所がそちら側にあると判明しても、ユーザーの明示的な許可なく編集しない。**
（`../YMEngine/extern/ymfm` のような vendor submodule も同様）。調査・原因特定は自由に
行ってよいが、実際にコードを書き換える前に必ずユーザーに確認を取る。

## 変更してはいけない名前

取り違えやすいもの。**特に注意。**

| 項目 | 扱い |
|---|---|
| `"type": "FMHWIF"` | FITOM_X 側プロトコル識別子。絶対に変更しない（`FMEMUIF` 等へ変えるのは誤り） |
| `src/fitom/IHWPlugin.h` | FITOM_X の `plugin_sdk/include/fitom/IHWPlugin.h` の独立コピー。内容は同期させる必要があるが、ファイル名自体は変更しない |
| 環境変数 `FMEMUIF_PROFILE` / ファイル名 `fmemuif_profile.json` | プロジェクト名 `FitomEmuIF` に合わせた名称。`FMHWIF`/`fmhwif` 系の旧名には戻さない |
| ターゲット名 | `FitomEmuIF`（CMake ターゲット名・DLL 名） |
