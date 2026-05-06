# CO2 Monitor (XIAO ESP32C3 + MH-Z19)

Seeed Studio XIAO ESP32C3 と MH-Z19 CO2 センサーを使用した、Wi-Fi 経由でデータを取得できる CO2 モニターです。

## 主な機能

- **CO2 濃度・温度計測**: MH-Z19 センサーを使用して、周囲の CO2 濃度 (ppm) と温度を計測します。
- **JSON API**: `/sensor` エンドポイントから、現在の値を JSON 形式で取得できます。
- **mDNS 対応**: `http://co2monitor.local/sensor` でアクセス可能です。
- **自動再接続 & 自己修復**: Wi-Fi 切断時に自動的に再接続を試みます。長時間（10分以上）接続できない場合は、通信スタックのハングアップを防ぐためシステムを自動再起動します。
- **高安定 Wi-Fi**: 省電力モードを無効化し、通信の応答性と安定性を高めています。
- **安定性**: ウォッチドッグタイマーを実装しており、フリーズ時にも自動復帰します。

## 必要ハードウェア

- **マイコン**: Seeed Studio XIAO ESP32C3
- **センサー**: MH-Z19 シリーズ (MH-Z19B 等)
- **電源**: USB-C 給電 (5V/1A 以上の安定した電源を推奨)

## ピンアサイン

| XIAO ESP32C3 (GPIO) | MH-Z19 | 備考 |
| :--- | :--- | :--- |
| 5V | Vin | 5V供給が必要 |
| GND | GND | |
| 21 (RX) | TX | センサーの送信をマイコンの受信へ |
| 2 (TX) | RX | マイコンの送信をセンサーの受信へ |

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

## API 仕様

### 1. センサーデータ取得
**Endpoint**: `GET /sensor`

**Response Example**:
```json
{
  "co2_ppm": 850,
  "temperature": 24,
  "accuracy": 0,
  "status": "ok",
  "system": {
    "uptime": 120,
    "free_heap": 256000,
    "free_heap_percent": 78,
    "version": "1.1.0"
  }
}
```
- `status`: `warming` (起動後3分間), `ok`, `sensor_error` のいずれか。

### 2. ゼロキャリブレーション
**Endpoint**: `POST /calibrate`

現在の環境値を 400ppm としてセンサーを校正します。実行の際は、必ず外気などの新鮮な空気（約400ppm）に20分以上晒した状態で実行してください。

## ライブラリ依存関係
- MH-Z19 (wifwaf/MH-Z19)
- ArduinoJson
- (将来用) Adafruit SSD1306 / GFX Library
