// Seeeduino Lotus Cortex-M0+ (SAMD21) 専用
// 水位(I2C 0x77/0x78)・温度(DS18B20 on D3)・照度(TSL2561 I2C)・騒音(アナログ A1)・LCD(I2C 0x3E/0x62)
// 未接続でも停止せず、NAを出力。1Hz更新。

#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2561_U.h>
#include <math.h>

// ---- 有効/無効スイッチ ----
#define USE_TEMP_SENSOR   true
#define USE_WATER_SENSOR  true
#define USE_LIGHT_SENSOR  true
#define USE_SOUND_SENSOR  true
#define USE_LCD_DISPLAY   true
#define OUTPUT_CSV        false   // false: ラベル付き / true: CSV

// ---- 電圧/ピン ----
#define ADC_VREF_MV       3300
#define ONE_WIRE_PIN      3
#define SOUND_PIN         A2

#define ATTINY_LOW_ADDR   0x77
#define ATTINY_HIGH_ADDR  0x78
#define I2C_READ_TIMEOUT_MS 50

// ---- シリアル（SAMDはSerialUSB）----
#define LOG SerialUSB

// ---- インスタンス ----
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature tempSensor(&oneWire);
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_LOW, 12345);

#if USE_LCD_DISPLAY
  #include "rgb_lcd.h"
  rgb_lcd lcd; // 0x3E(text) / 0x62(RGB)
#endif

// ---- 状態 ----
bool hasLCD = false;

// ---- 前方宣言 ----
bool  i2cReadWithTimeout(uint8_t addr, uint8_t* buf, int len);
int   readWaterPercent();
float readTemperatureC();
bool  soundPresent();
int   readSoundLevel();
long  readLux();
void  printLabeled(float tC, int waterPct, int soundLevel, long lux);
void  printCSV(float tC, int waterPct, int soundLevel, long lux);

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(1500);

  LOG.begin(115200);
  unsigned long t0 = millis();
  while (!LOG && (millis() - t0 < 5000)) {}

  Wire.begin();

#if USE_LIGHT_SENSOR
  if (tsl.begin()) {
    tsl.enableAutoRange(true);
    tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_402MS);
    LOG.println("TSL2561 initialized");
  } else {
    LOG.println("TSL2561 not detected");
  }
#endif

#if USE_TEMP_SENSOR
  tempSensor.begin(); // 失敗は返さない。値で判定
#endif

#if USE_LCD_DISPLAY
  delay(120);
  // 0x3E(テキスト)が見えれば初期化。0x62(RGB)は任意。
  Wire.beginTransmission(0x3E);
  bool textOK = (Wire.endTransmission() == 0);
  Wire.beginTransmission(0x62);
  bool rgbOK  = (Wire.endTransmission() == 0);

  if (textOK) {
    lcd.begin(16, 2);
    if (rgbOK) lcd.setRGB(0,120,255);
    lcd.clear();
    lcd.print("Sensor Ready");
    delay(500);
    lcd.clear();
    hasLCD = true;
  }
#endif

  LOG.println("=== Sensor Monitor ===");
#if OUTPUT_CSV
  LOG.println("temp_c,water_pct,sound_level,light_lux");
#endif
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  static bool hb = false;

  float temperature = USE_TEMP_SENSOR  ? readTemperatureC() : NAN;
  int   waterPct    = USE_WATER_SENSOR ? readWaterPercent() : -1;
  int   soundLevel  = USE_SOUND_SENSOR ? readSoundLevel()   : -1;
  long  lux         = USE_LIGHT_SENSOR ? readLux()          : -1;

#if OUTPUT_CSV
  printCSV(temperature, waterPct, soundLevel, lux);
#else
  printLabeled(temperature, waterPct, soundLevel, lux);
#endif

#if USE_LCD_DISPLAY
  if (hasLCD) {
    lcd.setCursor(0,0);
    lcd.print("T:");
    if (isnan(temperature)) lcd.print("--"); else lcd.print(temperature,1);
    lcd.print(" L:");
    if (lux < 0) lcd.print("--"); else lcd.print(lux);
    lcd.print("   ");

    lcd.setCursor(0,1);
    lcd.print("W:");
    if (waterPct < 0) lcd.print("--"); else lcd.print(waterPct);
    lcd.print("% S:");
    if (soundLevel < 0) lcd.print("--"); else lcd.print(soundLevel);
    lcd.print("   ");
  }
#endif

  hb = !hb; digitalWrite(LED_BUILTIN, hb);
  delay(1000);
}

// ===== 読み取り実装 =====
float readTemperatureC() {
  tempSensor.requestTemperatures();
  float t = tempSensor.getTempCByIndex(0);
  // DS18B20未接続時は -127.0 または DEVICE_DISCONNECTED_C を返す
  if (t == DEVICE_DISCONNECTED_C || t == -127.0f || t < -40.0f || t > 85.0f) {
    return NAN; // 未接続はNANで返す
  }
  return t;
}

int readWaterPercent() {
  uint8_t low[8]={0}, high[12]={0};
  if (!i2cReadWithTimeout(ATTINY_LOW_ADDR,  low,  8))  return -1;
  if (!i2cReadWithTimeout(ATTINY_HIGH_ADDR, high, 12)) return -1;
  int lc=0,hc=0;
  for (int i=0;i<8;i++)  if (low[i]  >250 && low[i]  <255) lc++;
  for (int i=0;i<12;i++) if (high[i] >250 && high[i] <255) hc++;
  int total = lc+hc; // 0..20
  int pct = (total*100)/20;
  if (pct<0) pct=0; if (pct>100) pct=100;
  return pct;
}

long readLux() {
  sensors_event_t event;
  tsl.getEvent(&event);
  
  if (event.light) {
    return (long)event.light;
  } else {
    return -1; // センサーエラーまたは暗すぎる場合
  }
}

// ―― 未接続判定つきの騒音読み取り ――
// 1) 短時間だけ INPUT_PULLDOWN にして2回読む → ほぼ0なら未接続
// 2) 接続ありなら INPUT に戻して多点サンプリング→0-100%に変換
bool soundPresent() {
  pinMode(SOUND_PIN, INPUT_PULLDOWN);
  delay(3);
  int r1 = analogRead(SOUND_PIN);
  delay(2);
  int r2 = analogRead(SOUND_PIN);
  pinMode(SOUND_PIN, INPUT); // 元に戻す
  // プルダウンで2回とも極小 → 未接続とみなす（しきい値は経験的に20程度）
  return (max(r1, r2) > 20);
}

int readSoundLevel() {
  if (!soundPresent()) return -1;
  
  // 接続あり：多点平均でノイズ低減
  const int N=16;
  long sum=0;
  for (int i=0;i<N;i++){ sum += analogRead(SOUND_PIN); delay(2); }
  int raw = (int)(sum/N);
  
  // 0-1023を0-100にマッピング
  int level = map(raw, 0, 1023, 0, 100);
  if (level < 0) level = 0;
  if (level > 100) level = 100;
  return level;
}

// ===== 共通ユーティリティ =====
bool i2cReadWithTimeout(uint8_t addr, uint8_t* buf, int len) {
  memset(buf, 0, len);
  Wire.requestFrom((int)addr, (int)len);
  unsigned long st = millis();
  while (Wire.available() < len) {
    if (millis() - st > I2C_READ_TIMEOUT_MS) {
      while (Wire.available()) (void)Wire.read();
      return false;
    }
  }
  for (int i=0;i<len;i++) buf[i]=Wire.read();
  delay(5);
  return true;
}

// ===== 出力 =====
void printLabeled(float tC, int waterPct, int soundLevel, long lux) {
  LOG.print("Temp: ");
  if (isnan(tC)) LOG.print("NA"); else { LOG.print(tC,1); LOG.print("C"); }
  LOG.print(" | Water: ");
  if (waterPct < 0) LOG.print("NA"); else { LOG.print(waterPct); LOG.print("%"); }
  LOG.print(" | Sound: ");
  if (soundLevel < 0) LOG.print("NA"); else { LOG.print(soundLevel); LOG.print("%"); }
  LOG.print(" | Light: ");
  if (lux < 0) LOG.print("NA"); else { LOG.print(lux); LOG.print(" lux"); }
  LOG.println();
}

void printCSV(float tC, int waterPct, int soundLevel, long lux) {
  if (isnan(tC)) LOG.print("NA"); else LOG.print(tC,2);
  LOG.print(",");
  if (waterPct < 0) LOG.print("NA"); else LOG.print(waterPct);
  LOG.print(",");
  if (soundLevel < 0) LOG.print("NA"); else LOG.print(soundLevel);
  LOG.print(",");
  if (lux < 0) LOG.print("NA"); else LOG.print(lux);
  LOG.println();
}