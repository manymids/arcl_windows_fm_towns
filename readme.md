# ARCL Windows FM TOWNS — Windows 向け FM TOWNS エミュレータ / MCP サーバ

[English version](readme_en.md)

このプロジェクトは、[TOWNSEMU（津軽）](https://github.com/captainys/TOWNSEMU) を基にした FM TOWNS MX 向けの Windows フロントエンドです。小型 GUI での通常プレイに加え、[Model Context Protocol (MCP)](https://modelcontextprotocol.io/) の stdio サーバとして動作します。MCP クライアントは画面・音声の取得、入力、メディア操作、デバッグ、およびセーブ状態の管理を行えます。

このリポジトリには ROM、Towns OS、ゲーム、CD/FD/HDD イメージを含めません。利用・配布の権利を持つファイルだけを各自で用意してください。

## 対象環境

- Windows 10 / 11、64 ビット
- FM TOWNS II MX と UNZ 互換の ROM セット
- ソースからビルドする場合: Visual Studio の MSVC x64 開発環境、CMake 3.20 以降

このフロントエンドと手順は Windows / MSVC x64 を対象にしています。

## 事前に用意するファイル

### ROM

FM TOWNS の ROM は富士通など権利者の著作物です。実機から抽出したもの、または利用条件を満たす互換 ROM を用意し、次の配置にします。

```text
TOWNSEMU/
  roms/
    ROM_MX/
      FMT_DIC.ROM
      FMT_DOS.ROM
      FMT_F20.ROM
      FMT_FNT.ROM
      FMT_SYS.ROM
```

`TOWNSEMU\roms\` は Git の管理対象外です。新しい clone では必要に応じて作成してください。

```powershell
New-Item -ItemType Directory -Force TOWNSEMU\roms\ROM_MX
```

ROM、OS、ゲーム、およびディスクイメージをコミットまたは再配布しないでください。

### CD / FD / HDD イメージ

起動したいメディアはホスト上の任意の場所に置けます。TOWNSEMU は CD イメージとして `.ISO`、`.CUE`、`.MDS` を扱います。音楽 CD トラックを含むゲームは `.MDS` / `.MDF` を推奨します。`.CUE` の `PREGAP` / `POSTGAP` の解釈には作成ツール間で曖昧さがあるためです。

以下の例では、CD イメージを `C:\FM_TOWNS\game.cue` とします。空白を含むパスは、個別の引数として引用符で囲んでください。

## ビルド

1. Visual Studio の C++ によるデスクトップ開発ワークロードと CMake をインストールします。
2. リポジトリのルートで、x64 Native Tools Command Prompt を開くか、MSVC x64 環境を有効にします。
3. 次を実行します。

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S TOWNSEMU\src -B arcl_windows_fmtowns\build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build arcl_windows_fmtowns\build --target arcl_windows_fmtowns
```

実行ファイルは `arcl_windows_fmtowns\build\bin\arcl_windows_fmtowns.exe` に生成されます。CMake の構成を作り直す場合は `arcl_windows_fmtowns\build\` を削除してから、configure コマンドを再実行してください。

## 通常モードで起動する

次のコマンドは小型 GUI を開き、指定した ROM ディレクトリで FM TOWNS を起動します。

```powershell
arcl_windows_fmtowns\build\bin\arcl_windows_fmtowns.exe TOWNSEMU\roms\ROM_MX
```

CD ゲームの起動例です。

```powershell
arcl_windows_fmtowns\build\bin\arcl_windows_fmtowns.exe `
  TOWNSEMU\roms\ROM_MX `
  -CD "C:\FM_TOWNS\game.cue" `
  -GAMEPORT0 KEY
```

GUI スレッドは描画と入力だけを最大約 60 Hz で扱い、VM 実行と音声生成は別スレッドで連続実行します。画面の更新周期は音声出力の周期ではありません。`-FREQ 40` は VM の CPU 周波数指定であり、表示を 40 Hz にする指定ではありません。

| PC キー | 操作 |
|---|---|
| 通常の TOWNS キー | ゲストへそのまま送る（英数字、記号、Space、Tab、Enter、Backspace、Shift、Ctrl、Alt、カーソル、F1–F12 など） |
| Arrow keys | game pad 0 の方向キー（`-GAMEPORT0 KEY` 指定時） |
| `Z` / `X` | game pad 0 の A / B |
| `A` / `S` | game pad 0 の Run / Pause |
| `Q` | game pad 0 の Zoom |
| `Esc` | ARCL ウィンドウを閉じる（ゲストへは送らない） |

表示・音声・性能計測の詳細と After Burner III の実行例は [ARCL GUI 操作](doc/reference/arcl-gui-controls.md) を参照してください。`--arcl-profile` を付けると、終了時に区間別の累積時間を標準出力へ出力します。

既定では実行ファイルの隣に `arcl-output\` と `arcl-state\` を作成します。`--arcl-output-dir DIR` と `--arcl-state-dir DIR` で変更できます。明示した相対パスは、コマンドを実行したカレントディレクトリから解決されます。

## MCP モードで起動する

MCP モードでは、標準入力・標準出力で改行区切りの JSON-RPC 2.0 を処理します。標準出力は MCP メッセージ専用のため、端末から直接対話するのではなく MCP クライアントの stdio サーバとして起動してください。

```powershell
arcl_windows_fmtowns\build\bin\arcl_windows_fmtowns.exe `
  --mcp --no-window --mcp-layers l0,l1 `
  --arcl-allow-root C:\FM_TOWNS `
  TOWNSEMU\roms\ROM_MX
```

`--no-window` を外すと GUI も表示され、利用者はエージェントの操作を観察できます。MCP モードではエミュレータは停止状態で始まり、クライアントが `arcl_run` または `arcl_resume` を呼ぶまで実行しません。診断出力は stderr に出力されます。

### MCP クライアントの設定

[arcl_windows_fmtowns/config](arcl_windows_fmtowns/config) には、パスに依存しない Codex および Claude Code 向けの設定テンプレートがあります。ローカル設定へコピーし、実行ファイル、メディアの許可ルート、および ROM のパスを置き換えてください。

| テンプレート | 用途 |
|---|---|
| [codex-config.toml.template](arcl_windows_fmtowns/config/codex-config.toml.template) | Codex の `config.toml` に追加するサーバ設定 |
| [claude-code-mcp.json.template](arcl_windows_fmtowns/config/claude-code-mcp.json.template) | Claude Code の `.mcp.json` に追加するサーバ設定 |

ローカルの `.mcp.json` と `.codex/config.toml`、実行ファイル、ROM、メディアは意図的に Git 管理対象外です。公開リポジトリへ追加しないでください。

### 機能レイヤー

`--mcp-layers` は `control`、`l0` から `l4` のコンマ区切りの部分集合を受け取ります。既定値は `control,l0,l1` です。

| レイヤー | 主な機能 |
|---|---|
| Control | 実行、停止、リセット、セーブ、ロード |
| L0 | 画面取得、キーボード、マウス、ジョイパッド、音声取得 |
| L1 | コンソール、メディア操作、ホストディレクトリ |
| L2 | レジスタ、メモリ、ブレークポイント、逆アセンブル |
| L3 | ビデオ、VRAM、パレット、スプライト、DMA、IRQ、音源デバイス |
| L4 | 名前付きスナップショット、巻き戻し、速度計測 |

接続後は `tools/list` が返す JSON Schema を、各ツールの正確な入出力契約として参照してください。L2 以降にはエミュレート状態を変更できる操作があります。信頼できる MCP クライアントだけを接続してください。

## 既知の制約

- 対象マシンは FM TOWNS II MX です。TOWNSEMU 本体の対応状況と制約は [TOWNSEMU/readme.md](TOWNSEMU/readme.md) も参照してください。
- CD オーディオを含むメディアでは `.CUE` の `PREGAP` / `POSTGAP` 解釈に依存するため、保存用および再現性が必要な用途では `.MDS` / `.MDF` を推奨します。
- MCP のメディア操作・共有ディレクトリは、`--arcl-allow-root` で許可したディレクトリ配下だけに制限されます。
- 巻き戻し状態は毎フレーム保存しません。明示的なスナップショットと巻き戻し操作のために必要なときだけ保存します。

## リポジトリ構成

| パス | 内容 |
|---|---|
| `TOWNSEMU/` | TOWNSEMU コアと必要なコア変更 |
| `arcl_windows_fmtowns/` | Windows フロントエンド、MCP サーバ、テスト、設定テンプレート |
| `doc/reference/` | ARCL 仕様、GUI 操作、検証用リファレンス |
| `doc/development/` | 設計、フェーズ計画、開発・レビュー記録 |
| `readme_en.md` | この README の英語版 |

## ライセンス

ARCL 追加部分は BSD 3-Clause License です。TOWNSEMU 本体は CaptainYS による BSD 3-Clause License であり、原文は [TOWNSEMU/LICENSE](TOWNSEMU/LICENSE) に保持しています。詳細は [LICENSE](LICENSE) を参照してください。

ROM、OS、ゲーム、およびディスクイメージは各権利者に帰属します。本リポジトリのライセンス対象ではなく、同梱・再配布してはいけません。
