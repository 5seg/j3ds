# Jellyfin 3DS (j3ds)

Nintendo 3DS / 2DS / New3DS 向けの Jellyfin 音楽クライアントです。
libctru / devkitARM を使って構築し、Jellyfin サーバーからライブラリを
ブラウジングして曲をダウンロード・再生できます。

## 機能一覧

- Jellyfin サーバー接続（HTTP / HTTPS）
- ユーザー名 / パスワード 認証
- API Key 認証
- ライブラリ → アーティスト → アルバム → 曲 のブラウジング
- 曲のダウンロードと再生（MP3）
- サムネイル表示（上画面）
- 自己署名証明書対応（SSL 検証無効化オプション）
- 再生中のスリープ阻止
- 下画面のGUIカード／リスト操作とタッチ操作

## 環境要件

- 実行機器: 3DS / 2DS / New3DS
- 実行環境: CFW 環境（Homebrew Launcher からの起動、または .cia インストール）

### 開発環境

ローカル開発では incus コンテナ `j3ds-dev` を使用できます。
構築手順は `SETUP.md` を参照してください。

### CI / リリース

GitHub Actions では、公式の `devkitpro/devkitarm:latest` Docker イメージ上で
Ubuntu ランナーがビルドを行います。
`.github/workflows/build.yml` を参照してください。

- `main` / `master` ブランチへのプッシュ、および `v*` タグへのプッシュでビルド
- `v*` タグがプッシュされると、GitHub Release が自動生成され `.3dsx`、`.cia`、
  および `.cia` ダウンロード用 QR コードが添付されます

## ディレクトリ構成

```
source/        アプリケーションソース
  main.c       エントリポイント
  app.c/h      状態管理と画面遷移
  ui/          画面、ブラウザ、プレイヤー、ソフトキーボード、サムネイル
  net/         HTTP クライアント、Jellyfin API クライアント
  audio/       オーディオプレイヤー、MP3 デコーダー
  sys/         スリープ制御、SD パス補助
  storage/     設定 JSON の読み書き
  utils/       jansson 補助、JPEG 描画
meta/          icon、banner、audio、RSF など .cia 用メタデータ
romfs/         CIA 用 RomFS アセット
Makefile       devkitARM アプリケーション Makefile
build.sh       .3dsx + .cia 同時ビルド（incus コンテナ経由）
build-cia.sh   .cia のみビルド（incus コンテナ経由）
```

## ビルド方法

### ローカル開発（incus コンテナ）

`j3ds-dev` コンテナ内でビルドします。

```bash
./build.sh    # .3dsx と .cia を同時にビルド
./build-cia.sh  # .cia のみビルド
```

成功すると、カレントディレクトリに `j3ds.3dsx` と `j3ds.cia` が生成されます。

### GitHub Actions

- すべてのプッシュ / PR 時にビルドが走ります
- `v*` タグをプッシュすると、自動で GitHub Release が作成されます
- Release には `.cia` ダウンロード用の QR コードが含まれます

直接 Docker イメージを使う場合は次のようにビルドできます
（bannertool / makerom はソースからビルドする必要があります）。

```bash
docker run --rm -v "$PWD:/workspace/j3ds" devkitpro/devkitarm:latest bash -lc '
  git clone --depth 1 https://github.com/carstene1ns/3ds-bannertool.git /tmp/bannertool
  cd /tmp/bannertool && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
  install -Dm755 build/bannertool /usr/local/bin/bannertool
  git clone --depth 1 https://github.com/3DSGuy/Project_CTR.git /tmp/makerom
  cd /tmp/makerom/makerom && make deps && make program
  install -Dm755 bin/makerom /usr/local/bin/makerom
  cd /workspace/j3ds && make -j$(nproc)
'
```

### 参考: ビルド成果物のサイズ

- `j3ds.3dsx` : 約 733 KB（750,204 bytes）
- `j3ds.cia`  : 約 589 KB（603,072 bytes）

## インストール方法

### .3dsx（Homebrew Launcher）

生成された `j3ds.3dsx` を SD カードの `/3ds/j3ds.3dsx` へコピーし、
Homebrew Launcher から起動します。

### .cia（HOME メニュー）

生成された `j3ds.cia` を FBI や godmode9 などでインストールし、
HOME メニューから起動します。

## 操作方法

### Home 画面

- **A**: ブラウザ画面を開く
- **SELECT**: 設定画面を開く
- **START**: 終了
- **B**: プレイヤー画面を開く
- 下画面の **LIBRARY / SETUP** カードをタッチして移動

### 設定画面

- **↑ / ↓**: 項目選択
- **A**: 選択項目を編集
- **X**: 接続テスト
- **START**: 設定を保存
- **B**: 戻る
- 各設定行をタッチして編集

Username と Password が設定されている場合、ライブラリを開くと
Jellyfin のユーザー認証を行い、取得したセッショントークンを使います。
既存の `apiKey` は設定ファイルに保持され、Password が未設定の場合の
後方互換ログインとして利用されます。

### ブラウザ画面

- **↑ / ↓**: 選択移動
- **A**: 下位階層を開く / 曲を再生
- **B**: 戻る
- **L / R**: ページ移動
- **X**: 選択中の曲をダウンロードのみ

### プレイヤー画面

- **X**: 再生
- **Y**: ポーズ / 再開
- **B**: 停止して戻る

## 3DS 上でのデータ配置

- 設定ファイル: `/3ds/j3ds/config.json`
- サムネイルキャッシュ: `/3ds/j3ds/cache/thumbs/`
- ダウンロードした曲: `/3ds/j3ds/cache/audio/`

## 注意事項

- 本アプリは音楽再生のみを対象としています（動画は非対応）。
- サーバー側で MP3 フォーマットにトランスコードできる設定を推奨します。
- サムネイルや曲のダウンロードには Wi-Fi 接続が必要です。
- 自己署名証明書やローカル HTTPS を使う場合は、設定の「Disable SSL verify」を有効にしてください。
- 実機での動作確認はユーザー側で実施してください。

## 既知の問題

- Vorbis / Ogg 再生は実装されていません（MP3 推奨）。
- 大きなライブラリは表示に時間がかかる場合があります。
