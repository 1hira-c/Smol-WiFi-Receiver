# Smol Wi-Fi Receiver

[English](README.md) | 日本語

今回のSmol Slimeトラッカーは、`SlimeVR-Tracker-nRF`をフォークして
ブロードキャスト／ダイバーシティ対応を追加した独自改造版FW
（[`1hira-c/SlimeVR-Tracker-nRF`](https://github.com/1hira-c/SlimeVR-Tracker-nRF/tree/codex/xiao-nrf54l15-diversity)）
で動作します。本リポジトリは、その独自Tracker FWと組み合わせるレシーバー側の
ファームウェアです。レシーバーはSeeed Studio XIAO nRF54L15とXIAO ESP32-C5の
2マイコンで構成します。

- nRF54L15は、このフォークが2.4 GHz帯でブロードキャストする28バイトRF v2
  パケットを受信します。Tracker側の形式は変更せず、そのまま入力として扱います。
- ESP32-C5は受信レポートを5 GHz Wi-Fi経由でホストBridgeへ転送します。
- 2つのMCUはREADYハンドシェイク付きの2 MHz SPIリンクで通信します。
- ホストBridgeはWi-FiとUSB HIDのレシーバーを統合してRF重複を除去し、標準UDP
  トラッカーパケットとしてSlimeVR Serverへ転送します。

本リポジトリ内のデバイスFWはWi-Fiレシーバーを対象とします。ホストBridgeは、
従来のレシーバーリポジトリで管理するnRF52840 USB HIDレシーバーにも対応します。

## ディレクトリ構成

- `firmware/nrf54l15`: nRF Connect SDK v3.3.1用レシーバーアプリケーション
- `firmware/esp32c5`: ESP-IDF v6.0.2用ゲートウェイアプリケーション
- `protocol`: 両MCUで共有するC言語のワイヤ形式とプロトコル仕様
- `bridge`: Wi-Fi/HIDダイバーシティ受信用Rustホストアプリケーション
- `docs`: 試作配線、設定、書き込み、検証に関する文書
- `tests/host`: ホスト環境で実行できるプロトコルテスト

## クイックスタート

最初に[試作配線](docs/prototype-wiring.md)を確認し、
[ビルド・書き込み手順](docs/build-and-flash.md)に従って両方のファームウェアを
ビルドして書き込んでください。通常動作時、ゲートウェイは5 GHz Wi-Fiのみを
使用します。Wi-Fiが未設定の場合、または接続に30秒間失敗した場合は、2.4 GHzで
`SmolReceiver-Setup-XXXX`という設定用APを開きます。初期WPA2パスワードは
`smol-wifi-setup`です。

ホストアプリは[Bridge README](bridge/README.md)の手順で起動します。
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
