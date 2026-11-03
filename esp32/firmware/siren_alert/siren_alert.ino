// siren_alert: XIAO ESP32S3 Sense 마이크로 1초씩 듣고, 사이렌이면 Supabase에 이벤트를 기록한다.
// 마이크 1초(16kHz) → Edge Impulse 소리 분류(siren / noise) → 연속 확인·쿨다운(alert_logic.h) → Supabase INSERT.
// 웹 페이지(web/)가 Supabase Realtime으로 이벤트를 받아 화면 깜빡임·진동으로 알린다.
// 라이브러리: esp32/model/의 Edge Impulse .zip을 Sketch → Include Library → Add .ZIP Library로 설치.
// 설정: config.example.h를 config.h로 복사해 Wi-Fi와 Supabase 값을 채운다.

#include <siren_detect_inferencing.h>
#include <ESP_I2S.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_memory_utils.h"
#include "alert_logic.h"
#include "config.h"

// 펌웨어를 바꿀 때마다 올린다. 이벤트의 fw_version으로 기록된다
#define FW_VERSION "0.1.0"

// ================= 설정 =================
const char* SIREN_LABEL           = "siren";  // Edge Impulse 라벨과 같아야 한다
const float SIREN_THRESHOLD       = 0.7f;     // 창 하나를 사이렌으로 칠 최소 점수
const int   CONFIRM_WINDOWS       = 2;        // 연속 N창(약 N초) 사이렌이어야 알림 (순간 오탐 제거)
const uint32_t COOLDOWN_MS        = 10000;    // 알림 후 다음 알림까지 대기
const unsigned long WIFI_RETRY_MS = 5000;     // Wi-Fi 끊겼을 때 재연결 간격
const uint16_t HTTP_TIMEOUT_MS    = 5000;
const int   MIC_GAIN              = 1;        // mic_recorder.ino와 같은 값이어야 한다 (학습 데이터와 같은 음량)
// ========================================

// XIAO ESP32S3 Sense 확장보드의 PDM 마이크
#define MIC_CLK_PIN  42
#define MIC_DATA_PIN 41
#define LED_PIN      21  // 사용자 LED (LOW = 켜짐). 사이렌 창이면 켠다

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "마이크(소리)용 모델이 아니다"
#endif
#if EI_CLASSIFIER_FREQUENCY != 16000
#error "16kHz 모델이 아니다. Edge Impulse Create impulse의 Frequency를 16000으로 맞춘다"
#endif

struct SirenResult {
  float score;       // siren 라벨 점수 (0~1)
  uint32_t inferMs;  // 전처리(DSP) + 모델 실행 시간
};

// Edge Impulse 라이브러리의 ei_calloc(weak)을 바꿔 끼운다 (safety-edge-ai에서 찾은 문제).
// 1) ESP32-S3 가속(ESP-NN)은 임시 버퍼가 16바이트 정렬이라고 가정하는데, 라이브러리는 esp32 코어 3.x에서
//    정렬 없이 할당해 버퍼 밖을 덮어쓰고 CORRUPT HEAP으로 재부팅된다.
// 2) 모델 메모리가 PSRAM에 잡히면 추론이 몇 배 느려진다. 내부 RAM에 먼저 잡고, 모자라면 PSRAM에 잡는다.
void* ei_calloc(size_t nitems, size_t size) {
  void* p = heap_caps_aligned_calloc(16, nitems, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_aligned_calloc(16, nitems, size, MALLOC_CAP_DEFAULT);
  if (p && nitems * size >= 16 * 1024) {
    Serial.printf("[mem] 모델 버퍼 %u bytes → %s\n", (unsigned)(nitems * size),
                  esp_ptr_external_ram(p) ? "PSRAM (느림)" : "내부 RAM");
  }
  return p;
}

static I2SClass i2s;
static int16_t audioBuf[EI_CLASSIFIER_RAW_SAMPLE_COUNT];  // 1창(1초) = 16000샘플 = 32KB
static int sirenIndex = -1;
static AlertLogic alert(SIREN_THRESHOLD, CONFIRM_WINDOWS, COOLDOWN_MS);
static unsigned long lastWifiTry = 0;

bool initMic() {
  i2s.setPinsPdmRx(MIC_CLK_PIN, MIC_DATA_PIN);
  if (!i2s.begin(I2S_MODE_PDM_RX, EI_CLASSIFIER_FREQUENCY, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("[mic] 초기화 실패");
    return false;
  }
  // 켜진 직후 수백 ms는 값이 튄다. 버린다
  static char junk[1024];
  for (int i = 0; i < 10; i++) i2s.readBytes(junk, sizeof(junk));
  return true;
}

// 1창을 audioBuf에 채운다. 읽기가 멈추면 false
bool recordWindow() {
  uint8_t* p = (uint8_t*)audioBuf;
  size_t want = sizeof(audioBuf);
  size_t got = 0;
  while (got < want) {
    size_t n = i2s.readBytes((char*)p + got, want - got < 1024 ? want - got : 1024);
    if (n == 0) return false;
    got += n;
  }
  if (MIC_GAIN != 1) {
    for (size_t i = 0; i < EI_CLASSIFIER_RAW_SAMPLE_COUNT; i++) {
      int32_t v = (int32_t)audioBuf[i] * MIC_GAIN;
      audioBuf[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
    }
  }
  return true;
}

// Edge Impulse가 입력을 읽어 가는 콜백
static int getAudio(size_t offset, size_t length, float* out) {
  ei::numpy::int16_to_float(&audioBuf[offset], out, length);
  return 0;
}

bool runSirenModel(SirenResult& out) {
  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  signal.get_data = &getAudio;

  ei_impulse_result_t result = {};
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
  if (err != EI_IMPULSE_OK) {
    Serial.printf("[model] 실행 실패 (%d)\n", err);
    return false;
  }
  out.score = result.classification[sirenIndex].value;
  out.inferMs = (uint32_t)(result.timing.dsp + result.timing.classification);
  return true;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[wifi] 연결 중");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[wifi] 연결됨 %s (RSSI %d dBm)\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("\n[wifi] 실패, loop에서 다시 시도");
  }
  lastWifiTry = millis();
}

// 끊겼으면 WIFI_RETRY_MS마다 재연결을 건다. 기다리지 않으므로 듣기는 계속 돈다
void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWifiTry < WIFI_RETRY_MS) return;
  lastWifiTry = millis();
  Serial.println("[wifi] 끊김, 재연결");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// Supabase REST로 siren_events에 한 줄 넣는다. 201이면 true (docs/contracts/siren_event.md)
bool sendEvent(float confidence, uint32_t inferMs) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[http] Wi-Fi 없음, 기록 건너뜀");
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();  // 인증서 검증을 하지 않는다 (시연용). 실제로 쓰려면 setCACert로 루트 인증서를 넣는다

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(client, String(SUPABASE_URL) + "/rest/v1/siren_events");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);  // anon(publishable) key. Authorization 헤더는 넣지 않는다
  http.addHeader("Prefer", "return=minimal");

  char body[256];
  snprintf(body, sizeof(body),
           "{\"device_id\":\"%s\",\"label\":\"%s\",\"confidence\":%.3f,\"infer_ms\":%u,"
           "\"windows\":%d,\"fw_version\":\"%s\",\"rssi\":%d}",
           DEVICE_ID, SIREN_LABEL, confidence, (unsigned)inferMs, CONFIRM_WINDOWS, FW_VERSION, WiFi.RSSI());

  unsigned long t0 = millis();
  int code = http.POST((uint8_t*)body, strlen(body));
  Serial.printf("[http] POST -> %d (%lu ms) %s\n", code, millis() - t0, code > 0 ? http.getString().c_str() : "");
  http.end();
  return code == 201;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("FW_VERSION %s\n", FW_VERSION);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (strcmp(ei_classifier_inferencing_categories[i], SIREN_LABEL) == 0) sirenIndex = i;
  }
  Serial.printf("[boot] model=%s %d샘플(%dHz), label=%s, threshold=%.2f, confirm=%d, cooldown=%lums\n",
                EI_CLASSIFIER_PROJECT_NAME, EI_CLASSIFIER_RAW_SAMPLE_COUNT, EI_CLASSIFIER_FREQUENCY,
                SIREN_LABEL, SIREN_THRESHOLD, CONFIRM_WINDOWS, (unsigned long)COOLDOWN_MS);
  if (sirenIndex < 0) {
    Serial.printf("[boot] 모델 라벨에 \"%s\"이 없다. SIREN_LABEL을 Edge Impulse 라벨과 맞춘다\n", SIREN_LABEL);
    while (true) delay(1000);
  }
  if (!initMic()) {
    delay(1000);
    ESP.restart();
  }

  connectWiFi();
  Serial.printf("[boot] 준비됨, device=%s\n", DEVICE_ID);
}

void loop() {
  ensureWiFi();

  if (!recordWindow()) {
    Serial.println("[mic] 읽기 실패");
    delay(100);
    return;
  }
  SirenResult r;
  if (!runSirenModel(r)) {
    delay(100);
    return;
  }
  digitalWrite(LED_PIN, r.score >= SIREN_THRESHOLD ? LOW : HIGH);

  bool fire = alert.update(r.score, millis());
  Serial.printf("siren=%.2f infer_ms=%u hits=%d\n", r.score, (unsigned)r.inferMs, alert.hits());
  if (fire) {
    Serial.printf(">> 사이렌 확인 (최고 %.2f), 기록\n", alert.lastPeak());
    sendEvent(alert.lastPeak(), r.inferMs);  // 실패해도 쿨다운은 걸린다 (Supabase가 죽었을 때 매 창 재시도 방지)
  }
}
