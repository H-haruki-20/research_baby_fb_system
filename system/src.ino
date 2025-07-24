/*
=============================================================================================
=============================================================================================
Baby BioFeedback System
乳幼児の生体情報を模倣するシステム
=============================================================================================
=============================================================================================

Git情報：
- リポジトリ: https://github.com/H-haruki-20/research_baby_fb_system/blob/main/system/src/src.ino
- ブランチ: main
git add .
git commit -m "What you did"
git push -u origin main

WiFi情報：
SSID: elecom2g-0f6767, Password: 4784344870359 に接続する。(自宅)
実験の時は、モバイルWiFiを使用すること。HMDとともにそれに接続する。

作業履歴：
2024/11/19　初期バージョンを作成
2024/11/19　正常に動作することを確認
2024/11/19　WiFi機能を分離しようとしたところ、PCから制御不能になった。(未解決)　→　2024/12/21 : fetch('')のバッククォートが抜けてたことが原因(解決)
2024/12/21  心拍をPWMで制御し、出力を上げた。しかし、振動子から音がなってしまう。
2025/02/04　WiFi機能の削除、および平均的な6ヶ月児の心拍数と呼吸時間を設定　→　実験用に使えるように確定
2025/07/17　ArduinoからPico 4 Ultraにデータ送信する機能の追加
*/

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>

/*
=============================================================================================
定数および変数定義
=============================================================================================
*/

/*
Wi-Fi接続情報
- SSID: 接続先のWi-Fiネットワーク名
- password: Wi-Fiネットワークのパスワード
*/
const char *ssid = "elecom2g-0f6767";
const char *password = "4784344870359";
// const char* ssid = "Buffalo-A-9EA8";
// const char* password = "t6v6yuubsytps";

/*
データ送信先の情報
- SEND_ADDRESS: 送信先（VR側）のIPアドレス
（※ HMDでUnityアプリケーションを動作させて、IPアドレスを確認する）
*/
const char *SEND_ADRESS = "192.168.2.174";

// 以下、固定(ポート番号)
const int send_port = 22222;
const int receive_port = 22224;

WiFiUDP udp;

// 送るデータのサイズ。データはショート型の符号付き２バイト、送信時は１バイトに変換。
static const int MSG_SIZE = 10;
static const int MSG_BUFF = MSG_SIZE * 2;

// 共用体の設定
typedef union
{
  short sval[MSG_SIZE];   // ショート型
  uint8_t bval[MSG_BUFF]; // 符号なしバイト型
} UDPData;

UDPData s_upd_message_buf; // 送信用共用体のインスタンスを宣言
UDPData r_upd_message_buf; // 受信用共用体のインスタンスを宣言

int count = 0; // 送信変数用のカウント

/*
Pin定義：
- airOutPin: 空気を抜く制御用
- airInPin: 空気を入れる制御用
- builtInPin: 内蔵心拍の制御用
*/
const int airOutPin = 3;  // 空気を抜くピン
const int airInPin = 5;   // 空気を入れるピン
const int builtInPin = 9; // 内蔵心拍のピン

/*
心拍や呼吸の制御に使用する定数定義:
- HEARTBEAT_ON: 心拍動作時のPWM出力
- HEARTBEAT_OFF: 心拍停止時のPWM出力
- AIR_IN_ON: 吸気時のPWM出力
- AIR_OUT_ON: 呼気時のPWM出力
- AIR_OFF: エアポンプ停止時のPWM出力
*/
const int HEARTBEAT_ON = 255;
const int HEARTBEAT_OFF = 0;
const int AIR_IN_ON = 255;
const int AIR_OUT_ON = 255;
const int AIR_OFF = 0;

// 時間計測用の変数
unsigned long previousBeatMillis = 0;   // 心拍のタイミング
unsigned long previousBreathMillis = 0; // 呼吸のタイミング
bool isInhaling = true;                 // 吸気か呼気かのフラグ

// 心拍制御用の変数
unsigned long heartbeatStartMillis = 0; // 心拍開始時刻
bool isHeartbeating = false;            // 心拍中フラグ
int heartbeatDuration = 100;            // 心拍の持続時間（基本値）

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
状態ごとの値：{心拍数, 吸気時間, 呼気時間, ランダムの変動幅}
*/
StateSettings crying = {140, 1000, 1200, 200};   // 泣いているとき
StateSettings sleeping = {100, 3000, 3000, 100}; // 寝ているとき
StateSettings playing = {50, 1500, 2500, 150};   // 遊んでいるとき
StateSettings relaxing = {10, 2000, 2500, 120};  // リラックスしているとき
StateSettings start = sleeping;
StateSettings stop = {0.1, 0, 0, 0};

StateSettings currentSettings;

/*
=============================================================================================
関数定義
=============================================================================================
*/

/*
applyStateSettings():
現在の状態に応じた設定（心拍数、呼吸パターンなど）を適用する関数
- 入力: なし（`currentState`を参照）
- 出力: なし（`currentSettings`を更新）
*/
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

/*
disableAllPins():
すべての出力をオフにする
*/
void disableAllPins()
{
  analogWrite(airOutPin, AIR_OFF);
  analogWrite(airInPin, AIR_OFF);
  analogWrite(builtInPin, HEARTBEAT_OFF);
}

/*
sendUDP():
送信用の関数　Arduino → VR(Unity)
*/
void sendUDP()
{
  String test = "";

  udp.beginPacket(SEND_ADRESS, send_port);

  for (int i = 0; i < MSG_BUFF; i++)
  {
    udp.write(s_upd_message_buf.bval[i]); // １バイトずつ送信
    if (i % 2 == 0)
    {
      test += String(s_upd_message_buf.sval[i / 2]) + ", ";
    }
  }
  Serial.println("[SEND] " + test); // 送信データ（short型）を表示

  udp.endPacket(); // UDPパケットの終了
}

/*
receiveUDP():
受信用の関数　VR(Unity) → Arduino
あまり使う機会ないかも
*/
void receiveUDP()
{
  int packetSize = udp.parsePacket();
  byte tmpbuf[MSG_BUFF]; // パケットを一次受けする配列

  // データの受信
  Serial.print("[RESV] ");
  if (packetSize == MSG_BUFF)
  {                             // 受信したパケットの量を確認
    udp.read(tmpbuf, MSG_BUFF); // パケットを受信
    for (int i = 0; i < MSG_BUFF; i++)
    {
      r_upd_message_buf.bval[i] = tmpbuf[i]; // 受信データを共用体に転記
    }
    for (int i = 0; i < MSG_SIZE; i++)
    {
      Serial.print(String(r_upd_message_buf.sval[i])); // シリアルモニタに出力
      Serial.print(", ");
    }
  }
  else
  {
    Serial.print("none."); // 受信していない場合はシリアルモニタにnone.を出力
  }
  Serial.println();
}

/*
=============================================================================================
メインプログラム
=============================================================================================
*/

void setup()
{
  Serial.begin(9600);
  delay(500);
  pinMode(airInPin, OUTPUT);
  pinMode(airOutPin, OUTPUT);
  pinMode(builtInPin, OUTPUT);
  Serial.println("************************************************");
  Serial.println("****** Welcome to Baby BioFeedback System ******");
  Serial.println("************************************************");

  // *********** WiFi接続 ******************** //
  WiFi.disconnect();
  Serial.println("Connecting to WiFi to : " + String(ssid));
  delay(100);

  WiFi.begin(ssid, password); // Wifiに接続

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
  }

  Serial.println("WiFi connected.");
  Serial.print("WiFi connected. ESP32's IP address is : ");
  Serial.println(WiFi.localIP());

  // UDP 開始
  udp.begin(receive_port);
  delay(500);

  // *********** WiFi接続終わり ******************** //

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

  // 状態に応じた設定を適用
  applyStateSettings();

  /*
  ====================================================
  心拍の処理
  ====================================================
  */
  static bool heartbeatActive = false;
  static bool lastHeartbeatActive = false;
  int beatInterval = 60000 / currentSettings.heartRate;

  if (!isHeartbeating && (currentMillis - previousBeatMillis >= beatInterval))
  {
    previousBeatMillis = currentMillis;
    heartbeatStartMillis = currentMillis;
    isHeartbeating = true;
    heartbeatActive = true;

    // 心拍の持続時間を変動させる
    heartbeatDuration = 50 + random(-currentSettings.variability, currentSettings.variability);

    // 心拍の振動（ON）
    analogWrite(builtInPin, HEARTBEAT_ON);
  }

  // 心拍の終了判定
  if (isHeartbeating && (currentMillis - heartbeatStartMillis >= heartbeatDuration))
  {
    isHeartbeating = false;
    heartbeatActive = false;

    // 心拍の振動（OFF）
    analogWrite(builtInPin, HEARTBEAT_OFF);
  }

  /*
  ================================================
  呼吸の処理
  ================================================
  */
  static bool lastIsInhaling = true;
  bool breathingStateChanged = false;

  if (isInhaling)
  {
    if (currentMillis - previousBreathMillis >= currentSettings.inhaleTime + random(0, currentSettings.variability))
    {
      previousBreathMillis = currentMillis;
      isInhaling = false;
      breathingStateChanged = true;

      // 呼吸を膨らませる
      analogWrite(airOutPin, AIR_OFF);
      analogWrite(airInPin, AIR_IN_ON);
    }
  }
  else
  {
    if (currentMillis - previousBreathMillis >= currentSettings.exhaleTime + random(0, currentSettings.variability))
    {
      previousBreathMillis = currentMillis;
      isInhaling = true;
      breathingStateChanged = true;

      // 呼吸を縮ませる
      analogWrite(airOutPin, AIR_OUT_ON);
      analogWrite(airInPin, AIR_OFF);
    }
  }

  /*
  =========================================================
  体温
  =========================================================
  */
  float bodyTemp = 37.5;



  /*
  =========================================================
  Unityへ送信
  =========================================================
  */
  // 心拍または呼吸の状態が変化した時に送信
  bool shouldSend = false;

  // 心拍の状態変化をチェック
  if (heartbeatActive != lastHeartbeatActive)
  {
    shouldSend = true;
    lastHeartbeatActive = heartbeatActive;
  }

  // 呼吸の状態変化をチェック
  if (breathingStateChanged)
  {
    shouldSend = true;
  }

  // 定期的な送信も維持（状態確認用、1秒間隔）
  static unsigned long lastPeriodicSendTime = 0;
  if (currentMillis - lastPeriodicSendTime >= 1000)
  {
    shouldSend = true;
    lastPeriodicSendTime = currentMillis;
  }

  if (shouldSend)
  {
    /*
    =========================================================
    送信データ s_udp_message_buf.sval
    [0] -> 心拍数：int
    [1] -> 呼吸：空気 in(1) / out(0)
    [2] -> 体温：float (整数部分のみ)
    [3] -> カウンタ：int
    [4] -> タイムスタンプ：int
    [5] -> 現在の状態：int
    [6]-[9] -> 予備
    =========================================================
    */
    s_upd_message_buf.sval[0] = currentSettings.heartRate; // 心拍数
    s_upd_message_buf.sval[1] = isInhaling ? 1 : 0;        // 呼吸状態（1:吸気, 0:呼気）
    s_upd_message_buf.sval[2] = (int)(bodyTemp * 10);      // 体温（37.5℃ → 375として送信）
    s_upd_message_buf.sval[3] = count++;                   // カウンタ
    s_upd_message_buf.sval[4] = currentMillis % 65536;     // タイムスタンプ（下位16bit）
    s_upd_message_buf.sval[5] = (int)currentState;         // 現在の状態
    s_upd_message_buf.sval[6] = 0;                         // 以下、予備
    s_upd_message_buf.sval[7] = 0;
    s_upd_message_buf.sval[8] = 0;
    s_upd_message_buf.sval[9] = 0;

    sendUDP();
  }
}