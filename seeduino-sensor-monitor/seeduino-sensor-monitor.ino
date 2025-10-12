#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Digital_Light_TSL2561.h>
#include "DFRobot_EC10.h"

#define ONE_WIRE_BUS 2
#define EC_PIN A2
#define LOUDNESS_SENSOR A6
#define ATTINY1_HIGH_ADDR 0x78
#define ATTINY2_LOW_ADDR 0x77

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);
DFRobot_EC10 ec;

unsigned char low_data[8] = {0};
unsigned char high_data[12] = {0};

void setup() {
  Serial.begin(115200);
  Wire.begin();
  tempSensor.begin();
  TSL2561.init();
  ec.begin();
  
  Serial.println("=== Sensor Monitor ===");
}

void loop() {
  static float lastValidTemp = 25.0;
  
  // 温度
  tempSensor.requestTemperatures();
  float temperature = tempSensor.getTempCByIndex(0);
  if(temperature < -50 || temperature > 80) {
    temperature = lastValidTemp;
  } else {
    lastValidTemp = temperature;
  }
  
  // 水位（%）
  int waterPercent = getWaterLevelPercent();
  
  // EC
  int rawEC = analogRead(EC_PIN);
  float voltage = rawEC * 3300.0 / 1024.0;
  float ecValue = ec.readEC(voltage, temperature);
  float ecCalibratedUS = ecValue * 1000 * 0.229;
  
  // 音量（dB変換）
  int loudnessRaw = analogRead(LOUDNESS_SENSOR);
  float loudness_db = convertToDB(loudnessRaw);
  
  // 光量
  unsigned long lux = TSL2561.readVisibleLux();
  
  // ラベル付き出力
  Serial.print("Temp: "); Serial.print(temperature, 1); Serial.print("C | ");
  Serial.print("Water: "); Serial.print(waterPercent); Serial.print("% | ");
  Serial.print("EC: "); Serial.print(ecCalibratedUS, 0); Serial.print(" uS/cm | ");
  Serial.print("Sound: "); Serial.print(loudness_db, 1); Serial.print(" dB | ");
  Serial.print("Light: "); Serial.print(lux); Serial.println(" lux");
  
  delay(1000);
}

float convertToDB(int raw) {
  // dB変換
  if(raw <= 1) return 0.0;
  
  float voltage = raw * (5.0 / 1023.0);
  float db = 16.801 * log10(voltage * 10) + 9.872;
  
  if(db < 30) db = 30;
  if(db > 90) db = 90;
  
  return db;
}

void getHigh12SectionValue(void) {
  memset(high_data, 0, sizeof(high_data));
  Wire.requestFrom(ATTINY1_HIGH_ADDR, 12);
  while (12 != Wire.available());
  for (int i = 0; i < 12; i++) {
    high_data[i] = Wire.read();
  }
  delay(10);
}

void getLow8SectionValue(void) {
  memset(low_data, 0, sizeof(low_data));
  Wire.requestFrom(ATTINY2_LOW_ADDR, 8);
  while (8 != Wire.available());
  for (int i = 0; i < 8; i++) {
    low_data[i] = Wire.read();
  }
  delay(10);
}

int getWaterLevelPercent(void) {
  int low_count = 0;
  int high_count = 0;
  
  getLow8SectionValue();
  getHigh12SectionValue();
  
  for (int i = 0; i < 8; i++) {
    if (low_data[i] > 250 && low_data[i] < 255) low_count++;
  }
  for (int i = 0; i < 12; i++) {
    if (high_data[i] > 250 && high_data[i] < 255) high_count++;
  }
  
  int total = low_count + high_count;
  return (total * 100) / 20;
}

