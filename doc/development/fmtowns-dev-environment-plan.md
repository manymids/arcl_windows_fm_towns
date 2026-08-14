# FM TOWNS 開発環境 構築計画

作成日: 2026-08-14
関連: [fmtowns-dev-environment-research.md](fmtowns-dev-environment-research.md)

## ゴール

FreeTOWNSOS（Tsugaru OS）を、本リポジトリの TOWNSEMU / ARCL MCP 資産の上で動かし、その上で自作プログラムをビルド・実行・検証できる状態にする。

## 基本方針: 自作プログラムのビルドに限定

`release/HDIMG.h0` 等の**ビルド済み FreeTOWNSOS をそのまま実行環境として使い**、その上で動かす自作プログラムだけを TOWNS-gcc または NASM でビルドする。FreeTOWNSOS 自体のビルドシステム（`tgbios/makedisk.py` 等）には触れない。

## メインルート

### Step A — 前提確認（決定済み・2026-08-14）

- **取り込み方法**: git submodule として `FreeTOWNSOS/`（仮）を本リポジトリに追加する。TOWNSEMU と同じ扱い。
- **着手タイミング**: 今回はレポートと計画のレビューまで。実装は次回以降、ユーザーの指示を受けてから開始する。
- 残課題: `TOWNSEMU/roms/ROM_MX/*.ROM` が TOWNSROM 由来かどうかの確認は必要になった時点で行う。

### Step B — release 済みイメージをそのまま起動（ビルド不要）

FreeTOWNSOS は GitHub Releases は使っていないが、リポジトリの `release/` フォルダに**ビルド済みの起動イメージがそのままコミットされている**（`CDIMG.ISO`, `HDIMG.h0`, `FDIMG.BIN`, `FDIMG_USEROM.BIN`, `TGBIOS.BIN` 等）。

- submodule 追加後、`release/HDIMG.h0`（または `CDIMG.ISO`）を本リポジトリの `ROM_MX` と組み合わせて津軽で起動する。
- 既存の ARCL MCP（`arcl_mount` → `arcl_run` → `arcl_screenshot`）で、ビルド作業なしに Panic Ball 2 / VSGP / Sky Duel 等が動く状態を確認する。
- この時点で「別途 Tsugaru_CUI を単体ビルドする」必要はない。既にビルド済みの `arcl_windows_fmtowns.exe`（MCP サーバ）だけで完結する。理由: `-SHAREDDIR`/`-FD0`/`-UNITTEST` といった Tsugaru_CUI のバッチ機能は、実装上ただの「対話プロンプトを止めて自動実行する」フラグ（`TOWNSEMU/src/main_cui/argv/townsargv.cpp:570`, `interactive=false`）にすぎず、[towns_arcl_spec.md](../reference/towns_arcl_spec.md) が設計する `arcl_host_dir`（=共有ディレクトリ）、`arcl_mount`（=起動メディア差し替え）、`arcl_run`（=自動実行）、`arcl_command`（=type→prompt一致までrun→console_read）、`arcl_console_read`（=出力確認）で同等以上のことができる。

**検証結果（2026-08-14）**: CD (`CDIMG.ISO`) と HDD (`HDIMG.h0`) の両方で起動確認済み。

- CD は `arcl_mount`（`kind: cd`）でホットマウント可能。`arcl_run` → `arcl_screenshot` でブート完了（プロンプト `Q:\>`）を確認。
- **HDD (`-HD0`) は `arcl_mount` のホットスワップ非対応**（スキーマ上 `kind` は `floppy`/`cd` のみ）。起動時のコマンドライン引数でしか指定できないため、`.mcp.json` の `arcl_windows_fmtowns` サーバ `args` に `-HD0 <path>` を追加し、MCP サーバを再接続する必要があった。
  - **引数の順序に注意**: `-HD0 <path>` は ROM ディレクトリより**後ろ**に置く。前に置くと `Undefined Option or Insufficient Parameters` で起動失敗する（実機で確認済み）。
  - 起動後は `D:\>` プロンプトまで到達（CD起動時は `Q:\>`）。
- 現在の `.mcp.json` は `-HD0 E:\arcl_windows_fm_towns\FreeTOWNSOS\release\HDIMG.h0` 付きで固定されている。CD 起動の検証など HDD 抜きに戻したい場合は、この `-HD0` 行を除いて再接続すること。

### Step C — 自作プログラムのビルド＆実行（コンパイラだけ必要、OSの再ビルドは不要）

Step B で動作確認した release イメージを実行環境として使い続けたまま、自分で書いたプログラムだけをホスト側でビルドしてゲストに持ち込む。FreeTOWNSOS 自体のビルドシステム（`tgbios/makedisk.py` 等）には触れない。

1. コンパイラ/アセンブラを用意する（優先順位は下記「コンパイラの選択」）。
2. 自作プログラム（アセンブリまたは C）をホストでビルドし、FM TOWNS ネイティブ実行形式（`.EXP` 等）を得る。
3. `arcl_host_dir` で成果物のディレクトリをゲストに共有する（or `arcl_mount` でイメージに追加する）。
4. Step B で起動した release イメージ環境（FreeTOWNSOS / Free386 が動いている状態）の上から、共有ディレクトリ経由で自作プログラムを実行する。
5. `arcl_console_read` / `arcl_screenshot` で結果を確認する。

#### コンパイラの選択（優先順位）

1. **NASM 単体**（無料）— アセンブリだけで足りる範囲（ブートローダ相当の小さいプログラムなど）はこれで完結。FreeTOWNSOS 同梱の `externals/Free386`（DOSエクステンダ）と組み合わせる。
2. **TOWNS-gcc / TOWNS-gpp**（無料、C/C++ が要る場合）— コミュニティ製の FM TOWNS 向け GCC 移植版。

   | 項目 | 内容 |
   |---|---|
   | 配布元 | [anikun.kutami.jp/towns-gcc](https://anikun.kutami.jp/towns-gcc/)（非公式ページ、Copyright 1999-2023 Anikun） |
   | 対応言語 | TOWNS-gcc = C（ANSI準拠）、TOWNS-gpp = C++ |
   | 生成物 | FM TOWNS ネイティブ実行形式（`.EXP`）。グラフィック/サウンド等 TOWNS の機能を利用可能 |
   | 必要な DOS エクステンダ | `RUN386`（Towns OS 付属）、または **`Free386`（無料・FreeTOWNSOS に `externals/Free386` として同梱済み）**、`EXE386`、`Light Extender` のいずれか |
   | 由来 | 配布ファイルに `djdev110.zip` 等があり、DJGPP 初期版（GCC 2.x 系）ベースと推測される。古い GCC のため ANSI C 準拠・C++ サポートは限定的 |
   | 注意 | GCC 本体は GPL だが、TOWNS 移植部分のライセンス表記はページ上に明記なし。使用前に同梱の `INSTALL.DOC` 等で確認する |

   `Free386` は FreeTOWNSOS に既に無料同梱されているため、**TOWNS-gcc + Free386 の組み合わせなら、Towns OS の実機媒体も High-C も一切不要な、完全無料の C/C++ 開発環境**が組める可能性が高い。着手する場合はまずこの組み合わせを検証する。

3. **High-C Multimedia Kit / 386ASM Toolkit**（商用・未保有・最終手段）— 1・2で要件を満たせない場合のみ。ユーザー自身が中古媒体を入手する必要がある（Claude 側では代行不可）。

#### 検証結果（2026-08-14）— 選択肢2（TOWNS-gcc）は LD（リンク）で行き詰まり

**入手・構築**: [anikun.kutami.jp/towns-gcc](https://anikun.kutami.jp/towns-gcc/) から以下を取得し、`tools/towns-gcc-toolchain/usr/{bin,lib,include}` にマージして再構成した（個々のアーカイブは相互に依存するパッチ形式で提供されているため、素の展開だけでは完結しない）。

| 取得物 | 役割 |
|---|---|
| `gnucd2x1.lzh` / `gnucd2s3.lzh`（GNU CD2 標準版） | ベース一式。**GAS（アセンブラ）、標準Cヘッダ（stdio.h等）、LIBC.A はここにしかない** |
| `tgp245_0.lzh` / `tgp245_1.lzh`（TOWNS-gpp 2.4.5） | 新しめの GCC.EXE / CC1.EXP / CPP.EXP / LD.EXP / AR.EXP 一式（`BINBIN.LZH`/`LIBBIN.LZH`を展開すると、パッチ適用済み前提の`INSTALL.EXE`を経由せず直接使えるバイナリが手に入る） |
| `gcc272.lzh` | 最新の CC1.EXP（コンパイラ本体）+ LIBGCC.A |
| `libtn.lzh`, `libsys.lzh`, `rlib1.lzh` | TOWNS BIOS ラッパー（`LIBTN.A`: EGB/MOS/IO/SPR、`LIBSYS.A`: システム情報） |
| `egb.lzh`, `snd.lzh` | グラフィックス/サウンドヘッダの補足（**完全な `EGB.H` 自体は元々 High-C Multimedia Kit 付属のもので、この配布には含まれていない**。K&R形式の暗黙関数宣言で回避可能だった） |

**ビルドパイプラインの実体**（`GCCF.BAT`/`GCLF.BAT` 等から判明）: `RUN386 cpp → RUN386 cc1 → RUN386 gas → RUN386 ld → genexp` の5段階。`RUN386` という名前はTOWNS OS付属コマンドを指すエイリアスで、ここでは `FreeTOWNSOS/externals/Free386/free386.com` を `RUN386.EXE` として配置して代用した。

**結果**:
- ✅ `CPP → CC1 → GAS` は完全に成功。グラフィックスサンプル（`EGB_init`/`EGB_box`/`EGB_maru`/`EGB_putSjis` 等を呼ぶ最小プログラム）が正しい386アセンブリにコンパイルされ、正常にオブジェクトファイル化された（K&R形式の暗黙関数宣言で `EGB.H` なしでもコンパイル可能）。
- ❌ **`LD.EXP`（GNUリンカ）が Free386 上で必ずハングする**。引数なしの起動テストでも、Free386 の起動バナー表示直後に停止し、`Ctrl+C` も効かない。新旧2種類の `LD.EXP`（1991年版・1993年版）で同じ症状。
- ❌ 代替の DOS エクステンダ「Light Extender」（`le11.lzh`）は `Sorry. Can't launch.` と即座に起動拒否（同梱ドキュメントに「実機Windows の DOS プロンプト前提」と明記されており、素の DOS 互換環境では動作対象外と判明）。
- **調査で判明した副次情報**: EGB（グラフィックスBIOS）はソフトウェア割り込みではなく、**プロテクトモードの call gate（セレクタ `0x0110`、オフセット `0x20`）経由の far call** で呼ばれる（`libtn/LIB_EGB/EGB_S1.S` で確認）。そのため real-mode の `.COM` から直接呼ぶことはできず、Native Mode（386拡張モード）での実行が前提になる。

**結論**: 現状のツールチェーンでは `.EXP`（Native Mode実行ファイル）を作る最終段階（リンク）が通らず、TOWNS-gcc 経由でのグラフィックス/サウンドプログラム作成は**未達**。前処理・コンパイル・アセンブルは動作確認済みなので、環境（Free386の未実装機能、または30年前のLD.EXPとの相性）の問題を特定できれば道は開ける可能性がある。

**次に試すとしたら**:
- NASM で直接 32bit プロテクトモードコードを書き、GENEXP.EXE で `.EXP` 化する（GNU LD を経由しない）。ただし `.EXP` ローダのエントリ規約（セグメント初期化、スタック設定等）をゼロから解析する必要があり、まとまった追加調査が要る。
- ~~ARCL の L2 デバッガレイヤー（レジスタ/ブレークポイント/逆アセンブル）で `LD.EXP` の停止箇所を直接調べる。~~ → **2026-08-14 実施済み。下記参照。**

#### L2デバッガによる LD ハング原因調査（2026-08-14）

`--mcp-layers l0,l1,l2` を `.mcp.json` に追加して MCP サーバを再接続し、`arcl_registers`/`arcl_disasm`/`arcl_read_mem`/`arcl_stack` で LD0.BAT（引数なし LD 起動）のハング状態を直接調べた。

- **ハング位置**: `CS:EIP = 1000:0000361A` の `JMP 0000361A`（自己ループ、`cr0=0` すなわちリアルモード）。
- **分岐条件**: 直前に `MOVZX ECX,CX` → `MOV ECX,FS:[DI+15H]` → `CMP ECX,FS:[DI+11H]` → `JA 0000361A`。`arcl_read_mem` で実測したところ `ECX`（`FS:[DI+15H]`由来）`=18`、`FS:[DI+11H]=0`。「要求値18 が 上限値0 を超えた」形になり、本来は起こり得ないはずの安全弁的分岐がそのまま無限ループに落ちている。
- **重要な結論**: このコードは **LD.EXP 自身ではなく、Free386（`free386.com`）自身のリアルモード起動処理内**にある。つまり **LD.EXP には一度も制御が渡っていない**。Free386 が自分のプログラムロード/ディスパッチ処理の中で、何らかの「上限」フィールドが 0（未初期化）のままになっており、そこでハングしている。
- **Free386 はソース公開されている**: [github.com/nabe-abk/free386](https://github.com/nabe-abk/free386)（NASM製、PDS）。今回はリポジトリ全文検索ができず、該当箇所（`FS:[DI+11H]`/`FS:[DI+15H]` を使う構造体、判定条件の意味）の特定までは至らなかった。

**新たな有望策の発見**: Free386 と同じ作者（nabe@abk）が **[FlatLink](https://github.com/nabe-abk/flatlink)** という別のリンカを公開している。NASM が生成する OMF 形式の obj から Phar Lap `.EXP`/`.COM` を直接生成するツールで、Free386 と同じ作者による組み合わせのため、GNU LD で起きた相性問題を避けられる可能性が高い。ただし GAS（GNU アセンブラ、a.out 形式出力）とは非互換なので、採用する場合は **コード自体を NASM で書く**（GCC/CPP/CC1/GAS の GNU パイプラインは使わない）方針になる。これは「NASM で 32bit プロテクトモードコードを書き、EGB を call gate（セレクタ `0x0110`、オフセット `0x20`）経由で呼ぶ」という、以前から検討していた方向性と合致する。FlatLink がその `.EXP` 化（GENEXP 相当）とリンクを一手に引き受けてくれる。

#### 検証結果（2026-08-14）— 選択肢1（NASM単体）で Step C 完走

1. **NASM 導入**: choco は無権限で失敗（`Unable to obtain lock file access`）。代わりに [nasm.us](https://www.nasm.us/) 公式サイトから win64 ポータブル zip（`nasm-2.16.03-win64.zip`, 約500KB）を取得し `tools/nasm-2.16.03/` に展開（インストーラ不要、Git管理対象外）。
2. **共有ディレクトリの追加**: `-HD0` と同様、`-SHAREDDIR <path>` も**起動時のコマンドライン引数**でしか設定できない（`arcl_host_dir` は一覧表示のみで追加機能はない）。`.mcp.json` に `-SHAREDDIR E:\arcl_windows_fm_towns\arcl_dev_shared` を追加し、MCP サーバを再接続して反映した。引数順は ROM ディレクトリの後ろ、`-HD0` と同様。
3. **最小サンプル**: `arcl_dev_shared/hello.asm`（16bit real-mode DOS `.COM`、`org 0x100` + `INT 21h AH=09` で文字列出力）を NASM で `HELLO.COM`（53バイト）にビルド。**Free386/TOWNS-gccいずれも使わず、real-mode COM形式なので TOWNS の DOS 互換シェル上でそのまま実行できる**（386|DOS-Extender 経由の `.EXP` 形式より単純な検証経路）。
4. **実行確認**: `arcl_host_dir` で共有ディレクトリがドライブ **`E:`** としてゲストから見えることを確認 → `DIR E:` で `HELLO.ASM`/`HELLO.COM` が見える → `E:` → `HELLO` で実行 → `HELLO FROM STEP C - ARCL-BUILT PROGRAM` が出力された。

## 決定済み事項（2026-08-14 ユーザー確認済み・改訂）

1. FreeTOWNSOS の取り込み方法: **git submodule**
2. メインルートは Step B(動作確認・ビルド不要) → Step C(自作プログラムだけコンパイラでビルドし、release イメージ環境の上で実行)
3. コンパイラの優先順位: **NASM単体 → TOWNS-gcc(無料GCC)+Free386 → High-C(商用・未保有)**
4. Tsugaru_CUI 単体ビルド: **不要**。ARCL MCP が同等の自動化を代替できるため
5. 今回の対応範囲: **レポート・計画のレビューまで**。実装は次回以降、着手指示を受けてから開始する。
6. **（2026-08-14 追記）TOWNS-gcc 経由のグラフィックス/サウンドプログラム作成は LD（リンク）で行き詰まり、いったん保留**。コンパイラパイプライン（CPP/CC1/GAS）自体は動作確認済み。詳細は上記「検証結果 — 選択肢2」を参照。

## EGB 最終検証（2026-08-14）

GNU LD の代替は、既存の Free386 と FlatLink を使う NASM/OMF 経路で確定した。`flatlink -f exp` が Phar Lap P3 `.EXP` を生成し、`RUN386` が実行するため、CRT0 の追加解析は不要だった。

- 正とした実装は `FreeTOWNSOS/tests/tgbios/EGBGRAD.C` / `EGBLINE.C`。page 0/1 をともに mode 10 に設定し、page 1 → page 0 の順に clear してから描画する。
- NASM 側では起動時に `ES=GS=DS`、各EGB呼び出しで `FS=0x0110; call far [FS:0x20]` を使う。
- `EGBSIM.EXP` は `RUN386 -c0` と `RUN386 -c3` の双方で `EGB TEXT OK` を表示した。`EGBDEMO.EXP` は矩形・円・対角線・`EGB SHAPES + TEXT OK` を一画面に表示した。
- DOS 終了時の CRTC 復帰を避けるため、デモは表示後に保持ループへ入る。再現時は入力FIFOのため、起動コマンドを1文字ずつ `arcl_type` → `arcl_run(frames=3)` で送り、Return後に120 frame進める。
- 証跡: `arcl_dev_shared/arcl-output/egbdemo-c0-final.png`。frame 3804 で `CR0=0x80000001` のまま保持されており、終了後の偽陰性ではない。
- ARCL のスクリーンショットは、異常な一時 CRTC サイズを事前検査し、非圧縮PNGで出力するよう改善した。対象のARCLテスト3件は全て通過。
- `arcl_run` に `ignore_vm_pause`（"Break Request from VM" を無視して連続実行）を追加。ただしこの停止は EGB 呼び出し全てをカバーするわけではなく、後述のサウンドデモでも同じ箇所で再現した。実運用では `ignore_vm_pause=true` を渡した上で、稀に来る `stop_reason:"breakpoint"` を `arcl_step` 1回 → `arcl_run` 再開で抜ける運用に倒すのが確実。

## EGB + サウンド デモ（2026-08-14、`egb_sound_demo` タスク完了）

`EGBSND.ASM`（NASM+FlatLink、GCC/GNU LD不使用）で、グラフィックスと音声を同時に鳴らすデモを完成させた。

- **`SND_key_on` 単体では無音だった**。原因: FM音色は `.FMB` バンクファイルのロード（`SND_fm_bank_load`）で初めて設定され、バンク未ロードのチャンネルには有効な音色がない。バンクファイルを持っていないため、`SND_fm_write_data`（T-BIOS AH=0x11、`sndlib/SND_2.S` 由来）で **YM2612 相当チップのレジスタを直接叩く**方式に切り替えた（アルゴリズム7=全オペレータ並列、TL=0、キーオン0x28=0xF0 など約30レジスタを直書き）。
- それでも無音だったため、Explore agent で TOWNSEMU のサウンド経路を調査。**真因はチップではなくミキサーの出力ゲート**だった: `TownsSound::ProcessSound()`（`TOWNSEMU/src/towns/sound/sound.cpp`）は `muteFlag`（port `0x4D5` bit1）と `audioFlag`（port `0x4EC` bit6）が両方立っていないと、YM2612 の出力を実バッファではなく破棄用ダミーバッファへ書く。両フラグともリセット後は 0（ミュート）がデフォルトで、チップ自体は正しく発音状態になっていても音声出力には一切乗らない。
- 修正: ネイティブモード（IOPL=3）で直接 `OUT DX,AL` により `port 0x4D5 = 0x03`（FM+PCMミュート解除）、`port 0x4EC = 0x40`（オーディオ出力有効）を発行してから発音。
- 結果: `arcl_audio_record` で `peak=27280, rms=18521, silent=false` を確認。同時に `arcl_screenshot` で青背景+`EGB + SND OK` テキストも確認（グラフィックスとサウンドが同一プログラム内で共存）。
- 証跡: `arcl_dev_shared/arcl-output/egbsnd_final.png`, `egbsnd_audio4.wav`。

## ROM トレーサビリティ確認（2026-08-14、`towns_rom_verify` タスク完了）

`TOWNSEMU/roms/ROM_MX/*.ROM` を [TOWNSROM](https://github.com/captainys/TOWNSROM) の `release/forTsugaru/` および `release/forUNZ/`（プロジェクト自身がコミットしているビルド済みROM）と SHA-256 でハッシュ比較した。

| ROM | 一致状況 |
|---|---|
| `FMT_DIC.ROM` | ✅ 完全一致（forTsugaru/forUNZ とも同一） |
| `FMT_F20.ROM` | ✅ 完全一致（forTsugaru/forUNZ とも同一） |
| `FMT_FNT.ROM` | ✅ 完全一致（forTsugaru/forUNZ とも同一） |
| `FMT_DOS.ROM` | ❌ 不一致（forTsugaru/forUNZ 同士は一致、こちらだけ異なる） |
| `FMT_SYS.ROM` | ❌ 不一致（3者とも異なる値） |

DIC/F20/FNT（合計 1.3MB 分）がバイト単位で一致しているため、**TOWNSROM 由来であることはほぼ確実**。DOS/SYS が食い違うのは、README にある通りこの2つが YSDOS.SYS/YAMAND.COM を含む活発に更新される領域であり、**手元の ROM_MX が TOWNSROM の別リビジョン（浅いclone のため正確な特定は未実施）からビルドされたため**と考えられる。実害はなく、`--arcl-allow-root` 配下で問題なく動作している。追加のトレーサビリティが必要になれば、TOWNSROM の全履歴を取得して DOS/SYS 生成物が一致するコミットを特定できる。

## 追記: `mcDomDom/tgcc_inst` — Windows完結のTOWNS-gccクロスコンパイル環境（2026-08-14）

ユーザーの情報提供で [mcDomDom/tgcc_inst](https://github.com/mcDomDom/tgcc_inst) を発見・検証した。これは**Windows上だけで `.c` → `.exp` を完結できる**TOWNS-gccクロスコンパイル環境で、以前ぶつかった「Free386上でGNU LDがハングする」問題を、**そもそもFree386を経由しないリンカに差し替える**ことで根本的に回避している。

### パイプライン

```
.c → (djgpp_v2 gcc 2.7.2.3, Windows上で直接実行) → .s
   → (as.exe, MinGWでビルドしたi386-linux-aout版binutils) → .o
   → (ld_towns.exe, TOWNS用GNU CDのBINUTILY LDソースをVisual Studio 2010で再ビルド) → .bin
   → (genexp3.exe, TOWNS用GNU CDのGCCTOOLソースをVisual Studio 2010で再ビルド) → .exp
```

**4段階すべてがWindowsネイティブバイナリとして動作し、Free386/TOWNSEMUを一切経由しない。** リポジトリには上記バイナリ一式（`bin/gcc.exe`前身のdjgpp、`as.exe`、`ld_towns.exe`、`genexp3.exe`）と、TOWNS-gcc資産（`gcc272.lzh`, `gnucd2*.lzh`, `libtn.lzh`, `libsys.lzh`, `egb.h` 等）、インストーラ（`tgcc_inst.cmd`）が同梱されている。

### セットアップで要ったもの・ハマったところ

- 前提として **djgpp_v2アーカイブ**（SourceForge、約25MB）と**LHA32.exe**（Vector配布の自己解凍書庫 `lha267.exe`）が必要。`lha267.exe` はWin16アプリで64bit Windows上で自己解凍できないが、**7-Zipで直接開けば中身（LHA32.EXE）を取り出せる**（`lha267.exe` を7zで `x` すると `Cannot open as [PE]` の警告と共に `Lzh` アーカイブとして認識され、正常に展開できた）。
- `tgcc_inst.cmd <インストール先>` でセットアップ。**インストール先を `C:\` 直下にすると `ld_towns` が `Permission denied for C:/.../lib` で失敗**した（`E:\` 配下の作業ディレクトリに変えたら解消）。原因はおそらく `C:\` 直下フォルダに対するOS側の権限制限。
- `set LIBRARY_PATH=... && ... && ld_towns ...` のように**環境変数の設定と参照を`&&`で1行に繋げると、Windowsのバッチ展開は行全体を先に展開するため未設定のまま参照されてしまう**（`LIBRARY_PATH=%LIBRARY_PATH%` という文字通りの値になる）。**複数行の`.cmd`ファイルに分ければ解消**（1行ずつ順に実行されるので次の行では正しく展開される）。

### 検証結果

`test/ctest.c`(EGBDEMO.ASMと同等の内容: `EGB_init`→`resolution`(mode10×2ページ)→`clearScreen`→`box`→`maru`→`displayPage`→無限ループ)を実際にCで書いて `tbld.cmd ctest` でビルドし、`RUN386 -c0` で実行 → **矩形・円を含む正しいグラフィックスがそのまま表示された**（`arcl_dev_shared/arcl-output/ctest_result.png`）。

### 位置づけ

- **NASM+FlatLink**（先に確立した経路）と**tgcc_inst**（今回発見）はどちらも「Free386のLDを経由しない」という同じ考え方で問題を回避しているが、tgcc_inst は**実際のC言語**が使え、TOWNS-gcc本来のライブラリ（`libtn.a`にEGB・sndlib・システムBIOSまで統合済み）をそのまま利用できる点で、今後の開発効率は大きく上回る。
- 今後 C で本格的に自作プログラムを書く場合は **tgcc_inst を第一選択**とし、NASM+FlatLinkは「Cで足りない/確認したい低レベル処理（今回のミキサーゲート直叩き等）」のピンポイント用途に位置づけるのが良さそう。
- サウンド（ミキサーゲート `port 0x4D5`/`0x4EC`）をCから叩くには、GCC 2.7.2.3のインラインアセンブラ（`asm`キーワード）か、`outportb`相当のヘルパー関数の有無を要確認。未検証。
