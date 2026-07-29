#include <WiFi.h>
#include <MHZ19.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

// credentials.ini で定義することを想定。定義されていない場合のフォールバック。
#ifndef DISPLAY_URL
#define DISPLAY_URL "http://192.168.0.100:8000/data"
#endif

#ifndef DEVICE_ID
#define DEVICE_ID "co2-monitor-01"
#endif

const long SENSOR_BAUDRATE = 9600;
const long DEBUG_BAUDRATE = 115200;
const int RX_PIN = 21;  // D0 (MH-Z19B TX)
const int TX_PIN = 2;   // D6 (MH-Z19B RX)
const int I2C_SDA_PIN = 6;  // D4 (BME280 SDA)
const int I2C_SCL_PIN = 7;  // D5 (BME280 SCL)
const uint8_t BME280_I2C_ADDR = 0x77; // SDO未接続(プルアップでHIGH) → 0x77
const int SERIAL_CHANNEL = 1;
const unsigned long SENSOR_UPDATE_INTERVAL = 5000; // 5秒間
const uint32_t WDT_TIMEOUT_S = 30; // 30秒
const unsigned long WIFI_RECONNECT_TIMEOUT_MS = 600000; // 10分間
const unsigned long DISPLAY_INTERVAL_MS = 30000; // 30秒間
const unsigned long RESTART_DELAY_MS = 1000;
const int CO2_MIN_VALID = 1;
const int CO2_MAX_VALID = 10000;

// BME280 測定値の妥当性範囲（仕様: -40〜85°C, 870〜1084 hPa, 0〜100%）
const float BME_TEMP_MIN  = -40.0F;
const float BME_TEMP_MAX  =  85.0F;
const float BME_PRESS_MIN = 870.0F;
const float BME_PRESS_MAX = 1100.0F;
const float BME_HUM_MIN   =   0.0F;
const float BME_HUM_MAX   = 100.0F;
const int   BME_MAX_ERRORS = 3; // 連続エラーがこの回数を超えたら再初期化

// 前方宣言
void connectWiFi();
void checkWiFiStatus();
void updateSensorData();
bool setupBME280();
void sendToDisplay();

MHZ19 myMHZ19;
HardwareSerial mySerial(SERIAL_CHANNEL);
Adafruit_BME280 bme;
bool bme280Available = false;

// キャッシュ用変数
int cachedCo2 = 0;
int cachedTemp = 0;       // MH-Z19B 内部温度（整数）
float cachedBmeTemp = NAN;     // BME280 温度 [°C]
float cachedBmeHumidity = NAN; // BME280 湿度 [%RH]
float cachedBmePressure = NAN; // BME280 気圧 [hPa]
bool sensorValid = false;
unsigned long lastSensorUpdate = 0;


// WiFi接続処理
void connectWiFi() {
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

// WiFi接続状態の監視
void checkWiFiStatus() {
    static bool lastConnected = false;
    static unsigned long lastDisconnectedTime = 0;
    bool currentlyConnected = (WiFi.status() == WL_CONNECTED);

    if (currentlyConnected && !lastConnected) {
        Serial.print("WiFi Reconnected. IP: ");
        Serial.println(WiFi.localIP());

        lastDisconnectedTime = 0;
    } else if (!currentlyConnected && lastConnected) {
        Serial.println("WiFi Disconnected. Waiting for auto-reconnect...");
        lastDisconnectedTime = millis();
    }

    // 長時間接続できない場合は再起動
    if (!currentlyConnected && lastDisconnectedTime != 0) {
        if (millis() - lastDisconnectedTime > WIFI_RECONNECT_TIMEOUT_MS) {
            Serial.println("WiFi connection lost for too long. Restarting...");
            delay(RESTART_DELAY_MS);
            ESP.restart();
        }
    }
    lastConnected = currentlyConnected;
}

// CO2_Displayへのデータ送信
void sendToDisplay() {
    static unsigned long lastDisplayTime = 0;
    if (millis() - lastDisplayTime < DISPLAY_INTERVAL_MS && lastDisplayTime != 0) return;

    if (WiFi.status() != WL_CONNECTED) return;
    if (!sensorValid) return;

    HTTPClient http;
    WiFiClient client;

    Serial.println("Sending data to CO2_Display...");

    if (http.begin(client, DISPLAY_URL)) {
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        doc["device_id"] = DEVICE_ID;
        doc["co2"] = cachedCo2;

        if (bme280Available && !isnan(cachedBmeTemp)) {
            doc["temperature"] = cachedBmeTemp;
        } else {
            doc["temperature"] = cachedTemp; // MH-Z19B 内部温度
        }

        // BME280データを追加（数値として直接格納）
        if (bme280Available) {
            if (!isnan(cachedBmeHumidity)) doc["humidity"] = cachedBmeHumidity;
            if (!isnan(cachedBmePressure)) doc["pressure"] = cachedBmePressure;
        }

        String json;
        serializeJson(doc, json);

        int httpCode = http.POST(json);

        if (httpCode > 0) {
            Serial.printf("[Display] Result code: %d\n", httpCode);
            Serial.println(json);
        } else {
            Serial.printf("[Display] Failed, error: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
        lastDisplayTime = millis();
    }
}



// BME280 の初期化（再試行対応）
bool setupBME280() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(100000); // 100kHz (Standard mode) — 400kHz より安定
    delay(10);             // バス安定待ち

    if (!bme.begin(BME280_I2C_ADDR)) {
        // アドレスを反転して再試行（0x76 <-> 0x77）
        uint8_t altAddr = (BME280_I2C_ADDR == 0x76) ? 0x77 : 0x76;
        Serial.printf("BME280 not found at 0x%02X. Trying 0x%02X...\n", BME280_I2C_ADDR, altAddr);
        if (!bme.begin(altAddr)) {
            Serial.println("BME280 not found. Check wiring.");
            return false;
        }
        Serial.printf("BME280 found at alternate address 0x%02X\n", altAddr);
    }
    // 屋内環境モード（推奨設定）
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X2,   // 温度
                    Adafruit_BME280::SAMPLING_X16,  // 気圧
                    Adafruit_BME280::SAMPLING_X1,   // 湿度
                    Adafruit_BME280::FILTER_X16,
                    Adafruit_BME280::STANDBY_MS_0_5);
    delay(100); // 最初の測定が安定するまで待機
    Serial.println("BME280 initialized successfully.");
    return true;
}

// センサーデータの定期更新
void updateSensorData() {
    if (millis() - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
        int co2 = myMHZ19.getCO2();

        // MH-Z19B の妥当性チェック
        if (co2 >= CO2_MIN_VALID && co2 <= CO2_MAX_VALID) {
            cachedCo2 = co2;
            cachedTemp = myMHZ19.getTemperature();
            sensorValid = true;
            Serial.printf("[MH-Z19B] CO2: %d ppm  Temp: %d°C\n", cachedCo2, cachedTemp);
        } else {
            Serial.print("Sensor error: Invalid CO2 value received: ");
            Serial.println(co2);
            sensorValid = false;
        }

        // BME280 の読み取りと妥当性チェック
        if (bme280Available) {
            static int bmeErrorCount = 0;

            float rawTemp  = bme.readTemperature();
            float rawHum   = bme.readHumidity();
            float rawPress = bme.readPressure() / 100.0F; // Pa → hPa

            bool tempOk  = !isnan(rawTemp)  && rawTemp  >= BME_TEMP_MIN  && rawTemp  <= BME_TEMP_MAX;
            bool humOk   = !isnan(rawHum)   && rawHum   >= BME_HUM_MIN   && rawHum   <= BME_HUM_MAX;
            bool pressOk = !isnan(rawPress) && rawPress >= BME_PRESS_MIN && rawPress <= BME_PRESS_MAX;

            if (tempOk && humOk && pressOk) {
                cachedBmeTemp     = rawTemp;
                cachedBmeHumidity = rawHum;
                cachedBmePressure = rawPress;
                bmeErrorCount = 0;
                Serial.printf("[BME280] Temp: %.1f°C  Hum: %.1f%%  Press: %.1f hPa\n",
                              cachedBmeTemp, cachedBmeHumidity, cachedBmePressure);
            } else {
                bmeErrorCount++;
                Serial.printf("[BME280] Invalid reading (T=%.1f H=%.1f P=%.1f) error=%d/%d\n",
                              rawTemp, rawHum, rawPress, bmeErrorCount, BME_MAX_ERRORS);
                if (bmeErrorCount >= BME_MAX_ERRORS) {
                    Serial.println("[BME280] Too many errors. Attempting re-init...");
                    bme280Available = setupBME280();
                    bmeErrorCount = 0;
                    // キャッシュを無効化
                    cachedBmeTemp = cachedBmeHumidity = cachedBmePressure = NAN;
                }
            }
        }

        lastSensorUpdate = millis();
    }
}



void setup() {
    Serial.begin(DEBUG_BAUDRATE);
    mySerial.begin(SENSOR_BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN);
    myMHZ19.begin(mySerial);
    myMHZ19.autoCalibration(false);

    // BME280 の初期化
    bme280Available = setupBME280();

    // ウォッチドッグタイマーの設定 (30秒)
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    connectWiFi();
    // WiFiの省電力モードを有効化（発熱対策）
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    Serial.println("WiFi Power Save enabled");

    updateSensorData();
}

void loop() {
    esp_task_wdt_reset();

    checkWiFiStatus();
    updateSensorData();
    sendToDisplay();
}
