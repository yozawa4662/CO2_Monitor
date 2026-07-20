#include <WiFi.h>
#include <WebServer.h>
#include <MHZ19.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// credentials.ini で定義することを想定。定義されていない場合のフォールバック。
#ifndef GAS_URL
#define GAS_URL "YOUR_GAS_URL_HERE"
#endif

#ifndef DISPLAY_URL
#define DISPLAY_URL "http://192.168.0.100:8000/data"
#endif

#ifndef DEVICE_ID
#define DEVICE_ID "co2-monitor-01"
#endif

#ifndef ENABLE_GAS
#define ENABLE_GAS 0
#endif

#ifndef ENABLE_HTTP_SERVER
#define ENABLE_HTTP_SERVER 0
#endif

const long SENSOR_BAUDRATE = 9600;
const long DEBUG_BAUDRATE = 115200;
const int HTTP_PORT = 80;
const int RX_PIN = 21;
const int TX_PIN = 2;
const int SERIAL_CHANNEL = 1;
const unsigned long WARMUP_TIME_MS = 180000; // 3分間
const unsigned long SENSOR_UPDATE_INTERVAL = 5000; // 5秒間
const uint32_t WDT_TIMEOUT_S = 30; // 30秒
const unsigned long WIFI_RECONNECT_TIMEOUT_MS = 600000; // 10分間
const unsigned long LOG_INTERVAL_MS = 300000; // 5分間
const unsigned long DISPLAY_INTERVAL_MS = 30000; // 30秒間
const unsigned long RESTART_DELAY_MS = 1000;
const int CO2_MIN_VALID = 1;
const int CO2_MAX_VALID = 10000;

const char* VERSION = "1.3.0";
const uint32_t TOTAL_HEAP = 327680;

// 前方宣言
void connectWiFi();
void checkWiFiStatus();
void setupMDNS();
void updateSensorData();
#if ENABLE_HTTP_SERVER
void handleRoot();
void handleCalibrate();
#endif
void sendLogToGAS();
void sendToDisplay();

#if ENABLE_HTTP_SERVER
WebServer server(HTTP_PORT);
#endif
MHZ19 myMHZ19;
HardwareSerial mySerial(SERIAL_CHANNEL);

// キャッシュ用変数
int cachedCo2 = 0;
int cachedTemp = 0;
int cachedAccuracy = 0;
bool sensorValid = false;
unsigned long lastSensorUpdate = 0;
uint32_t minFreeHeap = TOTAL_HEAP;

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
#if ENABLE_HTTP_SERVER
        setupMDNS(); // 再接続時にmDNSを再設定
#endif
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
        doc["temperature"] = cachedTemp;

        String json;
        serializeJson(doc, json);

        int httpCode = http.POST(json);

        if (httpCode > 0) {
            Serial.printf("[Display] Result code: %d\n", httpCode);
        } else {
            Serial.printf("[Display] Failed, error: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
        lastDisplayTime = millis();
    }
}

// GASへのログ送信
void sendLogToGAS() {
    if (!ENABLE_GAS) return;
    static unsigned long lastLogTime = 0;
    if (millis() - lastLogTime < LOG_INTERVAL_MS && lastLogTime != 0) return;

    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // GASへのHTTPS通信を簡易化

    Serial.println("Sending log to GAS...");

    // 自動リダイレクトを無効化し、手動で処理（HTTP 400対策）
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

    if (http.begin(client, GAS_URL)) {
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        doc["co2"] = cachedCo2;
        doc["temp"] = cachedTemp;
        doc["accuracy"] = cachedAccuracy;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["min_free_heap"] = minFreeHeap;
        doc["rssi"] = WiFi.RSSI();
        doc["uptime"] = millis() / 1000;

        String json;
        serializeJson(doc, json);

        int httpCode = http.POST(json);

        // 302 Found (リダイレクト) の手動処理
        if (httpCode == 302 || httpCode == 301) {
            String newUrl = http.getLocation();
            Serial.print("Redirecting to: ");
            Serial.println(newUrl);
            http.end();

            // 新しいURLでGETリクエスト（ヘッダーをクリーンにするため）
            http.begin(client, newUrl);
            httpCode = http.GET();
        }

        if (httpCode > 0) {
            Serial.printf("[HTTP] Result code: %d\n", httpCode);
            String payload = http.getString();
            Serial.println("Response body: " + payload);
        } else {
            Serial.printf("[HTTP] Failed, error: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
        lastLogTime = millis();
    }
}

// mDNSの設定
void setupMDNS() {
    if (MDNS.begin("co2monitor")) {
        Serial.println("mDNS responder started (co2monitor.local)");
        MDNS.addService("http", "tcp", 80);
    } else {
        Serial.println("Error setting up MDNS responder!");
    }
}

// センサーデータの定期更新
void updateSensorData() {
    if (millis() - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
        int co2 = myMHZ19.getCO2();

        // センサーの妥当性チェック
        if (co2 >= CO2_MIN_VALID && co2 <= CO2_MAX_VALID) {
            cachedCo2 = co2;
            cachedTemp = myMHZ19.getTemperature();
            cachedAccuracy = myMHZ19.getAccuracy();
            sensorValid = true;
        } else {
            Serial.print("Sensor error: Invalid CO2 value received: ");
            Serial.println(co2);
            sensorValid = false;
        }

        lastSensorUpdate = millis();
    }
}

// JSONレスポンスの作成
String createJsonResponse(int co2, int temp, int accuracy, String status) {
    JsonDocument doc;
    doc["co2_ppm"] = co2;
    doc["temperature"] = temp;
    doc["accuracy"] = accuracy;
    doc["status"] = status;

    // システム情報
    JsonObject system = doc["system"].to<JsonObject>();
    uint32_t freeHeap = ESP.getFreeHeap();
    system["uptime"] = millis() / 1000;
    system["free_heap"] = freeHeap;
    system["min_free_heap"] = minFreeHeap;
    system["free_heap_percent"] = (freeHeap * 100) / TOTAL_HEAP;
    system["rssi"] = WiFi.RSSI();
    system["version"] = VERSION;

    String json;
    serializeJson(doc, json);
    return json;
}

#if ENABLE_HTTP_SERVER
// Webサーバーのハンドラ
void handleRoot() {
    String status;
    if (millis() < WARMUP_TIME_MS) {
        status = "warming";
    } else if (!sensorValid) {
        status = "sensor_error";
    } else {
        status = "ok";
    }

    String json = createJsonResponse(cachedCo2, cachedTemp, cachedAccuracy, status);
    server.send(200, "application/json", json);
}

// キャリブレーションのハンドラ
void handleCalibrate() {
    myMHZ19.calibrateZero();
    server.send(200, "text/plain", "Calibrating...");
}
#endif

void setup() {
    Serial.begin(DEBUG_BAUDRATE);
    mySerial.begin(SENSOR_BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN);
    myMHZ19.begin(mySerial);
    myMHZ19.autoCalibration(false);

    // ウォッチドッグタイマーの設定 (30秒)
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    connectWiFi();
    // WiFiの省電力モードを有効化（発熱対策）
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    Serial.println("WiFi Power Save enabled");

#if ENABLE_HTTP_SERVER
    setupMDNS();
#endif
    updateSensorData();

#if ENABLE_HTTP_SERVER
    server.on("/sensor", handleRoot);
    server.on("/calibrate", HTTP_POST, handleCalibrate);
    server.begin();
#endif
}

void loop() {
    esp_task_wdt_reset();

    // 最小メモリ残量の更新
    uint32_t currentFreeHeap = ESP.getFreeHeap();
    if (currentFreeHeap < minFreeHeap) {
        minFreeHeap = currentFreeHeap;
    }

    checkWiFiStatus();
    updateSensorData();
#if ENABLE_HTTP_SERVER
    server.handleClient();
#endif
    sendToDisplay();
    sendLogToGAS();
}
