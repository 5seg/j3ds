# j3ds-dev 環境構築手順

## 概要

このドキュメントは **ローカル開発用** の incus コンテナ `j3ds-dev` の構築手順です。

GitHub Actions（CI / リリース）では、公式の `devkitpro/devkitarm:latest` Docker イメージを
使用します。`.github/workflows/build.yml` を参照してください。

incus コンテナ `j3ds-dev` 内に devkitPro 3DS 開発環境を構築した。

- ホスト: Arch Linux x86_64
- コンテナ: `images:archlinux/current/amd64` (Arch Linux rolling)
- マウント:
  - `/home/master/workspace/j3ds` → `/workspace/j3ds`
  - `/home/master/cloned/3ds-examples` → `/workspace/3ds-examples`

## コンテナ作成コマンド

```bash
incus launch images:archlinux/current/amd64 j3ds-dev --profile default
incus config device add j3ds-dev j3ds-workspace disk source=/home/master/workspace/j3ds path=/workspace/j3ds
incus config device add j3ds-dev 3ds-examples disk source=/home/master/cloned/3ds-examples path=/workspace/3ds-examples
```

## コンテナへの入り方

```bash
incus exec j3ds-dev -- bash
```

ログイン後、devkitPro 環境変数を読み込む場合は以下を実行する。

```bash
source /etc/profile.d/devkit-env.sh
source /etc/profile.d/ctrulib.sh
```

## コンテナ内での devkitPro インストール手順

### 1. 基本ツールをインストール

```bash
pacman -Syu --noconfirm base-devel wget git make sudo
```

### 2. devkitPro GPG キーを追加

```bash
pacman-key --init
pacman-key --recv BC26F752D25B92CE272E0F44F7FD5492264BB9D0 --keyserver keyserver.ubuntu.com
pacman-key --lsign BC26F752D25B92CE272E0F44F7FD5492264BB9D0
```

### 3. devkitPro keyring をインストール

```bash
cd /tmp
curl -L -A 'Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0' \
  -o devkitpro-keyring.pkg.tar.zst https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst
pacman -U --noconfirm devkitpro-keyring.pkg.tar.zst
```

### 4. pacman.conf にリポジトリを追加

`/etc/pacman.conf` の `XferCommand` を、User-Agent を付けた curl に変更する。

```text
XferCommand = /usr/bin/curl -L -C - -f -A "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0" -o %o %u
```

末尾に以下を追加する。

```text
[dkp-libs]
Server = https://pkg.devkitpro.org/packages

[dkp-linux]
Server = https://pkg.devkitpro.org/packages/linux/$arch/
```

> `pkg.devkitpro.org` への素の curl リクエストが Cloudflare ブロックされるため、User-Agent 偽装が必要。

### 5. 3DS パッケージをインストール

```bash
pacman -Syu --noconfirm
pacman -S --noconfirm --needed \
  3ds-dev \
  3ds-jansson \
  3ds-libjpeg-turbo \
  3ds-libpng \
  3ds-mpg123 \
  3ds-libvorbisidec \
  3ds-opusfile
```

注意: 要求にあった `3ds-libopusfile` ではなく、`3ds-opusfile` というパッケージ名が正しい。

### 6. その他に必要なツール

```bash
pacman -S --noconfirm --needed imagemagick
```

`graphics/bitmap/24bit-color` のビルドに ImageMagick が必要だった。

## インストールしたパッケージ一覧

```text
3ds-cmake 1.5.2-1
3ds-examples 20240917-1
3ds-jansson 2.13-1
3ds-libjpeg-turbo 3.1.3-1
3ds-libogg 1.3.6-1
3ds-libopus 1.4-1
3ds-libpng 1.6.53-1
3ds-libvorbisidec 1.2.1-3
3ds-mpg123 1.31.3-3
3ds-opusfile 0.11-1
3ds-pkg-config 0.28-5
3ds-zlib 1.3.1-1
3dslink 0.6.3-1
3dstools 1.3.1-3
citro2d 1.7.0-1
citro3d 1.7.1-2
devkit-env 1.0.1-2
devkitARM r68-1
devkitARM-gdb 14.1-2
devkitarm-binutils 2.46.0-1
devkitarm-cmake 1.2.4-1
devkitarm-crtls 1.2.6-1
devkitarm-gcc 16.1.0-1
devkitarm-newlib 4.6.0.20260123-5
devkitarm-rules 1.6.0-4
devkitpro-keyring 20241017-2
dkp-cmake-common-utils 1.5.3-1
general-tools 1.4.4-1
imagemagick 7.1.2.29-1
libctru 2.7.0-1
picasso 2.7.2-3
tex3ds 2.3.0-4
```

## 確認できた example 一覧

`/workspace/3ds-examples` はマウントされた読み取り専用ディレクトリとして扱われるため、
コンテナ内にコピーしてから `make` した。

```bash
source /etc/profile.d/devkit-env.sh
source /etc/profile.d/ctrulib.sh
cp -r /workspace/3ds-examples /root/3ds-examples
```

| example | 結果 |
| --- | --- |
| `/root/3ds-examples/audio/ogg-vorbis-decoding` | 成功 (ogg-vorbis-decoding.3dsx 生成) |
| `/root/3ds-examples/network/http` | 成功 (http.3dsx 生成) |
| `/root/3ds-examples/graphics/bitmap/24bit-color` | 成功 (24bit-color.3dsx 生成) |
| `/root/3ds-examples/templates/application` | 成功 (application.3dsx 生成) |

エラーは特になし。`graphics/bitmap/24bit-color` だけ ImageMagick 不足で一度失敗したが、
インストール後に再試行して成功した。

## 環境変数の値

コンテナ内で `/etc/profile.d/devkit-env.sh` により設定される。

```text
DEVKITPRO=/opt/devkitpro
DEVKITARM=/opt/devkitpro/devkitARM
DEVKITPPC=/opt/devkitpro/devkitPPC
PATH=${DEVKITPRO}/tools/bin:${PATH}
```

> 注意: `PATH` には `DEVKITARM/bin` は含まれていない。`arm-none-eabi-gcc` などは `${DEVKITARM}/bin/arm-none-eabi-gcc` に存在し、3DS の Makefile 側が `DEVKITARM` 変数を参照して直接呼び出す。

`/etc/profile.d/ctrulib.sh` で追加。

```text
CTRULIB=/opt/devkitpro/libctru
```

## bannertool / makerom のパス

### ソース

- `bannertool`: https://github.com/carstene1ns/3ds-bannertool/releases/tag/1.2.3
  - `bannertool-1.2.3-linux.tar.gz`
- `makerom`: https://github.com/3DSGuy/Project_CTR/releases/tag/makerom-v0.19.0
  - `makerom-v0.19.0-ubuntu_x86_64.zip`

### 配置先

- ホスト上: `/home/master/workspace/j3ds/tools/bannertool`
- ホスト上: `/home/master/workspace/j3ds/tools/makerom`
- コンテナ内: `/workspace/j3ds/tools/bannertool`
- コンテナ内: `/workspace/j3ds/tools/makerom`

さらに PATH の通った `/opt/devkitpro/tools/bin/` にもコピーしてある。

```bash
which bannertool   # /opt/devkitpro/tools/bin/bannertool
which makerom      # /opt/devkitpro/tools/bin/makerom
```

## 備考

- コンテナ内では `/workspace/j3ds` や `/workspace/3ds-examples` の所有者が `nobody` として見えるため、直接書き込むことはできない。作業用コピーを `/root/3ds-examples` などに作成する運用とする。
- `pacman` 経由で `pkg.devkitpro.org` へアクセスする際は Cloudflare ブロックを回避するため、`/etc/pacman.conf` の `XferCommand` にブラウザ User-Agent を指定している。
