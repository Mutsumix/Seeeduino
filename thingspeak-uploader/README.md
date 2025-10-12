# ThingSpeak Uploader

Arduino で取得したセンサーデータをシリアル経由で受信し、ThingSpeak チャンネルへ送信する Python スクリプトです。

## セットアップ

1. `config.yml` に ThingSpeak の API キーとシリアル接続設定を記入します。
2. 必要ライブラリをインストールします。
```bash
pip install pyserial pyyaml requests
```
3. スクリプトを実行します。
```bash
python uploader.py
```

## 設定

`config.yml` の例:

```yaml
thingspeak_api_key: "YOUR_THINGSPEAK_API_KEY"
serial_port: "/dev/ttyUSB0"
baud_rate: 115200
```

`serial_port` は環境に応じて `/dev/ttyACM0` などへ変更してください。
