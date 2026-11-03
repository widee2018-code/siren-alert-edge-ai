# siren_event: ESP32 → Supabase

ESP32가 사이렌을 확인하면(연속 `windows`창 이상) Supabase REST로 한 줄 넣는다. 웹은 같은 테이블을 Realtime으로 받는다.

## 요청

```
POST {SUPABASE_URL}/rest/v1/siren_events
apikey: {anon 또는 publishable key}
Content-Type: application/json
Prefer: return=minimal
```

```json
{
  "device_id": "mic-01",
  "label": "siren",
  "confidence": 0.934,
  "infer_ms": 38,
  "windows": 2,
  "fw_version": "0.1.0",
  "rssi": -54
}
```

| 필드 | 타입 | 뜻 |
|---|---|---|
| `device_id` | text, 필수 | 기기 이름 (`config.h`의 `DEVICE_ID`) |
| `label` | text | 감지한 라벨. 지금은 항상 `siren` |
| `confidence` | real 0~1, 필수 | 알림을 만든 연속 구간에서 가장 높은 siren 점수 |
| `infer_ms` | integer | 마지막 창의 전처리 + 모델 실행 시간 |
| `windows` | integer ≥ 1, 필수 | 알림까지 연속으로 확인한 창 수 (`CONFIRM_WINDOWS`) |
| `fw_version` | text | 펌웨어 버전 |
| `rssi` | integer | 보낼 때 Wi-Fi 신호 세기 (dBm) |

`id`, `created_at`은 DB가 채운다. ESP32에는 시계가 없어서 시각은 서버 기준이다.

## 응답

- `201`: 저장됨
- `400`: 필드 형식·CHECK 위반 (예: confidence > 1). 재시도하지 않는다
- `401`: key가 틀림
- 그 밖: 재시도하지 않고 다음 알림을 기다린다 (쿨다운은 그대로 걸린다)

필드를 바꾸면 `cloud/supabase/schema.sql`, `esp32/firmware/siren_alert/siren_alert.ino`, `web/app.js`, 이 문서를 함께 고친다.
