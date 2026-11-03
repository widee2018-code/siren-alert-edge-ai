// alert_logic.h: 1초 창마다 나온 siren 점수로 알림을 보낼지 정한다.
// Arduino에 의존하지 않아 PC에서 g++로 테스트한다 (tests/esp32/alert_logic_test.cpp).
// 규칙: 점수가 threshold 이상인 창이 confirm번 연속이면 알림. 알림 뒤 cooldownMs 동안은 다시 알리지 않는다.
//       사이렌이 계속 울리면 쿨다운이 끝날 때마다 다시 알린다.
#pragma once
#include <stdint.h>

class AlertLogic {
 public:
  AlertLogic(float threshold, int confirm, uint32_t cooldownMs)
      : threshold_(threshold), confirm_(confirm), cooldownMs_(cooldownMs) {}

  // 창 하나의 점수를 넣는다. 알림을 보내야 하면 true
  bool update(float score, uint32_t nowMs) {
    if (score >= threshold_) {
      hits_++;
      if (score > runPeak_) runPeak_ = score;
    } else {
      hits_ = 0;
      runPeak_ = 0.0f;
    }
    if (hits_ < confirm_) return false;
    // millis()가 49일마다 0으로 돌아가도 부호 없는 뺄셈이라 그대로 맞다
    if (sent_ && nowMs - lastSentMs_ < cooldownMs_) return false;

    sent_ = true;
    lastSentMs_ = nowMs;
    lastPeak_ = runPeak_;
    hits_ = 0;
    runPeak_ = 0.0f;
    return true;
  }

  int hits() const { return hits_; }            // 지금까지 연속으로 넘은 창 수
  float lastPeak() const { return lastPeak_; }  // 마지막 알림을 만든 연속 구간의 최고 점수

 private:
  float threshold_;
  int confirm_;
  uint32_t cooldownMs_;
  int hits_ = 0;
  float runPeak_ = 0.0f;
  float lastPeak_ = 0.0f;
  bool sent_ = false;  // 부팅 직후 첫 알림은 쿨다운 없이 보낸다
  uint32_t lastSentMs_ = 0;
};
