# 사이렌 감지 엣지 AI 알림 (청각장애인 보조)

XIAO ESP32-S3 Sense의 마이크가 1초씩 소리를 듣고, ESP32 안의 모델이 사이렌인지 아닌지 판단한다.
사이렌이 연속으로 확인되면 Supabase에 이벤트를 기록하고, 휴대폰 웹 페이지가 실시간으로 받아 화면 깜빡임과 진동으로 알린다.
기존 경보를 대체하지 않는 **보조 수단**이다.

```
[XIAO 마이크] → 사이렌 분류(siren/noise) → 연속 확인·쿨다운 → Supabase INSERT
                    ESP32 안에서 실행                              │ Realtime
                                              [휴대폰 웹 페이지] ◀─┘ 화면 깜빡임 + 진동
```

## 필수 조건

| 조건 | 어디서 |
|---|---|
| ESP32에 모델을 넣어 사용 | `esp32/model/` Edge Impulse 소리 분류 모델, `esp32/firmware/siren_alert/` |
| 로그를 DB에 저장 | `cloud/supabase/schema.sql`의 `siren_events`, 형식은 `docs/contracts/siren_event.md` |
| CI 구현 | `.github/workflows/ci.yml`: 펌웨어 컴파일, 알림 판정 테스트, 필드 대조 테스트 |

## 폴더

| 폴더 | 내용 |
|---|---|
| `esp32/firmware/siren_alert/` | 본 펌웨어. `alert_logic.h`는 연속 확인·쿨다운 규칙 (PC에서 테스트) |
| `esp32/firmware/mic_recorder/` | 학습 데이터 녹음용 펌웨어 (모델 없이 컴파일됨) |
| `esp32/model/` | Edge Impulse Arduino 라이브러리 zip과 모델 정보 |
| `tools/record_wav.py` | mic_recorder로 녹음해 `data/<label>.*.wav` 저장 |
| `cloud/supabase/schema.sql` | 테이블, RLS, Realtime |
| `web/` | 알림 페이지 (정적 파일 3개) |
| `tests/` | `esp32/` 알림 판정, `contracts/` 펌웨어·웹·스키마 필드 대조 |

## 순서

1. **Supabase**: 새 프로젝트 → SQL Editor에 `cloud/supabase/schema.sql` 실행 → Project Settings → API에서 URL과 anon key 확인
2. **데이터 수집**: Arduino IDE에서 보드 `XIAO_ESP32S3`, PSRAM `OPI PSRAM`으로 `mic_recorder`를 올린다
   ```bash
   pip install -r tools/requirements.txt
   python tools/record_wav.py --port /dev/cu.usbmodem101 --label siren
   python tools/record_wav.py --port /dev/cu.usbmodem101 --label noise --count 60
   ```
3. **학습**: `esp32/model/README.md`대로 Edge Impulse 프로젝트 `siren_detect`를 만들어 학습 → Arduino library zip을 `esp32/model/`에 넣고 IDE에도 추가
4. **펌웨어**: `siren_alert/config.example.h`를 `config.h`로 복사해 Wi-Fi·Supabase 값을 채우고 올린다. 시리얼 모니터(115200)에 `siren=0.xx`가 1초마다 찍힌다
5. **웹**: `web/config.js`에 URL과 anon key를 채우고 정적 서버로 연다 (`python -m http.server -d web 8000`). 휴대폰에서 열려면 GitHub Pages처럼 HTTPS로 올린다 (wake lock은 HTTPS에서만 동작)
6. **시험**: 휴대폰으로 사이렌 소리를 틀어 웹에 알림이 뜨는지, Supabase 테이블에 줄이 쌓이는지 본다

ESP32 없이 웹·DB만 시험:
```bash
curl -X POST "$SUPABASE_URL/rest/v1/siren_events" \
  -H "apikey: $SUPABASE_KEY" -H "Content-Type: application/json" \
  -d '{"device_id":"curl","confidence":0.9,"windows":2}'
```

## 판정 설정 (`siren_alert.ino` 위쪽)

| 값 | 기본 | 뜻 |
|---|---|---|
| `SIREN_THRESHOLD` | 0.7 | 창 하나를 사이렌으로 칠 최소 점수 |
| `CONFIRM_WINDOWS` | 2 | 연속 N창(약 N초) 사이렌이어야 알림 |
| `COOLDOWN_MS` | 10000 | 알림 뒤 다시 알리기까지 대기. 사이렌이 계속되면 10초마다 다시 기록 |

## 테스트

```bash
pip install -r tests/requirements.txt
pytest
```

## 커밋하지 않는 것

`esp32/firmware/siren_alert/config.h`(Wi-Fi 비밀번호·key), `data/`와 `*.wav`(녹음 원본, YouTube 소리 포함).
예외: Edge Impulse 라이브러리 zip은 `esp32/model/`에 커밋한다 (CI 컴파일용).

## 알려진 한계

- 1초 녹음 → 추론을 번갈아 해서 추론하는 동안(수십 ms)은 듣지 못한다. 사이렌은 수 초 이상 울려서 판정에는 영향이 작다.
- Supabase 전송(HTTPS)은 1~2초 걸리고 그동안 듣기가 멈춘다. 알림 직후라 쿨다운 안에 들어간다.
- `setInsecure()`로 인증서를 검증하지 않는다 (시연용).
- anon key로 누구나 이벤트를 넣을 수 있다 (시연용 RLS).
