# myDAW

パターン（小節のブロック）を作って、パズルのように並べて曲を作るデスクトップ DAW です。
C++ / [JUCE](https://juce.com) 製で、Windows・macOS・Linux で動きます。

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
`myDAW-Windows`（`myDAW.exe`）/ `myDAW-macOS`（`myDAW.app`）/ `myDAW-Linux` をダウンロードできます。

- macOS: 署名していないアプリなので、初回は右クリック →「開く」で起動してください。
- Windows: SmartScreen が出たら「詳細情報」→「実行」。

## ビルド方法

必要なもの: CMake 3.22 以上、C++17 コンパイラ（Windows: Visual Studio 2022 / macOS: Xcode / Linux: GCC または Clang）。
JUCE は CMake が自動でダウンロードします。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

実行ファイルは `build/MyDAW_artefacts/Release/` にできます。

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
  Audio/PluginManager.* VST3 のスキャン・読み込み
  UI/                  画面（TransportBar, PatternList, StepGrid, PianoRoll, PatternEditor, SongEditor, SampleEditor, Sampler ...）
Tests/IntegrationTests.cpp
ThirdParty/demucs.cpp/ Demucs v4 の C++ 推論ライブラリ（MIT、src のみ同梱）
```

## 現在の制限・今後のアイデア

- VST2 は非対応（Steinberg が SDK の配布を終了しているため）。VST3 と Audio Unit（macOS）に対応。
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
