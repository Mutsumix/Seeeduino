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
    "serial_port": "/dev/ttyUSB0",
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
    data = {}

    temp_match = re.search(r"Temp: ([\d.]+)C", line)
    if temp_match:
        data["water_temp"] = float(temp_match.group(1))

    ec_match = re.search(r"EC: ([\d.]+) uS/cm", line)
    if ec_match:
        data["ec"] = float(ec_match.group(1))

    water_match = re.search(r"Water: (\d+)%", line)
    if water_match:
        data["water_level"] = int(water_match.group(1))

    sound_match = re.search(r"Sound: ([\d.]+) dB", line)
    if sound_match:
        data["sound"] = float(sound_match.group(1))

    light_match = re.search(r"Light: (\d+) lux", line)
    if light_match:
        data["light"] = int(light_match.group(1))

    return data


def send_data_to_thingspeak(api_key: str, data: dict) -> bool:
    payload = {
        "api_key": api_key,
        "field1": data.get("water_temp"),
        "field2": data.get("ec"),
        "field3": data.get("water_level"),
        "field4": data.get("sound"),
        "field5": data.get("light"),
    }

    try:
        response = requests.get(THINGSPEAK_URL, params=payload, timeout=10)
        response.raise_for_status()

        logger.info(
            "ThingSpeakへ送信: Temp=%s°C | EC=%s uS/cm | Water=%s%% | Sound=%s dB | Light=%s lux",
            data.get("water_temp"),
            data.get("ec"),
            data.get("water_level"),
            data.get("sound"),
            data.get("light"),
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
                required_fields = [
                    "water_temp",
                    "ec",
                    "water_level",
                    "sound",
                    "light",
                ]

                if all(field in latest_data for field in required_fields):
                    if send_data_to_thingspeak(api_key, latest_data):
                        last_send = current_time
                    else:
                        logger.warning("送信失敗: 次回リトライします")
                else:
                    logger.warning("データ不完全: %s", latest_data)

            time.sleep(0.5)

    except KeyboardInterrupt:
        logger.info("終了します")
    finally:
        if ser.is_open:
            ser.close()
            logger.info("シリアルポート切断")


if __name__ == "__main__":
    main()
