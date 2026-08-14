# FM TOWNS 自作ソフト／自作OS 開発環境 調査レポート

作成日: 2026-08-14

## 1. 目的

本リポジトリは TOWNSEMU（津軽）ベースのエミュレータ + MCP サーバとして、既存の FM TOWNS ソフトウェアを AI エージェントから操作することに主眼を置いている。
ここでは追加で「FM TOWNS 向けに自作ソフト（将来的には自作 OS）をビルドできる開発環境」を整えるための前提調査を行った。

## 2. 見つかった主要プロジェクト

いずれも TOWNSEMU（本リポジトリが基にしている津軽エミュレータ）と同じ作者 **Soji Yamakawa（CaptainYS）** による、無償公開・オープンソースの関連プロジェクト。

| プロジェクト | URL | 役割 |
|---|---|---|
| **FreeTOWNSOS**（Tsugaru OS） | https://github.com/captainys/FreeTOWNSOS | 著作権フリーの FM TOWNS 互換 OS。Panic Ball 2 / VSGP / Sky Duel などのフリーゲームを、実機の Towns OS 資産なしで起動できる |
| **TOWNSROM** | https://github.com/captainys/TOWNSROM | 著作権フリーの互換 ROM（FMT_SYS / FMT_DOS / FMT_DIC など）を生成する Free FM TOWNS Project。**本リポジトリの `TOWNSEMU/roms/ROM_MX/FMT_*.ROM` はこのプロジェクトの成果物と同一の命名規則** |
| **TOWNSEMU（津軽）** | https://github.com/captainys/TOWNSEMU | 本リポジトリが既に組み込み済みのエミュレータ本体。CUI 版 (`Tsugaru_CUI`) はバッチ実行・自動ビルドの母艦として使える |

→ 本リポジトリはすでに TOWNSEMU を取り込んでおり、README にある ROM 一式（`FMT_DIC.ROM` など）も TOWNSROM 由来の互換 ROM である可能性が高い。**FreeTOWNSOS の開発環境は、この既存資産の上にほぼそのまま乗せられる。**

## 3. FreeTOWNSOS の開発環境要件

### 3.1 必要なツール

| ツール | 入手性 | 備考 |
|---|---|---|
| Python | 無料 | ビルドスクリプト（`makedisk.py`, `buildall.py` 等）実行用 |
| NASM (Netwide Assembler) | 無料 | PATH に通す必要あり |
| Visual C++ 2019 以降 | 無料枠あり（Community） | Developer Command Prompt から実行 |
| Make | 無料 | |
| Tsugaru（津軽）エミュレータ | 無料・本リポジトリに同梱済み | `Tsugaru_CUI` をビルド／使用。バッチ実行 (`-UNITTEST` オプション等) でホストから自動ビルドを駆動できる |
| 互換 ROM (CompROM) | 無料・TOWNSROM 由来 | 本リポジトリの `TOWNSEMU/roms/ROM_MX` がそのまま使える見込み |
| **High-C Multimedia Kit V1.7 L12** | **有償・入手困難** | 富士通純正の C コンパイラ。1995年発売の商用ソフトで公式配布なし。README には「ヤフオクに時々出品される」と明記。中古 CD-ROM を各自入手する必要がある。**代替: 3.2.2 の TOWNS-gcc/TOWNS-gpp（無料）** |
| **FM TOWNS 386 ASM Toolkit** | **有償・入手困難** | 富士通純正アセンブラ拡張。同じく中古入手が前提。Internet Archive にはマニュアルのスキャンのみあり、フロッピー実体は意図的に含まれていない |
| Free386（DOS エクステンダ） | 無料（Public Domain, nabe 氏） | FreeTOWNSOS に `externals/Free386` として同梱済み |
| ORICON | 無料（再配布可, MIYAZAKI/YAMAZAKI 氏） | FreeTOWNSOS に `externals/ORICON` として同梱済み |

### 3.2 セットアップの流れ（README + `HIGH_C_FROM_TSUGARUOS.md` より）

1. 津軽（`Tsugaru_CUI.exe`）を PATH に通す。
2. 互換 ROM を `C:\FreeTOWNSOS\CompROM` に配置。
3. High-C の CD-ROM から `HC386` 一式を `C:\TOWNSDEV` にコピー。
4. `HC386.CNF` の `NATIVERUN` 行を `NATIVERUN=A:\FREE386.COM` に書き換え。
5. `HC386SET.CNF` 内のドライブレター `q:` を `d:` に置換。
6. `C:\TOWNSDEV\TASK.BAT` を作成し、`PATH` / `HCDIR` / `IPATH` / `LPATH` を設定。
7. 津軽起動後、D ドライブで `AUTOCFIG` を実行しコンパイラを初期化。
8. 以降はホスト側から `tgbios` の `makedisk.py` を実行するだけで、津軽を裏で自動起動してビルドし、`HDIMG.h0` / `FDIMG.bin` / `FDIMG_USEROM.bin` の3バイナリを生成できる（`BATCH_EXEC.md` の自動実行フロー）。

### 3.2.1 バイナリ配布について

GitHub の Releases 機能は使われていないが、リポジトリの **`release/` フォルダにビルド済みの起動イメージがそのままコミットされている**。

| ファイル | サイズ目安 | 用途 |
|---|---|---|
| `CDIMG.ISO` | 1.7MB | 起動用CDイメージ |
| `HDIMG.h0` | 8.4MB | 起動用HDDイメージ |
| `FDIMG.BIN` / `FDIMG_USEROM.BIN` | 各1.2MB | 起動用フロッピーイメージ |
| `FD_IPL.BIN` | 960B | IPLブートセクタ |
| `TGBIOS.BIN` / `TGBIOS.SYS` | — | BIOS本体 |
| `IO.SYS`, `RAMDRIVE.SYS`, `REPLACE.SYS`, `MINVCPI.SYS` 等 | — | ドライバ |

これらは互換 ROM（`CompROM`）と組み合わせるだけで津軽から即起動できるため、**「動かして試す」だけなら NASM / High-C いずれのビルド環境も不要**。開発環境の構築が要るのは、OS自体を改造・拡張したい場合のみ。

### 3.2.2 High-C の無料代替: TOWNS-gcc / TOWNS-gpp

コミュニティ製の FM TOWNS 向け GCC 移植版が存在する。High-C の代替候補。

| 項目 | 内容 |
|---|---|
| 配布元 | [anikun.kutami.jp/towns-gcc](https://anikun.kutami.jp/towns-gcc/)（非公式ページ、Copyright 1999-2023 Anikun） |
| 対応言語 | TOWNS-gcc = C（ANSI準拠）、TOWNS-gpp = C++ |
| 生成物 | FM TOWNS ネイティブ実行形式（`.EXP`）。グラフィック/サウンド等 TOWNS の機能を利用可能 |
| 必要な DOS エクステンダ | `RUN386`（Towns OS 付属）、または `Free386`（無料、**FreeTOWNSOS に `externals/Free386` として同梱済み**）、`EXE386`、`Light Extender` のいずれか |
| 由来 | 配布ファイルに `djdev110.zip`/`djlsr110.zip`（DJGPP 初期版）が含まれ、GCC 2.x 系ベースと推測される。古い GCC のため ANSI C 準拠・C++ サポートは限定的とみられる |
| ライセンス | GCC 本体は GPL。TOWNS 移植部分固有のライセンス表記はページ上に明記なし。使用前に同梱の `INSTALL.DOC` 等で確認が必要 |

**Free386 は FreeTOWNSOS に無料同梱済みのため、TOWNS-gcc + Free386 の組み合わせなら、Towns OS の実機媒体も High-C も一切不要な、完全無料の C/C++ 開発環境が組める可能性が高い。** これは Phase 4（商用ツール導入）を回避できる有力なフォールバック経路になる。

### 3.3 ディレクトリ構成（FreeTOWNSOS リポジトリ）

`CompROM/`, `buildenv/`, `experiments/`, `externals/`(Free386, ORICON), `iosys/`, `memo/`, `release/`, `resources/`, `scripts/`, `symtables/`, `tests/`, `tgbios/`（ビルド起点）, `tgdrv/`, `tgmenu/`, `tgutil/`, `util/`

## 4. ライセンス

- FreeTOWNSOS 本体: Soji Yamakawa（CaptainYS）, 2021年。BSD 系（"as-is"、著作権表示・免責文の保持が条件、著作者名を宣伝に無断使用することを禁止）。GitHub 上は "Other (NOASSERTION)" 扱いなので、再利用前に文面を厳密に確認すること。
- 同梱の Free386 / ORICON はそれぞれ別ライセンス（Free386 はパブリックドメイン、ORICON は再配布自由）。
- **High-C Multimedia Kit と 386ASM Toolkit は富士通の商用ソフトウェアであり、このプロジェクトのライセンス対象外。ユーザー自身が正規に入手した媒体を用意する必要がある**（本リポジトリの README にある「ROM は各自用意」と同じ考え方）。

## 5. 本リポジトリとの統合における論点

1. **ROM の再利用**: `TOWNSEMU/roms/ROM_MX/*.ROM` が TOWNSROM 由来なら、FreeTOWNSOS の `CompROM` としてそのまま使える可能性が高い（要確認: TOWNSROM 生成物とバイト一致するか）。
2. **Tsugaru_CUI の利用**: `TOWNSEMU/src/main_cui` は既にソースツリーに存在。現在の README のビルド手順は `arcl_windows_fmtowns` ターゲットのみを対象にしているため、`Tsugaru_CUI` 単体ビルドも別途通す必要がある。
3. **商用ツールの壁**: High-C / 386ASM を持たない場合、FreeTOWNSOS のフルビルド（C 言語部分含む）は不可。NASM で書けるアセンブリ主体の自作プログラム（フリーゲーム相当）であれば、High-C なしでも開発を始められる可能性がある。
4. **著作権的な立ち位置**: 本プロジェクトの README にある「ROM/OS/ゲームは同梱・再配布しない」という方針と同様、High-C/386ASM の媒体もこのリポジトリには絶対に含めない。

## 6. 未確認・要追加調査

- TOWNSROM の ROM 生成物と、本リポジトリが既に持つ `ROM_MX` 一式が完全一致するか（ビルドしてハッシュ比較、または `TOWNSROM/prep.py` の出力と突き合わせ）。
- FreeTOWNSOS を NASM + Free386 のみ（High-C 抜き）でどこまでビルドできるか（`tgbios` 配下のうち C 言語ソースの割合を要確認）。
- Windows 上での Python バージョン要件（README に明記なし、3.x 系と推定）。
- TOWNS-gcc/TOWNS-gpp: **2026-08-14 検証済み**。CPP/CC1/GAS（前処理・コンパイル・アセンブル）は Free386 上で正常動作。ただし GNU LD（リンク）が Free386 上でハングし、`.EXP` 生成まで到達できなかった。詳細は [fmtowns-dev-environment-plan.md](fmtowns-dev-environment-plan.md) の「検証結果 — 選択肢2」を参照。TOWNS 移植部分の正確なライセンス条件は依然未確認。

## Sources

- [FreeTOWNSOS (GitHub)](https://github.com/captainys/FreeTOWNSOS)
- [TOWNSROM (GitHub)](https://github.com/captainys/TOWNSROM)
- [TOWNSEMU / Tsugaru (GitHub)](https://github.com/captainys/TOWNSEMU)
- [Free Towns OS: an open source recreation of FM Towns OS – OSnews](https://www.osnews.com/story/141145/free-towns-os-an-open-source-recreation-of-fm-towns-os/)
- [Tsugaru OS – A New Free FM-Towns OS | Hacker News](https://news.ycombinator.com/item?id=42153535)
- [FMTOWNS 386 ASMTool Kit Users Manual (Internet Archive, マニュアルのみ)](https://archive.org/details/fmtowns-386-asmtool-kit-users-manual)
- [TOWNS-gpp (TOWNS-gcc) の非公式ページ](https://anikun.kutami.jp/towns-gcc/)
