# CO2モニタ ハングアップ調査・デバッグ手順

1日程度で反応しなくなる問題の原因を特定するために、Google スプレッドシートへ詳細なシステムログを送信する機能を実装しました。

## 1. Google スプレッドシート (GAS) の設定

### スプレッドシートの準備
1. 新規スプレッドシートを作成。
2. 1行目（A1〜H1）に以下の見出しを貼り付け。
   `日時	CO2(ppm)	温度(℃)	精度	空きメモリ	最小メモリ	電波強度(RSSI)	稼働時間(秒)`

### GASスクリプトの貼り付け
1. 「拡張機能」＞「Apps Script」を開く。
2. 以下のコードを貼り付ける（既存のコードは削除）。

```javascript
function doPost(e) {
  try {
    var jsonString = e.postData.contents;
    var data = JSON.parse(jsonString);
    var ss = SpreadsheetApp.getActiveSpreadsheet();
    var sheet = ss.getActiveSheet();
    var rowData = [
      new Date(),
      data.co2,
      data.temp,
      data.accuracy,
      data.free_heap,
      data.min_free_heap,
      data.rssi,
      data.uptime
    ];
    sheet.appendRow(rowData);
    return ContentService.createTextOutput("Success");
  } catch (error) {
    console.error("Error: " + error.toString());
    return ContentService.createTextOutput("Error: " + error.toString());
  }
}
```

### デプロイ設定 (重要)
1. 「新しいデプロイ」をクリック。
2. 種類：**ウェブアプリ**
3. 次のユーザーとして実行：**自分**
4. アクセスできるユーザー：**全員**
5. 発行された `https://script.google.com/.../exec` というURLをコピー。

---

## 2. マイコン側の設定 (`credentials.ini`)

`credentials.ini` に取得した URL を設定します。

```ini
[auth]
build_flags =
    -D WIFI_SSID=\"あなたのSSID\"
    -D WIFI_PASS=\"あなたのパスワード\"
    -D GAS_URL=\"コピーしたURL\"
```

---

## 3. 解析のポイント

ハングアップ（無反応）が発生した際、スプレッドシートの直近のデータを確認します。

| 項目 | 異常の兆候 | 推定される原因 |
| :--- | :--- | :--- |
| **空きメモリ** | 徐々に減り続けている | メモリリーク（プログラムの欠陥） |
| **最小メモリ** | 突然大きく減る / 0に近い | メモリ断片化（Stringの使いすぎ等） |
| **電波強度(RSSI)** | -80dBm よりも低い数値 | WiFiの電波不足・切断 |
| **データ停止** | ある時刻から記録がない | CPUフリーズ / 無限ループ / WDT再起動失敗 |

## 4. ログ送信の仕様
- **送信間隔:** 5分おき
- **ウォームアップ:** 起動後3分間はセンサー値が安定しないため、送信されない場合があります。
- **リダイレクト:** HTTPS (SSL) 通信を使用し、GASのリダイレクトを自動追跡します。
