#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>

// Wi-Fi設定
const char* ssid = "elecom2g-0f6767";
const char* password = "4784344870359";

WiFiServer server(80); // HTTPサーバーのポート

const int outPin = 2;    // 空気を抜くピン
const int inPin = 4;     // 空気を入れるピン
const int motorPin = 8;  // モーターのピン

enum BabyState { CRYING, SLEEPING, PLAYING, RELAXING };
BabyState currentState = SLEEPING;

// 各パターンの設定値
struct StateSettings {
  int heartRate;         // 心拍数 (1分間の心拍回数)
  int inhaleTime;        // 吸気時間 (膨らむ)
  int exhaleTime;        // 呼気時間 (縮む)
  int variability;       // ランダムの変動幅
};

StateSettings crying = {140, 1000, 2000, 200};     // 泣いているとき
StateSettings sleeping = {60, 3000, 3000, 100};    // 寝ているとき
StateSettings playing = {110, 1500, 2500, 150};    // 遊んでいるとき
StateSettings relaxing = {80, 2000, 2500, 120};    // リラックスしているとき

StateSettings currentSettings;

// 時間計測用の変数
unsigned long previousBeatMillis = 0;   // 心拍のタイミング
unsigned long previousBreathMillis = 0; // 呼吸のタイミング
bool isInhaling = true; // 吸気か呼気かのフラグ

void setup() {
  pinMode(inPin, OUTPUT);
  pinMode(outPin, OUTPUT);
  pinMode(motorPin, OUTPUT);
  Serial.begin(9600);

  // 初期設定の適用
  applyStateSettings();
}

void loop() {
  unsigned long currentMillis = millis();

  // 状態の設定を切り替えたいときは以下のように変更
  // currentState = PLAYING;  // 他の状態に変更する場合
  
  // 状態に応じた設定を適用
  applyStateSettings();

  // 心拍の処理
  int beatInterval = 60000 / currentSettings.heartRate;
  if (currentMillis - previousBeatMillis >= beatInterval) {
    previousBeatMillis = currentMillis;
    
    // 心拍の振動（ON）
    digitalWrite(motorPin, HIGH);
    delay(100 + random(-currentSettings.variability, currentSettings.variability));
    
    // 心拍の振動（OFF）
    digitalWrite(motorPin, LOW);
  }

  // 呼吸の処理
  if (isInhaling) {
    if (currentMillis - previousBreathMillis >= currentSettings.inhaleTime + random(0, currentSettings.variability)) {
      previousBreathMillis = currentMillis;
      isInhaling = false;
      
      // 呼吸を縮ませる
      digitalWrite(outPin, HIGH);
      digitalWrite(inPin, LOW);
    }
  } else {
    if (currentMillis - previousBreathMillis >= currentSettings.exhaleTime + random(0, currentSettings.variability)) {
      previousBreathMillis = currentMillis;
      isInhaling = true;
      
      // 呼吸を膨らませる
      digitalWrite(outPin, LOW);
      digitalWrite(inPin, HIGH);
    }
  }
}

// 現在の状態に応じた設定を適用する関数
void applyStateSettings() {
  switch (currentState) {
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
  }
}
