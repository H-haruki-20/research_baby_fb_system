/*
2024/11/19　正常に動作することを確認
http://192.168.2.171/
2024/11/19　WiFi機能を分離しようとしたところ、PCから制御不能になった。
未解決
2024/12/21  心拍をPWMで制御し、出力を上げた。しかし、振動子から音がなってしまう。
*/
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>

// Wi-Fi設定
const char *ssid = "elecom2g-0f6767";
const char *password = "4784344870359";

WiFiServer server(80); // HTTPサーバーのポート

/*
Pin Definition
*/
const int airOutPin = 2;    // 空気を抜くピン
const int airInPin = 4;     // 空気を入れるピン
const int builtInPin = 9;   // 内蔵心拍のピン
const int listBandPin = 10; // 心拍：リストバンド用のピン

/*
各種変数の定義_
*/
const int ON = 100;
const int OFF = 0;

// 時間計測用の変数
unsigned long previousBeatMillis = 0;   // 心拍のタイミング
unsigned long previousBreathMillis = 0; // 呼吸のタイミング
bool isInhaling = true;                 // 吸気か呼気かのフラグ

/*
状態の定義
*/
enum BabyState
{
  CRYING,
  SLEEPING,
  PLAYING,
  RELAXING,
  START,
  STOP
};
BabyState currentState = CRYING;

// 各パターンの設定値
struct StateSettings
{
  int heartRate;   // 心拍数 (1分間の心拍回数)
  int inhaleTime;  // 吸気時間 (膨らむ)
  int exhaleTime;  // 呼気時間 (縮む)
  int variability; // ランダムの変動幅
};

/*
実際の値：{心拍数, 吸気時間, 呼気時間, ランダムの変動幅}
=> 乳幼児の生体情報を取得して、それに合わせて設定値を変更する。
*/
StateSettings crying = {200, 1000, 2000, 200};   // 泣いているとき
StateSettings sleeping = {100, 3000, 3000, 100}; // 寝ているとき
StateSettings playing = {50, 1500, 2500, 150};   // 遊んでいるとき
StateSettings relaxing = {10, 2000, 2500, 120};  // リラックスしているとき
StateSettings start = sleeping;
StateSettings stop = {0.1, 0, 0, 0};

StateSettings currentSettings;

void setup()
{
  Serial.begin(9600);
  pinMode(airInPin, OUTPUT);
  pinMode(airOutPin, OUTPUT);
  pinMode(builtInPin, OUTPUT);
  pinMode(listBandPin, OUTPUT);
  Serial.println("Welcom to Baby BioFeedback System");

  // WiFi接続
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected!");
  Serial.println(WiFi.localIP());

  server.begin(); // サーバー開始

  // 初期設定の適用
  applyStateSettings();
}

void loop()
{
  if (currentState == STOP)
  {
    disableAllPins();
  }
  unsigned long currentMillis = millis();

  // クライアントからのリクエストを処理
  WiFiClient client = server.available();
  if (client)
  {
    String request = handleClient(client);
    processRequest(request); // リクエスト内容を処理
  }

  // 状態に応じた設定を適用
  applyStateSettings();

  // 心拍の処理
  int beatInterval = 60000 / currentSettings.heartRate;
  if (currentMillis - previousBeatMillis >= beatInterval)
  {
    previousBeatMillis = currentMillis;

    // 心拍の振動（ON）
    analogWrite(builtInPin, ON);
    delay(100 + random(-currentSettings.variability, currentSettings.variability));

    // 心拍の振動（OFF）
    analogWrite(builtInPin, OFF);
  }

  // 呼吸の処理
  if (isInhaling)
  {
    if (currentMillis - previousBreathMillis >= currentSettings.inhaleTime + random(0, currentSettings.variability))
    {
      previousBreathMillis = currentMillis;
      isInhaling = false;

      // 呼吸を縮ませる
      digitalWrite(airOutPin, HIGH);
      digitalWrite(airInPin, LOW);
    }
  }
  else
  {
    if (currentMillis - previousBreathMillis >= currentSettings.exhaleTime + random(0, currentSettings.variability))
    {
      previousBreathMillis = currentMillis;
      isInhaling = true;

      // 呼吸を膨らませる
      digitalWrite(airOutPin, LOW);
      digitalWrite(airInPin, HIGH);
    }
  }
}

void disableAllPins()
{
  digitalWrite(airOutPin, LOW);
  digitalWrite(airInPin, LOW);
  digitalWrite(builtInPin, LOW);
}

// クライアントのリクエストを取得
String handleClient(WiFiClient client)
{
  String request = "";
  while (client.connected())
  {
    if (client.available())
    {
      char c = client.read();
      request += c;

      // リクエストの終わり（空行）を検出
      if (c == '\n' && request.endsWith("\r\n\r\n"))
      {
        if (request.startsWith("GET / "))
        {
          // ルートパスにアクセスがあった場合にHTMLを提供
          handleRoot(client);
          break;
        }
        else
        {
          // 他のリクエストは処理
          client.stop();
          return request;
        }
      }
    }
  }
  client.stop();
  return "";
}

// リクエストを解析して状態を変更
void processRequest(String request)
{
  if (request.indexOf("GET /control?state=CRYING") >= 0)
  {
    currentState = CRYING;
  }
  else if (request.indexOf("GET /control?state=SLEEPING") >= 0)
  {
    currentState = SLEEPING;
  }
  else if (request.indexOf("GET /control?state=PLAYING") >= 0)
  {
    currentState = PLAYING;
  }
  else if (request.indexOf("GET /control?state=RELAXING") >= 0)
  {
    currentState = RELAXING;
  }
  else if (request.indexOf("GET /control?state=OFF") >= 0)
  {
    currentState = STOP;
  }
  else if (request.indexOf("GET /control?state=START") >= 0)
  {
    currentState = START;
  }
}

void handleRoot(WiFiClient client)
{
  // HTMLコンテンツ
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Baby Simulator Control</title>
      <style>
        body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }
        button { padding: 10px 20px; margin: 10px; font-size: 16px; cursor: pointer; }
        .crying { background-color: red; color: white; }
        .sleeping { background-color: blue; color: white; }
        .playing { background-color: green; color: white; }
        .relaxing { background-color: orange; color: white; }
        .off { background-color: black; color: white; }
        .start{ background-color:pink; color: white;}
      </style>
    </head>
    <body>
      <h1>Baby Simulator Control</h1>
      <button class="start" onclick="sendCommand('START')">START</button>
      <button class="crying" onclick="sendCommand('CRYING')">CRYING</button>
      <button class="sleeping" onclick="sendCommand('SLEEPING')">SLEEPING</button>
      <button class="playing" onclick="sendCommand('PLAYING')">PLAYING</button>
      <button class="relaxing" onclick="sendCommand('RELAXING')">RELAXING</button>
      <button class="off" onclick="sendCommand('OFF')">STOP</button>

      <script>
        function sendCommand(state) {
          fetch(/control?state=${state})
            .then(response => {
              if (!response.ok) {
                alert("Failed to send command!");
              }
            })
            .catch(error => console.error("Error:", error));
        }
      </script>
    </body>
    </html>
  )rawliteral";

  // HTTPヘッダとHTMLを送信
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.print(html);
}

// 現在の状態に応じた設定を適用する関数
void applyStateSettings()
{
  switch (currentState)
  {
  case CRYING:
    currentSettings = crying;
    break;
  case SLEEPING:
    currentSettings = sleeping;
    break;
  case PLAYING:
    currentSettings = playing;
    break;
  case RELAXING:
    currentSettings = relaxing;
    break;
  case START:
    currentSettings = start;
    break;
  case STOP:
    currentSettings = stop;
    break;
  }
}