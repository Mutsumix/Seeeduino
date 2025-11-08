#!/usr/bin/env python3
import re
import time
import serial
import requests
import yaml
from logging import getLogger, basicConfig, INFO
from pathlib import Path

logger = getLogger(__name__)
basicConfig(level=INFO, format='%(asctime)s - %(levelname)s - %(message)s')

CONFIG_PATH = Path(__file__).resolve().parent / "config.yml"
THINGSPEAK_URL = "https://api.thingspeak.com/update"

DEFAULT_CONFIG = {
    "thingspeak_api_key": None,
    "serial_port": "/dev/ttyACM0",
    "baud_rate": 115200,
    "send_interval_minutes": 0.25,
}


def load_config(path: Path) -> dict:
    if not path.exists():
        raise FileNotFoundError(f"config.yml が見つかりません: {path}")

    with path.open("r", encoding="utf-8") as f:
        config = yaml.safe_load(f) or {}

    merged = {**DEFAULT_CONFIG, **config}

    try:
        interval_minutes = float(merged.get("send_interval_minutes", 0))
    except (TypeError, ValueError):
        raise ValueError("send_interval_minutes は数値で指定してください。")

    if interval_minutes <= 0:
        raise ValueError("send_interval_minutes は0より大きい値にしてください。")

    merged["send_interval_minutes"] = interval_minutes

    if not merged.get("thingspeak_api_key"):
        raise ValueError("config.yml に thingspeak_api_key を設定してください。")

    return merged


def parse_sensor_data(line: str) -> dict:
    """
    現在の出力形式に対応:
    Temp: 25.0C | Water: 0% | Sound: 45% | Light: 123 lux
    NAが来た場合はNoneを設定
    """
    data = {}

    # 温度: "Temp: 25.0C" または "Temp: NA"
    temp_match = re.search(r"Temp: ([\d.]+)C", line)
    if temp_match:
        data["water_temp"] = float(temp_match.group(1))
    elif "Temp: NA" in line:
        data["water_temp"] = None

    # 水位: "Water: 50%" または "Water: NA"
    water_match = re.search(r"Water: (\d+)%", line)
    if water_match:
        data["water_level"] = int(water_match.group(1))
    elif "Water: NA" in line:
        data["water_level"] = None

    # 音量: "Sound: 45%" または "Sound: NA"
    sound_match = re.search(r"Sound: (\d+)%", line)
    if sound_match:
        data["sound"] = int(sound_match.group(1))
    elif "Sound: NA" in line:
        data["sound"] = None

    # 照度: "Light: 123 lux" または "Light: NA"
    light_match = re.search(r"Light: (\d+) lux", line)
    if light_match:
        data["light"] = int(light_match.group(1))
    elif "Light: NA" in line:
        data["light"] = None

    return data


def send_data_to_thingspeak(api_key: str, data: dict) -> bool:
    """
    ThingSpeakに送信。Noneの値は送らない
    Field1: water-temp
    Field2: water-level
    Field3: light
    Field4: sound
    """
    payload = {"api_key": api_key}
    
    # Noneでない値だけを追加
    if data.get("water_temp") is not None:
        payload["field1"] = data["water_temp"]
    if data.get("water_level") is not None:
        payload["field2"] = data["water_level"]
    if data.get("light") is not None:
        payload["field3"] = data["light"]
    if data.get("sound") is not None:
        payload["field4"] = data["sound"]

    try:
        response = requests.get(THINGSPEAK_URL, params=payload, timeout=10)
        response.raise_for_status()

        logger.info(
            "ThingSpeakへ送信: Temp=%s°C | Water=%s%% | Light=%s lux | Sound=%s%%",
            data.get("water_temp", "NA"),
            data.get("water_level", "NA"),
            data.get("light", "NA"),
            data.get("sound", "NA"),
        )
        return True

    except requests.exceptions.RequestException as exc:
        logger.error("ThingSpeak送信失敗: %s", exc)
        return False


def main():
    config = load_config(CONFIG_PATH)

    send_interval_minutes = config["send_interval_minutes"]
    send_interval_seconds = send_interval_minutes * 60

    logger.info("=" * 60)
    logger.info("センサーデータ → ThingSpeak 送信プログラム")
    logger.info("チャンネル: Seeeduino Sensor Data")
    logger.info("送信間隔: %.2f分 (%.0f秒)", send_interval_minutes, send_interval_seconds)
    logger.info("=" * 60)

    port = config["serial_port"]
    baud_rate = config["baud_rate"]

    try:
        ser = serial.Serial(port, baud_rate, timeout=1)
        time.sleep(2)  # Arduinoのリセット待ち
        logger.info("シリアルポート接続: %s", port)
    except serial.SerialException as exc:
        logger.error("シリアルポートエラー: %s", exc)
        logger.error("ls /dev/tty* で接続先を確認してください。")
        return

    api_key = config["thingspeak_api_key"]
    last_send = 0.0
    latest_data = {}

    try:
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode("utf-8", errors="ignore").strip()

                if not line or "===" in line:
                    continue

                data = parse_sensor_data(line)
                if data:
                    latest_data = data
                    logger.debug("受信: %s", line)

            current_time = time.time()
            if latest_data and (current_time - last_send >= send_interval_seconds):
                # データが1つでもあれば送信（NAでも可）
                if latest_data:
                    if send_data_to_thingspeak(api_key, latest_data):
                        last_send = current_time
                    else:
                        logger.warning("送信失敗: 次回リトライします")

            time.sleep(0.5)

    except KeyboardInterrupt:
        logger.info("終了します")
    finally:
        if ser.is_open:
            ser.close()
            logger.info("シリアルポート切断")


if __name__ == "__main__":
    main()