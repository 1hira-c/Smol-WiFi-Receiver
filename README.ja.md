# Smol Wi-Fi Receiver

[English](README.md) | 日本語

Seeed Studio XIAO nRF54L15とXIAO ESP32-C5で構成する、
Smol Slime（SlimeVR Tracker nRF）トラッカー群向けの2マイコン
Wi-Fiレシーバーファームウェアです。

- nRF54L15はSmol Slimeトラッカーが2.4 GHz帯で送信する既存のRF v2
  パケットを受信します。Tracker側のRF形式は変更しません。
- ESP32-C5は受信レポートを5 GHz Wi-Fi経由でSlimeVR Serverへ転送します。
- 2つのMCUはREADYハンドシェイク付きの2 MHz SPIリンクで通信します。

本リポジトリはWi-Fiレシーバーのみを対象としています。既存のnRF52840 USB HID
レシーバーは、従来のレシーバーリポジトリで引き続き管理します。

## ディレクトリ構成

- `firmware/nrf54l15`: nRF Connect SDK v3.3.1用レシーバーアプリケーション
- `firmware/esp32c5`: ESP-IDF v6.0.2用ゲートウェイアプリケーション
- `protocol`: 両MCUで共有するC言語のワイヤ形式とプロトコル仕様
- `docs`: 試作配線、設定、書き込み、検証に関する文書
- `tests/host`: ホスト環境で実行できるプロトコルテスト

## クイックスタート

最初に[試作配線](docs/prototype-wiring.md)を確認し、
[ビルド・書き込み手順](docs/build-and-flash.md)に従って両方のファームウェアを
ビルドして書き込んでください。通常動作時、ゲートウェイは5 GHz Wi-Fiのみを
使用します。Wi-Fiが未設定の場合、または接続に30秒間失敗した場合は、2.4 GHzで
`SmolReceiver-Setup-XXXX`という設定用APを開きます。初期WPA2パスワードは
`smol-wifi-setup`です。

その他の資料として、[アーキテクチャ](docs/architecture.md)、
[ワイヤプロトコル](protocol/PROTOCOL.md)、[Web API](docs/web-api.md)、
[検証チェックリスト](docs/validation.md)があります。現在の実機検証状況と作業再開手順は
[ハードウェア立ち上げ状況](docs/current-bring-up.md)に記録しています。
言語間で共通の正規テストデータは`protocol/golden_vectors.json`に、対応する
C言語のテスト初期値は`protocol/golden_vectors.h`に格納しています。

## ライセンス

特に記載がない限り、本リポジトリはMIT LicenseまたはApache License 2.0の
いずれかを選択できるデュアルライセンスです。SlimeVRのnRFレシーバーから移植した
コードについては、元の著作権表示を維持しています。
