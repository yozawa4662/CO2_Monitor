# CO2 Monitor (XIAO ESP32C3 + MH-Z19B + BME280)

Seeed Studio XIAO ESP32C3、MH-Z19B CO2 センサー、および BME280 温湿度・気圧センサーを使用した、Wi-Fi 経由でデータを取得できる環境モニターです。

## 主な機能

- **環境データ計測**: MH-Z19B による CO2 濃度 (ppm) に加え、BME280 を用いて高精度な温度 (°C)・湿度 (%RH)・気圧 (hPa) を同時計測します。
  - *BME280 が有効な場合、全体の温度情報は BME280 の値が優先されます。*
- **CO2_Display への自動データ送信**: Wi-Fi 経由で CO2 濃度や温度・湿度・気圧などの測定データを、表示用の CO2_Display 端末等へ定期的に送信します。
- **自動再接続 & 自己修復**: Wi-Fi 切断時に自動的に再接続を試みます。長時間（10分以上）接続できない場合は、通信スタックのハングアップを防ぐためシステムを自動再起動します。
- **高安定 Wi-Fi**: 省電力モードを有効化し、通信の応答性と安定性を高めています。
- **安定性**: ウォッチドッグタイマーおよびBME280の自動エラーリセット機能を実装しており、フリーズ時やI2Cバスエラー発生時にも自動復帰します。

## 必要ハードウェア

- **マイコン**: Seeed Studio XIAO ESP32C3
- **CO2センサー**: MH-Z19 シリーズ (MH-Z19B 等)
- **温湿度・気圧センサー**: BME280 (秋月電子 AE-BME280 等、I2C接続、SDO未接続=I2Cアドレス `0x77` に設定)
- **電源**: USB-C 給電 (5V/1A 以上の安定した電源を推奨)
- **コネクタ**: WFR-5（5極レバー式コネクタ、GND一括合流用）

## ピンアサイン

### ① 電源・信号線（メスメスワイヤ）

| デバイス | ピン名 | 接続先 (XIAO ESP32C3) | 備考・通信 |
| :--- | :--- | :--- | :--- |
| **MH-Z19B** | Vin / VCC | **5V (VUSB)** | 電源 (5V) |
| **MH-Z19B** | TX | **D0** | Serial RX (UART) |
| **MH-Z19B** | RX | **D6** | Serial TX (UART) |
| **BME280** | VDD | **3V3** | 電源 (3.3V) |
| **BME280** | SDI (SDA) | **D4 (SDA)** | I2C Data |
| **BME280** | SCK (SCL) | **D5 (SCL)** | I2C Clock |

*※ BME280 の CSB / SDO ピンは接続不要（未使用）*

### ② GND 合流（WFR-5 経由 / オスメスワイヤ）

* **WFR-5 1番目の穴:** XIAO ESP32C3 の **GND** ピン
* **WFR-5 2番目の穴:** MH-Z19B の **GND** ピン
* **WFR-5 3番目の穴:** BME280 の **GND** ピン
* *(4番目・5番目の穴は将来拡張用として空き)*

## セットアップ方法

### 1. 環境構築
PlatformIO がインストールされた VS Code 等を使用してください。

### 2. 認証情報の設定
プロジェクトルートにある `credentials_template.ini` をコピーして `credentials.ini` を作成し、自身の Wi-Fi 環境に合わせて書き換えてください。

```bash
cp credentials_template.ini credentials.ini
```

`credentials.ini` の内容:
```ini
[auth]
build_flags =
    -D WIFI_SSID=\"YOUR_SSID\"
    -D WIFI_PASS=\"YOUR_PASSWORD\"
```

### 3. 書き込み
PlatformIO でプロジェクトをビルドし、XIAO ESP32C3 にアップロードしてください。

## 送信データ仕様

本デバイスは `DISPLAY_URL` に対して、一定間隔（デフォルト30秒）で以下のJSONデータを POST 送信します。

### 送信データ例 (BME280 有効時)
```json
{
  "device_id": "co2-monitor-01",
  "co2": 850,
  "temperature": 24.3,
  "humidity": 45.2,
  "pressure": 1013.2
}
```

### 送信データ例 (BME280 無効時またはエラー時)
```json
{
  "device_id": "co2-monitor-01",
  "co2": 850,
  "temperature": 24
}
```
*※ BME280が無効または値が異常な場合、`humidity` および `pressure` フィールドは送信されず、`temperature` はMH-Z19Bの内部温度（整数値）が使用されます。*

## ライブラリ依存関係
- MH-Z19 (wifwaf/MH-Z19)
- Adafruit BME280 Library
- Adafruit Unified Sensor
- ArduinoJson
- (将来用) Adafruit SSD1306 / GFX Library
