// mic_recorder: siren_alert와 같은 마이크 설정으로 녹음해 USB 시리얼로 PC에 보낸다. 학습 데이터 수집용.
// PC의 tools/record_wav.py가 "R<초>\n"을 보내면 그만큼 녹음해 "PCM <바이트 수>\n" 뒤에 16bit PCM(little endian)을 보낸다.
// 그 밖에 보내는 줄은 모두 "#"으로 시작한다 (record_wav.py가 화면에 찍기만 한다).
// 모델이 없어도 컴파일된다. 보드 설정: PSRAM → OPI PSRAM

#include <ESP_I2S.h>

const int SAMPLE_RATE = 16000;  // siren_alert 모델 입력과 같다
const int MIC_GAIN    = 1;      // siren_alert.ino와 같은 값이어야 한다
const int MAX_SECONDS = 5;

#define MIC_CLK_PIN  42
#define MIC_DATA_PIN 41
#define LED_PIN      21  // 녹음하는 동안 켠다 (LOW = 켜짐)

static I2SClass i2s;
static int16_t* buf = nullptr;  // MAX_SECONDS 분량, PSRAM

// 대기하는 동안 DMA에 쌓인 소리를 버린다 (명령을 받은 뒤의 소리만 녹음)
void drainMic() {
  static char junk[1024];
  for (int i = 0; i < 4; i++) i2s.readBytes(junk, sizeof(junk));
}

size_t record(int seconds) {
  size_t want = (size_t)seconds * SAMPLE_RATE * sizeof(int16_t);
  size_t got = 0;
  while (got < want) {
    size_t n = i2s.readBytes((char*)buf + got, want - got < 1024 ? want - got : 1024);
    if (n == 0) break;
    got += n;
  }
  if (MIC_GAIN != 1) {
    for (size_t i = 0; i < got / 2; i++) {
      int32_t v = (int32_t)buf[i] * MIC_GAIN;
      buf[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
    }
  }
  return got;
}

void setup() {
  Serial.begin(921600);  // USB CDC라 실제 속도와는 상관없다
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  buf = (int16_t*)ps_malloc((size_t)MAX_SECONDS * SAMPLE_RATE * sizeof(int16_t));
  i2s.setPinsPdmRx(MIC_CLK_PIN, MIC_DATA_PIN);
  if (!buf || !i2s.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    while (true) {
      Serial.println("# 초기화 실패 (PSRAM을 OPI로 켰는지 확인)");
      delay(2000);
    }
  }
  for (int i = 0; i < 10; i++) drainMic();  // 켜진 직후 튀는 값
  Serial.printf("# ready %dHz max %ds\n", SAMPLE_RATE, MAX_SECONDS);
}

void loop() {
  if (!Serial.available()) {
    drainMic();
    return;
  }
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (!line.startsWith("R")) {
    if (line.length()) Serial.printf("# 모르는 명령: %s\n", line.c_str());
    return;
  }
  int seconds = line.substring(1).toInt();
  if (seconds < 1) seconds = 1;
  if (seconds > MAX_SECONDS) seconds = MAX_SECONDS;

  drainMic();
  digitalWrite(LED_PIN, LOW);
  size_t n = record(seconds);
  digitalWrite(LED_PIN, HIGH);

  Serial.printf("PCM %u\n", (unsigned)n);
  Serial.write((uint8_t*)buf, n);
  Serial.flush();
}
