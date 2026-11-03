# CLAUDE.md

이 저장소에서 사람과 Claude Code가 함께 따르는 규칙이다. 작업을 시작하기 전에 끝까지 읽는다.

## 0. 프로젝트

사이렌 감지 엣지 AI 알림 (청각장애인 보조). 1인 프로젝트, 2026-11-03 시작.
XIAO ESP32-S3 Sense 마이크 → ESP32 안의 소리 분류 모델(siren / noise) → 연속 확인·쿨다운 → Supabase `siren_events` → 휴대폰 웹 페이지가 Realtime으로 받아 화면 깜빡임·진동으로 알린다.
ESP32는 30초마다 `devices`에 생존 신호를 보내고, 웹은 90초 넘게 신호가 없으면 "기기 응답 없음"을 띄운다.
기존 경보를 대체하지 않는 **보조 수단**이다.

먼저 읽을 문서:
1. `README.md`: 폴더, 진행 순서, 판정 설정, 알려진 한계
2. `docs/contracts/`: ESP32 → Supabase 데이터 형식 (`siren_event.md`, `device_heartbeat.md`)
3. `esp32/model/README.md`: Edge Impulse 설정, 데이터 수집 방법

## 1. 필수 조건 (과제)

세 가지를 모두 유지한다. 어떤 변경도 이 중 하나를 깨면 안 된다.

| 조건 | 구현 |
|---|---|
| ESP32에 모델을 넣어 사용 | `esp32/firmware/siren_alert/` + `esp32/model/`의 Edge Impulse 라이브러리 |
| 로그를 DB에 저장 | `cloud/supabase/schema.sql`의 `siren_events`, `devices` |
| CI 구현 | `.github/workflows/ci.yml` |

## 2. 범위

- 지금은 **이진 분류**만 한다. 지금 모델은 `Emergency_vehices_alarm` 1.0.17 (라벨 `Noise`, `SIREN`, 헤더 `<Emergency_vehices_alarm_inferencing.h>`). 모델을 바꾸면 `#include`와 `SIREN_LABEL`을 맞춘다 (`esp32/model/README.md`).
- 사이렌 종류 구분(화재경보, 구급차, 민방위)은 이진 분류가 동작한 뒤에 한다. 라벨을 늘리면 `SIREN_LABEL` 판정과 `label` 컬럼 사용을 함께 바꾼다.

## 3. 개발 환경

| 대상 | 기준 |
|---|---|
| ESP32 | Seeed XIAO ESP32S3 Sense, FQBN `esp32:esp32:XIAO_ESP32S3:PSRAM=opi`, esp32 코어 3.3.12 (`ci.yml`과 같은 값) |
| 마이크 | PDM, CLK GPIO42 / DATA GPIO41, 16000Hz, 16bit 모노, `MIC_GAIN` 8 |
| 모델 | Edge Impulse, 창 1000ms, MFE + 1D Conv, int8, EON |
| DB | Supabase (리전 Seoul) |
| Python | 3.12, 테스트는 `tests/requirements.txt` |

- 위 값을 바꾸면 `ci.yml`과 이 표를 같은 커밋에서 함께 고친다.
- `mic_recorder.ino`와 `siren_alert.ino`의 `MIC_GAIN`·샘플레이트는 항상 같게 둔다 (학습 소리와 실제 소리의 음량이 같아야 한다).
- `siren_alert.ino`의 `ei_calloc` 교체와 `build_opt.h`(ESP-NN 끔)는 safety-edge-ai에서 겪은 CORRUPT HEAP 재부팅을 막는 것이다. 이유를 확인하기 전에는 지우지 않는다.

## 4. 브랜치

- `main`: 최종본. 마지막에 `dev → main` PR로만 병합한다. 직접 push하지 않는다.
- `dev`: 통합·실험. 평소 작업은 여기서 커밋·push한다 (push마다 CI가 돈다).
- 커밋 메시지: `<타입>(<파트>): <요약>`. 타입은 `feat` `fix` `docs` `refactor` `chore` `test`, 파트는 `esp32` `cloud` `web` `tools` `ci`.
  예: `feat(esp32): 쿨다운 중 LED 표시`, `fix(web): 재연결 상태 표시`

## 5. 커밋하지 않는 것

- `esp32/firmware/siren_alert/config.h` (Wi-Fi 비밀번호, Supabase key)
- `web/config.js` (Supabase key, 감시할 기기). 틀은 `web/config.example.js`
- `data/`, `*.wav` (녹음 원본, YouTube 소리 포함)
- Supabase `secret` / `service_role` key는 어디에도 넣지 않는다. ESP32와 웹은 anon(publishable) key만 쓴다.

예외: Edge Impulse Arduino 라이브러리 zip은 `esp32/model/`에 커밋한다 (CI 컴파일용).

## 6. 데이터 형식

- 형식을 바꾸면 `cloud/supabase/schema.sql`, `siren_alert.ino`의 `EVENT_JSON`·`HEARTBEAT_JSON`, `web/app.js`의 `select`, `docs/contracts/`를 같은 커밋에서 함께 고친다. `tests/contracts/`가 셋을 대조한다.
- `devices.last_seen`은 ESP32가 보내지 않고 DB 트리거가 `now()`로 채운다. 트리거를 지우면 upsert 때 갱신되지 않아 웹이 기기를 계속 응답 없음으로 본다.
- `HEARTBEAT_MS`(펌웨어)를 바꾸면 `OFFLINE_AFTER_MS`(웹)를 그 3배로 함께 바꾼다.
- 스키마 변경은 Supabase SQL Editor에서 `schema.sql`을 다시 실행해 반영한다 (여러 번 실행해도 되게 작성한다).
- `SUPABASE_URL`은 `https://<ref>.supabase.co`까지만 넣는다. `/rest/v1/`를 붙이면 경로가 두 번 들어가 404(`PGRST125`)가 난다. 따옴표는 곧은 따옴표 `"`만 쓴다.

## 7. 테스트와 CI

- `pytest`를 저장소 루트에서 돌린다. 테스트는 `tests/<파트>/test_*.py`에 추가한다.
- 펌웨어 판정 규칙은 Arduino 없이 테스트할 수 있게 `alert_logic.h`에 둔다. 규칙을 바꾸면 `tests/esp32/alert_logic_test.cpp`에 경우를 추가한다.
- CI가 실패하면 테스트를 지우거나 건너뛰게 하지 말고 원인을 고친다.
- `esp32/model/`에 zip이 없으면 CI는 `mic_recorder`만 컴파일한다. zip을 넣은 뒤에는 `siren_alert` 컴파일이 통과해야 한다.

## 8. 펌웨어를 바꿀 때

- `FW_VERSION`을 올린다. 이벤트의 `fw_version`으로 기록되어 어느 펌웨어가 보낸 것인지 구분한다.
- 판정 값(`SIREN_THRESHOLD`, `CONFIRM_WINDOWS`, `COOLDOWN_MS`)을 바꾸면 README의 판정 설정 표도 고친다.
- 모델을 바꾸면 `esp32/model/README.md`의 버전·정확도 칸을 채운다.
