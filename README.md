# myDAW

パターン（小節のブロック）を作って、パズルのように並べて曲を作るデスクトップ DAW です。
C++ / [JUCE](https://juce.com) 製で、Windows・macOS・Linux と **iPhone / iPad** で動きます（[iPhone / iPad で使う](#iphone--ipad-で使う)）。

![myDAW の画面](docs/screenshot.png)

## できること

| 仕様 | 実装 |
| --- | --- |
| 取り込んだ音 (mp3, wav) を打ち込める | WAV / MP3 / AIFF / FLAC / OGG を読み込んでチャンネルにする。ステップグリッドとピアノロール（音程・長さ・ベロシティ）で打ち込み |
| 打ち込んだ音を小節ごとに保存できる（小節の長さは自由） | 「パターン」= 1 小節以上のブロック。小節数・拍子（3/4, 5/4, 7/8 …）・グリッド分解能（3 にすると 3 連符）を自由に設定。パターン単体を `.mdpat` ファイルに保存して、別の曲で読み込める |
| 保存した小節をパズルのように組み合わせて曲を作る | ソング画面のトラックにパターンをドラッグ＆ドロップ。ブロックは隣のブロックの端に磁石のように吸着するので、長さの違う小節もぴったり並ぶ |
| VST を読み込める | VST3 のインストゥルメント（シンセ等）と エフェクト（チャンネルごと / マスター）を読み込み、プラグイン画面を開いて操作。設定はプロジェクトに保存される（macOS では Audio Unit も可） |
| 取り込んだファイルから範囲を選んでサンプリング（1 つのファイルから複数） | **サンプラー**: 曲などのファイルを開いて、範囲を選ぶたびに新しいサンプル（チャンネル）にできる。自動スライス（アタック検出 / 等分 / 拍ごと）でまとめて切り出しも可 |
| AI でボーカル・ギター・ドラム等を抽出（どれを抽出するか選べる） | **AI ステム分離**（Meta の Demucs v4 をパソコン上で実行）: ボーカル / ドラム / ベース / ギター / ピアノ / その他 / ボーカル以外（カラオケ）から選んで抽出。抽出した音からそのままサンプリングできる |
| 取り込んだ音 1 つ単位で、VST エフェクトなどで音を編集 | **サンプルエディタ**: 波形を見ながら範囲を選んでトリム・削除・無音化・フェード・ノーマライズ・音量・リバース。VST エフェクトを試聴しながら掛けて、音そのものに書き込める（リバーブの余韻も含む）。Undo / Redo、元に戻す、WAV 書き出し |

そのほか: BPM 変更、ミュート、音量 / パン、パターン再生 / ソング再生の切り替え、ソングのループ、WAV 書き出し、
プロジェクトの保存 / 読み込み（`.mydaw`）。

## 画面と操作

```
┌ 上部 ─ File / ▶ ■ ループ / PATTERN・SONG / BPM / 位置 / マスター音量 / Master FX / Plug-ins / Audio / Export WAV
├ 左 ─── パターン一覧（New・Clone・Delete・Save...・Load...）
├ 右上 ─ ソング（トラックにパターンを並べる）
└ 右下 ─ パターンエディタ（ステップグリッド / ピアノロール）
```

### 1. 音を取り込む
- **+ Sample** ボタン、またはパターンエディタに WAV / MP3 を **ドラッグ＆ドロップ** → チャンネルが追加されます。
- **+ Instrument** → VST3 シンセを追加（先に **Plug-ins** でスキャンが必要。下記）。
- チャンネル名をクリックすると試聴、ダブルクリックで名前変更、右クリックでメニュー。
- **...** ボタン: 音量・パン・ルートノート（サンプルが元の音程で鳴る鍵盤）・ワンショット・エフェクト。

### 1.5 音を編集する（サンプルエディタ）
取り込んだ音 1 つ 1 つを、波形を見ながら編集できます。チャンネル行の **Edit** ボタン
（または右クリック →「Edit sample」、**...** →「Edit sample...」）で開きます。

- **範囲選択**: 波形をドラッグ。Shift+クリックで範囲を広げる、ダブルクリックで全選択、Ctrl+ホイールで拡大縮小。
- **試聴**: ▶（またはスペースキー）。範囲を選んでいればその部分、選んでいなければカーソル位置から再生。Loop で繰り返し。
- **編集ボタン**: Trim（選択部分だけ残す）/ Delete / Silence / Fade in / Fade out / Normalize / -3 dB / +3 dB / Reverse。
  範囲を選んでいないときは音全体に掛かります。
- **VST エフェクトを音に書き込む**:
  1. 「+ Add effect」で VST3 エフェクトを追加（プラグイン画面が開くので音作りする）。複数つなげられます。
  2. ▶ で試聴（「Hear effects」が ON のとき、エフェクトを通した音が聞こえます）。
  3. 「Apply effects to selection / whole sample」で、選択範囲（または音全体）にエフェクトを書き込みます。
     Tail を「Auto」にすると、リバーブやディレイの余韻が切れずに残ります（音の後ろが伸びます）。
  4. 書き込んだ後、エフェクトは自動で OFF になります（2 回掛からないように）。
- **Undo / Redo**（Ctrl+Z / Ctrl+Shift+Z）、**Revert to original**（最初に取り込んだファイルに戻す）、**Export WAV...**（編集した音を書き出す）。
- 編集した音はプロジェクトを保存したときに `<プロジェクト名> Samples` フォルダへ WAV として保存されます。
  **元のファイルは書き換えません。**

> チャンネルの **...** → 「+ Add effect」は「再生するときにリアルタイムで掛かるエフェクト」（音そのものは変わらない）、
> サンプルエディタは「音そのものを書き換える編集」です。用途に応じて使い分けてください。

### 1.6 サンプラー（範囲を選んでサンプリング / AI で楽器ごとに分ける）
パターンエディタ右上の **Sampler / AI** ボタン（または File メニュー、チャンネルの右クリック →「Open in Sampler」、
サンプルエディタの「Sampler / AI stems」）で開きます。

- **ファイルを開く**: 「Open audio file...」かウィンドウにドラッグ＆ドロップ（WAV / MP3 / AIFF / FLAC / OGG）。
- **範囲を選んでサンプリング**: 波形をドラッグで選択 →「**Sample selection**」（Enter キー）。選ぶたびに新しいチャンネルが増えるので、
  1 つのファイルから何個でもサンプルを作れます。サンプリングした範囲には番号付きのマーカーが付きます。
- **自動スライス**: 「Auto-slice...」→ アタック（ドラムなどの打撃）で切る / 等分（2〜32）/ 拍ごと（曲の BPM で 1/4 拍〜1 小節）。
  選択範囲があればその中だけを切ります。番号をクリックでそのスライスを選択、右クリックで削除など。
  「**Sample all N slices**」で全部まとめてチャンネルにします。
- **AI ステム分離**: 「**AI: Separate stems...**」
  1. モデルを選ぶ: 6 ステム（ボーカル・ドラム・ベース・ギター・ピアノ・その他）または 4 ステム（ボーカル・ドラム・ベース・その他、ボーカルがよりきれい）。
  2. 抽出するものにチェック（「No vocals」= ボーカル以外を全部まとめた伴奏 / カラオケ）。
  3. 「Only the selected part」にすると選択範囲だけを分離（とても速い）。「Also add the stems ... as channels」で抽出結果をそのままチャンネルに追加。
  4. 「Separate」。初回だけ AI モデル（6 ステム 53 MB / 4 ステム 81 MB）を自動でダウンロードします。
  5. 終わると上の「SAMPLE FROM」にボーカル・ドラム…のボタンが並びます。押すとその楽器だけの音に切り替わり、
     そこから範囲を選んでサンプリングできます（例: ドラムだけを取り出してスネア 1 発を切り出す）。
- 分離は**パソコンの中で**行われます（音声はどこにも送信されません）。CPU で計算するため時間がかかります。
  目安: 数秒の選択範囲なら 30 秒〜1 分、1 曲（3〜4 分）なら CPU とメモリに応じて数分〜十数分。分離中も他の作業ができ、ウィンドウを閉じても続きます。
  1 つの処理に約 1.7 GB のメモリを使うので、メモリが多いほど並列に処理して速くなります。
- モデルの自動ダウンロードができない環境では、ブラウザで
  https://huggingface.co/datasets/Retrobear/demucs.cpp/tree/main から `ggml-model-htdemucs-6s-f16.bin`（または `-4s-`）を
  ダウンロードして「Load model file...」で読み込めます。

### 2. 打ち込む（パターンを作る）
- ステップグリッドのマスをクリックで ON / OFF、ドラッグで連続入力、右クリックで消去。
- **Keys** ボタンでピアノロール: クリックで音符追加、ドラッグで移動、右端ドラッグで長さ変更、右クリック / ダブルクリックで削除。
  サンプルも音程を変えて鳴らせます。
- Ctrl（または Alt）+ マウスホイールで音符のベロシティ（強さ）を変更。
- ヘッダーの **Bars**（小節数）・**Time**（拍子）・**Steps/beat**（分解能）で小節の長さを自由に決められます。
- **PATTERN** モードで ▶ を押すと、今のパターンをループ再生します（スペースキーでも再生 / 停止）。

### 3. 小節（パターン）を保存する
- 左のパターン一覧で **New**（新規）/ **Clone**（複製）。名前は一覧をダブルクリックかエディタ上部で変更。
- **Save...** でパターンを `.mdpat` ファイルに保存、**Load...** で別のプロジェクトに読み込み。
  使っているサンプル / プラグインの情報も一緒に保存されるので、読み込み先に無いチャンネルは自動で作られます。

### 4. パズルのように組み合わせる
- パターン一覧からソングのトラックへ **ドラッグ＆ドロップ**（または、トラックの空いている所をダブルクリック）。
- ブロックはドラッグで移動。**Alt / Ctrl + ドラッグでコピー**、Delete キーで削除、Ctrl+D で後ろに複製、右クリックでメニュー。
- **Snap** で吸着単位（小節 / 拍 / 1/2 / 1/4 / オフ）。さらに隣のブロックの端には自動で吸着します。
- ルーラー（小節番号の帯）をクリックするとその位置から **SONG** モードで再生。トラックの **M** でミュート。
- ソング中のブロックをクリックすると、そのパターンが下のエディタで開きます（同じパターンのブロックは全部一緒に変わります。
  1 つだけ変えたいときは右クリック →「Make unique」）。

### 5. VST を使う
1. **Plug-ins** → Options → 「Scan for new or updated VST3 plug-ins」でスキャン（初回のみ）。
   標準の場所: Windows `C:\Program Files\Common Files\VST3`、macOS `/Library/Audio/Plug-Ins/VST3`。
2. シンセ: パターンエディタの **+ Instrument**。エフェクト: チャンネルの **...** →「+ Add effect」、全体には **Master FX**。
3. プラグインの画面はチャンネル右クリック →「Open plug-in window」か、エフェクト行の **Edit**。

### 6. 書き出し
- **Export WAV**（Ctrl+E）でソング全体を 24bit WAV に書き出します。
- コマンドラインでも書き出せます: `myDAW --render song.mydaw out.wav [--samplerate 48000]`

### ショートカット
| キー | 動作 |
| --- | --- |
| Space | 再生 / 一時停止（■ を 2 回押すと先頭へ） |
| F5 / F6 | パターン再生 / ソング再生 |
| Ctrl+N / O / S | 新規 / 開く / 保存（Ctrl+Shift+S で別名保存） |
| Ctrl+E | WAV 書き出し |
| Delete, Ctrl+D | ソング: 選択ブロックの削除 / 複製 |

## ダウンロード

GitHub の **Actions** タブ → 最新の「Build」→ **Artifacts** から
`myDAW-Windows`（`myDAW.exe`）/ `myDAW-macOS`（`myDAW.app`）/ `myDAW-Linux` / `myDAW-iOS`（iPhone / iPad 用、
[入れ方](#iphone--ipad-で使う)）をダウンロードできます。

- macOS: 署名していないアプリなので、初回は右クリック →「開く」で起動してください。
- Windows: SmartScreen が出たら「詳細情報」→「実行」。

### 困ったとき（ウィンドウが画面からはみ出す場合）
- ウィンドウは画面の大きさ（Windows の表示スケール 125% / 150% なども考慮）に合わせて開き、前回の位置と大きさを覚えます。
  画面に収まらない位置は使わずに、自動で収まる位置に直します。
- それでもタイトルバー（閉じる・最小化ボタン）が見えないときは、Windows なら **Win + ↑**（最大化）、
  **Alt + Space** → 「移動」/「最大化」、**Alt + F4**（閉じる）が使えます。macOS は **Control + Command + F**（フルスクリーン）。

## iPhone / iPad で使う

同じアプリが iPhone / iPad でも動きます（iOS 15 以降）。iPhone は**横向き**で使います。

### インストール方法

App Store には出していないので、次のどちらかで入れます。おすすめは **A（SideStore、無料）** です。

**A. SideStore で入れる（無料の Apple ID でOK・Mac 不要）**

無料の Apple ID で入れたアプリは 7 日で期限が切れますが、[SideStore](https://sidestore.io) を使うと
iPhone だけで（パソコン無しで）期限を延ばせて、ショートカットで自動化もできます。パソコンが要るのは最初の準備の 1 回だけです。

*最初の準備（1 回だけ、Windows / Mac / Linux のパソコンを使う）*

1. パソコンの準備: Windows は [iTunes](https://apple.co/ms) を入れておきます（iPhone と USB でやり取りするため）。
   Mac はそのままで大丈夫です（Linux は `usbmuxd` が必要です）。
2. iPhone に App Store から **LocalDevVPN** を入れます（SideStore がアプリを入れるときに使う、iPhone の中だけの VPN です）。
3. パソコンに **iloader** を入れます。公式の配布元は [iloader.app](https://iloader.app) と
   [GitHub（nab138/iloader）](https://github.com/nab138/iloader) だけです。
4. iPhone を USB でつなぎ（初めてなら iPhone 側で「このコンピュータを信頼」）、iloader を開いて Apple ID でサインインし、
   **Install SideStore** を選びます。必要な設定（ペアリングファイルなど）は iloader が自動でやってくれます。
5. iPhone の「設定」→「一般」→「VPN とデバイス管理」で自分の Apple ID を「信頼」し、
   「設定」→「プライバシーとセキュリティ」→「**デベロッパモード**」を ON にします（再起動します）。
6. SideStore を開きます。Apple ID を聞かれたら、同じ Apple ID でサインインします。

*myDAW を入れる（ここからは iPhone だけ）*

1. GitHub の **Actions** タブ → 最新の「Build」→ **Artifacts** の `myDAW-iOS` をダウンロードし、
   「ファイル」アプリで zip をタップして展開すると `myDAW-iOS-unsigned.ipa` が出てきます
   （パソコンでダウンロードして、AirDrop や iCloud Drive で iPhone に送っても OK です）。
2. **LocalDevVPN** を開いて「Connect」します。
3. **SideStore** の「My Apps」タブ → 左上の **＋** → `myDAW-iOS-unsigned.ipa` を選びます。ホーム画面に myDAW が出てきます。

新しいバージョンにするときも同じ手順です（上書きされ、作ったプロジェクトは残ります）。

*7 日ごとの更新（ここが大事）*

- **LocalDevVPN を Connect** してから、SideStore の「My Apps」で **Refresh All** を押します。期限が来る前なら何度押しても大丈夫です。
- **自動にする**: 「ショートカット」アプリ →「オートメーション」→「時刻」（例: 毎日夜中）→ アクションに SideStore の
  「**Refresh All Apps**」を追加し、「実行前に尋ねる」をオフにします。実行するときに LocalDevVPN がつながっている必要があります。
- **期限を切らしてしまうと SideStore 自身も起動しなくなり**、もう一度パソコンで「最初の準備」の 4 からやり直しになります
  （作ったプロジェクトは消えません）。自動化しておくのがおすすめです。
- 無料の Apple ID では、SideStore 自身を含めて**同時に 3 本まで**しか入れられません。

> SideStore の最新の手順やトラブルのときは [SideStore の公式ドキュメント](https://docs.sidestore.io) を見てください。
> 7 日ごとの更新そのものを無くしたいときは、有料の Apple Developer Program（年 99 ドル）で 1 年有効にする方法もあります。

**B. Mac と Xcode がある場合: 自分でビルドして入れる**

```bash
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS
open build-ios/MyDAW.xcodeproj
```

Xcode で `MyDAW` ターゲットの「Signing & Capabilities」で自分の Team を選び、iPhone をつないで ▶ で実行します
（Bundle Identifier が使えないと言われたら `com.mydaw.app` を自分だけの名前に変えます）。
無料の Apple ID だと、こちらも 7 日で期限が切れます（そのたびに Xcode から入れ直し）。

### iPhone での操作（パソコン版との違い）

- **画面**: 左上の **Song / Pattern** でソング画面とパターンエディタを切り替えます（iPad など広い画面では両方表示）。
  **Menu** にファイル操作・WAV 書き出し・マスターエフェクト・オーディオ設定・プラグインがまとまっています。
- **右クリックの代わり = 長押し**: チャンネル名、パターン一覧、ソングのブロック、トラック名、サンプラーのスライス番号を
  長押しするとメニューが出ます。
- **ステップ入力**: マスをタップで ON / OFF、**横になぞる**と連続で入力、**縦になぞる**とスクロール。
  音符を長押しすると**強さ（ベロシティ）**と削除のメニュー（パソコンの Ctrl + ホイールの代わり）。
- **ピアノロール**: 空いている所をタップで音符を追加、音符をドラッグで移動 / 右端をドラッグで長さ、ダブルタップで削除、
  長押しで強さ。空いている所や鍵盤をなぞるとスクロールします。
- **ソング**: パターン一覧のパターンを**横に**ドラッグしてソングへ置きます（縦は一覧のスクロール）。
  空いている所をなぞるとスクロール、ダブルタップで選択中のパターンを置く、ブロックの長押しで複製・削除・
  「Edit pattern」（そのパターンをエディタで開く）。拡大縮小は **- / +** ボタン。
- **サンプルエディタ / サンプラー**: 全画面で開き、右上の × で閉じます。画面に入りきらない部分は上下にスクロールできます。
  波形をなぞると範囲選択です。

### ファイルの場所

- 保存するときは名前を聞かれ、アプリの書類フォルダに保存されます。「ファイル」アプリの
  **このiPhone内 > myDAW** から見えるので、パソコンとのやり取りもここで行えます。
  - プロジェクト: `myDAW` 直下（`<名前>.mydaw` と `<名前> Samples` フォルダ）
  - パターン: `Patterns`、書き出した WAV: `Exports`、取り込んだ音: `Imported Audio`
- **+ Sample** や **Open audio file...** では「ファイル」アプリの画面が開き、iCloud Drive などからも選べます。
  選んだ音はアプリの中にコピーされるので、元のファイルを消してもプロジェクトは壊れません。
  iPhone では WAV / MP3 / AIFF / FLAC / OGG に加えて **m4a（ボイスメモなど）/ AAC / CAF** も読み込めます。
- **Export WAV** の後は共有メニューが出るので、AirDrop で送ったり「ファイル」に保存したりできます。
- パソコンで作ったプロジェクトを使うときは、`.mydaw` と使っている音のファイルを同じフォルダ構成のまま
  「myDAW」フォルダへコピーして、Menu →「Open...」で開きます。見つからない音はチャンネルの **...** →
  「Replace sample...」で選び直せます。
- アプリを切り替えたとき、保存していない変更は自動で保存されます（名前が無いプロジェクトは `Autosave`）。
  次に開いたときは最後のプロジェクトから続けられます。

### iPhone / iPad での制限

- **VST3 は使えません**（iOS には VST3 がありません）。代わりに **AUv3** のプラグイン（App Store の
  Audio Unit 対応アプリ）を使えます。Menu →「Plug-in manager (AUv3)...」→ Options →「Scan for new or updated AudioUnit plug-ins」。
  パソコンで VST3 を使って作ったプロジェクトを開くと、VST3 シンセのチャンネルは「読み込めない」と表示され（設定は残ります）、
  VST3 エフェクトは外されます（そのまま iPhone で上書き保存すると、パソコンで開いてもそのエフェクトは戻りません。別名で保存するのがおすすめです）。
- **AI ステム分離**も動きますが、メモリの都合で 1 つずつ処理するので、パソコンより時間がかかります
  （まずは「Only the selected part」で短い範囲から試すのがおすすめです）。iOS は裏に回ったアプリを止めるので、
  分離中はアプリを前面に出したままにしてください。
- ファイルのドラッグ＆ドロップとキーボードショートカットは使えません（ボタンとメニューから同じ操作ができます）。

## ビルド方法

必要なもの: CMake 3.22 以上、C++17 コンパイラ（Windows: Visual Studio 2022 / macOS: Xcode / Linux: GCC または Clang）。
JUCE は CMake が自動でダウンロードします。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

実行ファイルは `build/MyDAW_artefacts/Release/` にできます。iPhone / iPad 用のビルドは [iPhone / iPad で使う](#iphone--ipad-で使う) を見てください。

スマホの画面サイズでの見え方は、パソコン版でも `myDAW --window-size 750x369` のように窓の大きさを指定して確認できます。

Linux では先に次のパッケージを入れてください:

```bash
sudo apt install build-essential cmake ninja-build libasound2-dev libjack-jackd2-dev libfreetype-dev \
  libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev
```

### テスト

```bash
cmake -S . -B build -DMYDAW_BUILD_TESTS=ON
cmake --build build --config Release
./build/MyDAWTests_artefacts/Release/MyDAWTests                        # エンジン・ファイル形式のテスト
./build/MyDAWTests_artefacts/Release/MyDAWTests Synth.vst3 Gain.vst3   # VST3 ホストのテストも実行
MYDAW_TEST_DEMUCS_MODEL=/path/to/ggml-model-htdemucs-6s-f16.bin ./build/MyDAWTests_artefacts/Release/MyDAWTests  # AI 分離のテスト
```

## ソースの構成

```
Source/
  Main.cpp             アプリ本体・コマンドライン書き出し
  AppCore.h            オーディオデバイス・プラグイン管理などの所有者
  Session.*            プロジェクトの操作（チャンネル / パターン / ソング / 保存・読み込み）
  Model/Project.*      データ構造（Note, Pattern, Clip, Track）
  Audio/AudioEngine.*  リアルタイム再生・シーケンサ・WAV 書き出し
  Audio/Channel.*      サンプラー / VST インストゥルメント + インサートエフェクト
  Audio/SampleEditing.* 波形編集（トリム・フェード・ノーマライズ・エフェクトの書き込み等）
  Audio/Slicing.*      自動スライス（アタック検出・等分・拍）と切り出し
  AI/StemSeparation.*  AI ステム分離（demucs.cpp の呼び出し、並列処理、モデルのダウンロード）
  Audio/EffectChain.*  エフェクトプラグインの直列処理
  Audio/PluginManager.* VST3（iOS では AUv3）のスキャン・読み込み
  UI/                  画面（TransportBar, PatternList, StepGrid, PianoRoll, PatternEditor, SongEditor, SampleEditor, Sampler ...）
  UI/Touch.*           タッチ操作（長押し = 右クリック、メニューを指の横に出す）
  UI/AppFiles.*        ファイルを開く / 保存（iOS はアプリの書類フォルダ・共有メニュー）
  UI/WindowPlacement.* 窓の配置（iOS は全画面・セーフエリア・スクロール表示）
Tests/IntegrationTests.cpp
ThirdParty/demucs.cpp/ Demucs v4 の C++ 推論ライブラリ（MIT、src のみ同梱）
```

## 現在の制限・今後のアイデア

- VST2 は非対応（Steinberg が SDK の配布を終了しているため）。VST3 と Audio Unit（macOS）、AUv3（iPhone / iPad）に対応。
- Windows の ASIO は未対応（WASAPI / DirectSound で動作）。ASIO SDK を入れれば有効化できます。
- Undo はサンプルエディタの中だけです（打ち込み・ソング編集の Undo はまだ）。MIDI キーボード入力 / 録音、オートメーション、オーディオ録音もまだありません。
- プラグインのスキャンはアプリ内で行うため、壊れたプラグインがあるとスキャン中に落ちることがあります
  （その場合は再起動すると、そのプラグインを飛ばしてスキャンを続けられます）。

## ライセンスについて

- AI ステム分離: [Demucs](https://github.com/facebookresearch/demucs)（Meta、MIT）、
  [demucs.cpp](https://github.com/sevagh/demucs.cpp)（MIT、`ThirdParty/demucs.cpp` に同梱）、[Eigen](https://eigen.tuxfamily.org)（MPL2）を使っています。
- 分離した音を作品に使うときは、元の曲の著作権に注意してください。

JUCE は AGPLv3 / 商用のデュアルライセンスです。個人で使う分には問題ありませんが、
このアプリを配布・販売する場合は JUCE のライセンス条件（AGPLv3 でソース公開、または商用ライセンス）に従ってください。
