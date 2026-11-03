// alert_logic.h를 PC에서 검사한다. test_alert_logic.py가 g++로 컴파일해 실행한다. 실패하면 0이 아닌 값으로 끝난다.
#include <cmath>
#include <cstdio>

#include "alert_logic.h"

static int failures = 0;
#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                \
    }                                                            \
  } while (0)

// 창 하나 = 1초로 본다
static void belowThresholdNeverAlerts() {
  AlertLogic a(0.7f, 2, 10000);
  for (uint32_t t = 0; t < 60; t++) CHECK(!a.update(0.69f, t * 1000));
}

static void needsConsecutiveWindows() {
  AlertLogic a(0.7f, 2, 10000);
  CHECK(!a.update(0.9f, 0));
  CHECK(a.update(0.8f, 1000));  // 부팅 직후 첫 알림은 쿨다운 없이
  CHECK(std::fabs(a.lastPeak() - 0.9f) < 1e-6);
}

static void interruptionResetsCount() {
  AlertLogic a(0.7f, 3, 10000);
  CHECK(!a.update(0.9f, 0));
  CHECK(!a.update(0.9f, 1000));
  CHECK(!a.update(0.1f, 2000));  // 끊김
  CHECK(!a.update(0.9f, 3000));
  CHECK(!a.update(0.9f, 4000));
  CHECK(a.update(0.9f, 5000));
}

static void cooldownBlocksThenRealerts() {
  AlertLogic a(0.7f, 2, 10000);
  CHECK(!a.update(0.9f, 0));
  CHECK(a.update(0.9f, 1000));
  bool again = false;
  uint32_t at = 0;
  for (uint32_t t = 2; t < 30 && !again; t++) {
    again = a.update(0.9f, t * 1000);
    if (again) at = t * 1000;
  }
  CHECK(again);
  CHECK(at >= 11000);  // 첫 알림(1000) + 쿨다운(10000) 이후
}

static void peakIsFromAlertingRun() {
  AlertLogic a(0.5f, 2, 0);
  CHECK(!a.update(0.95f, 0));
  CHECK(!a.update(0.1f, 1000));  // 0.95는 끊긴 구간이라 버려진다
  CHECK(!a.update(0.6f, 2000));
  CHECK(a.update(0.7f, 3000));
  CHECK(std::fabs(a.lastPeak() - 0.7f) < 1e-6);
}

static void millisWrapAround() {
  AlertLogic a(0.7f, 1, 10000);
  const uint32_t nearMax = 0xFFFFFFFFu - 2000;
  CHECK(a.update(0.9f, nearMax));
  CHECK(!a.update(0.9f, nearMax + 5000));  // 0을 지나 3000 근처. 5초만 지났으니 막혀야 한다
  CHECK(a.update(0.9f, nearMax + 11000));
}

int main() {
  belowThresholdNeverAlerts();
  needsConsecutiveWindows();
  interruptionResetsCount();
  cooldownBlocksThenRealerts();
  peakIsFromAlertingRun();
  millisWrapAround();
  if (failures == 0) std::printf("alert_logic: all passed\n");
  return failures == 0 ? 0 : 1;
}
