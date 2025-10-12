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
send_interval_minutes: 0.25
```

- `serial_port`: 環境に応じて `/dev/ttyACM0` などへ変更してください。
- `send_interval_minutes`: ThingSpeak への送信間隔（分単位）。ThingSpeak の無料プランでは 0.25 分（15 秒）以上に設定してください。
