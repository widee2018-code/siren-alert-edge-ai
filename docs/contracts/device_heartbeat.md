# device_heartbeat: ESP32 → Supabase `devices`

ESP32는 부팅 후 Wi-Fi가 붙으면 바로 한 번, 그 뒤로 30초(`HEARTBEAT_MS`)마다 `devices`의 자기 줄을 덮어쓴다 (없으면 만든다).
웹은 `last_seen`이 90초(`OFFLINE_AFTER_MS`) 넘게 지나면 그 기기를 **응답 없음**으로 보고 경고를 띄운다.
사이렌이 없을 때 "기기가 듣고 있는데 조용한 것"과 "기기가 꺼진 것"을 구분하기 위한 것이다.

## 요청

```
POST {SUPABASE_URL}/rest/v1/devices?on_conflict=device_id
apikey: {anon 또는 publishable key}
Content-Type: application/json
Prefer: resolution=merge-duplicates,return=minimal
```

```json
{
  "device_id": "mic-01",
  "fw_version": "0.3.0",
  "rssi": -54,
  "uptime_s": 1234
}
```

| 필드 | 타입 | 뜻 |
|---|---|---|
| `device_id` | text, 기본 키 | 기기 이름 (`config.h`의 `DEVICE_ID`). siren_events의 `device_id`와 같다 |
| `fw_version` | text | 펌웨어 버전 |
| `rssi` | integer | 보낼 때 Wi-Fi 신호 세기 (dBm) |
| `uptime_s` | bigint | 부팅 후 지난 초. 갑자기 작아지면 재부팅된 것이다 |
| `last_seen` | timestamptz | **보내지 않는다.** DB 트리거가 받은 시각(`now()`)으로 채운다 (ESP32에는 시계가 없다) |

## 응답

- `201`: 처음 만들 때, `200`: 덮어쓸 때 (펌웨어는 2xx를 모두 성공으로 본다)
- `401`: key가 틀림
- `42501`: anon에 권한이 없음. `schema.sql`을 다시 실행한다
- 실패하면 재시도하지 않고 다음 주기(30초 뒤)에 다시 보낸다

## 판정 값

| 값 | 위치 | 기본 |
|---|---|---|
| `HEARTBEAT_MS` | `siren_alert.ino` | 30000 |
| `OFFLINE_AFTER_MS` | `web/app.js` | 90000 (생존 신호 3번을 놓치면) |

`HEARTBEAT_MS`를 늘리면 `OFFLINE_AFTER_MS`도 그 3배 정도로 함께 늘린다.
판정은 휴대폰 시계와 DB 시각을 비교하므로, 휴대폰 시계가 수십 초 이상 틀리면 판정이 어긋날 수 있다.

필드를 바꾸면 `cloud/supabase/schema.sql`, `siren_alert.ino`의 `HEARTBEAT_JSON`, `web/app.js`, 이 문서를 함께 고친다.
