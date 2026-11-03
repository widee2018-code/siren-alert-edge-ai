# 진행 가이드: 학습된 모델 사용 (2026-11-03)

Edge Impulse에서 **이미 학습이 끝난 사이렌 모델**을 받아 XIAO ESP32-S3에 올리고, Supabase 기록과 웹 알림까지 완성하는 순서다.
데이터 녹음·학습은 하지 않는다 (직접 학습하려면 `guide_recording.md`).
위에서부터 차례로 진행하고, 끝낸 단계는 `[x]`로 바꾼다.

## 전체 순서와 시간

| 단계 | 할 일 | 예상 시간 | 상태 |
|---|---|---|---|
| 0 | 저장소 만들기, GitHub push (`main`, `dev`) | - | [x] |
| 1 | Supabase 테이블 만들기, curl로 저장 확인 | 20분 | [ ] |
| 2 | Arduino IDE 준비 | 15분 | [ ] |
| 3 | 모델 받기, 펌웨어와 맞는지 확인 | 30분 | [ ] |
| 4 | 펌웨어에 모델 연결 (`#include`, 라벨) | 15분 | [ ] |
| 5 | 펌웨어 올리기, 동작 확인 | 30분 | [ ] |
| 6 | XIAO 마이크에서의 성능 확인, 판정 값 조정 | 1시간 | [ ] |
| 7 | 웹 알림 페이지 | 30분 | [ ] |
| 8 | 처음부터 끝까지 시험 | 30분 | [ ] |
| 9 | 커밋, CI 확인, `dev → main` 병합 | 30분 | [ ] |
| 10 | 발표 준비 | 남는 시간 | [ ] |

막히면 30분 넘게 붙잡지 말고 맨 아래 **문제 해결**을 먼저 본다.

---

## 1. Supabase

1. https://supabase.com → **New project**. Name `siren-alert`, Region **Northeast Asia (Seoul)**.
2. **SQL Editor** → New query → `cloud/supabase/schema.sql` 전체를 붙여 넣고 **Run**.
3. 확인
   - **Table Editor**에 `siren_events`가 있다.
   - **Database → Publications → `supabase_realtime`**에서 `siren_events`가 켜져 있다.
4. **Project Settings → API Keys**에서 publishable(또는 Legacy의 anon) key, **Data API**에서 Project URL을 복사한다.
   `secret` / `service_role` key는 쓰지 않는다.
5. 저장 시험

   ```bash
   export SUPABASE_URL=https://<프로젝트ref>.supabase.co
   export SUPABASE_KEY=<복사한 key>

   curl -i -X POST "$SUPABASE_URL/rest/v1/siren_events" \
     -H "apikey: $SUPABASE_KEY" -H "Content-Type: application/json" \
     -d '{"device_id":"curl","confidence":0.9,"windows":2}'
   ```

   첫 줄이 `HTTP/2 201`이면 성공이다. Table Editor에서 시험 줄은 지워도 된다.

> **주의**: URL은 `.supabase.co`에서 끝난다. `/rest/v1/`를 붙이면 404(`PGRST125`)가 난다.
> 따옴표는 키보드로 친 `"`만 쓴다. 메신저에서 복사한 둥근 따옴표(`“ ”`)는 `URL rejected` 에러를 낸다.

---

## 2. Arduino IDE 준비

1. Arduino IDE 2를 연다.
2. **Settings → Additional boards manager URLs**에 추가:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. **Boards Manager**에서 `esp32` (by Espressif) **3.3.12**를 설치한다 (CI와 같은 버전).
4. **Tools** 메뉴
   - Board: **XIAO_ESP32S3**
   - PSRAM: **OPI PSRAM**
   - Port: `/dev/cu.usbmodem…` (터미널에서 `ls /dev/cu.usbmodem*`로 찾는다)

---

## 3. 모델 받기와 확인

### 3-1. 받기

- **내 Edge Impulse 프로젝트**면: **Deployment** → **Arduino library** → **Quantized (int8)**, **EON Compiler** 켜기 → **Build**.
- **다른 사람의 공개 프로젝트**면: 프로젝트 페이지에서 **Clone this project**로 내 계정에 복사한 뒤 위와 같이 받는다.
  출처(프로젝트 링크, 만든 사람)는 `esp32/model/README.md`와 발표 자료에 적는다.

받은 zip을 `esp32/model/`에 복사한다.

### 3-2. 펌웨어와 맞는지 확인

펌웨어(`siren_alert.ino`)는 아래 조건을 가정한다. zip 안의 설정 파일로 확인한다.

```bash
cd siren-alert-edge-ai
Z=$(ls esp32/model/*.zip | head -1)

# 1) 헤더 이름
unzip -l "$Z" | grep -E "src/[^/]+_inferencing\.h$"

# 2) 센서, 주파수, 창 크기, 라벨 수
unzip -p "$Z" "*/src/model-parameters/model_metadata.h" \
  | grep -E "define EI_CLASSIFIER_(PROJECT_NAME|SENSOR|FREQUENCY|RAW_SAMPLE_COUNT|LABEL_COUNT|INTERVAL_MS) "

# 3) 라벨 이름
unzip -p "$Z" "*/src/model-parameters/model_variables.h" | grep -A3 "inferencing_categories"
```

| 항목 | 펌웨어가 기대하는 값 | 다르면 |
|---|---|---|
| 헤더 이름 | `siren_detect_inferencing.h` | 4단계에서 `#include`를 바꾼다 (흔한 경우, 문제없음) |
| `EI_CLASSIFIER_SENSOR` | `1` (마이크) | 소리 모델이 아니다. 다른 모델을 받는다 |
| `EI_CLASSIFIER_FREQUENCY` | `16000` | 컴파일 에러가 난다. 값을 알려 주면 펌웨어를 맞춘다 (XIAO 마이크는 다른 주파수도 가능) |
| 창 크기 (`RAW_SAMPLE_COUNT ÷ FREQUENCY`) | 1초 (16000샘플) | 그대로 동작한다. 판단 주기가 창 크기만큼 바뀐다 |
| 라벨 | `siren`이 있다 | 4단계에서 `SIREN_LABEL`을 바꾼다 |
| 사이렌 라벨 수 | 1개 | `ambulance`, `firetruck`처럼 여러 개면 지금 펌웨어는 하나만 본다. 점수를 합치도록 바꿔야 하니 알려 준다 |

확인한 값은 `esp32/model/README.md` 표에 적는다 (출처, 라벨, 주파수, 창 크기, 라이브러리 버전).

---

## 4. 펌웨어에 모델 연결

`esp32/firmware/siren_alert/siren_alert.ino` 위쪽 두 곳만 고친다.

```cpp
#include <siren_detect_inferencing.h>   // ← 3-2에서 찾은 헤더 이름으로
...
const char* SIREN_LABEL = "siren";      // ← 모델의 사이렌 라벨 이름으로 (대소문자까지 같게)
```

그다음 설정 파일을 만든다.

```bash
cp esp32/firmware/siren_alert/config.example.h esp32/firmware/siren_alert/config.h
```

`config.h`에 Wi-Fi 이름·비밀번호, `SUPABASE_URL`, `SUPABASE_KEY`를 채운다 (1단계 주의 사항 그대로). Wi-Fi는 **2.4GHz**여야 한다.

---

## 5. 펌웨어 올리기

1. Arduino IDE: **Sketch → Include Library → Add .ZIP Library** → `esp32/model/`의 zip.
2. `siren_alert.ino`를 열고 **Upload**. 첫 컴파일은 모델 때문에 몇 분 걸린다.
3. Serial Monitor (115200)에서 확인

   ```
   FW_VERSION 0.1.0
   [boot] model=<프로젝트 이름> 16000샘플(16000Hz), label=siren, threshold=0.70, confirm=2, cooldown=10000ms
   [wifi] 연결됨 192.168.x.x (RSSI -50 dBm)
   [boot] 준비됨, device=mic-01
   siren=0.02 infer_ms=35 hits=0
   ```

4. 휴대폰으로 사이렌 소리를 틀면 `siren=0.9x`가 이어지다가 다음이 찍혀야 한다.

   ```
   >> 사이렌 확인 (최고 0.95), 기록
   [http] POST -> 201 (1200 ms)
   ```

`[boot] 모델 라벨에 "siren"이 없다`가 나오면 4단계의 `SIREN_LABEL`이 모델 라벨과 다르다.

판단 주기는 **창 크기(보통 1초) + `infer_ms`**다. Serial Monitor의 시계 아이콘(Toggle timestamp)을 켜면 `siren=` 줄 간격으로 확인할 수 있다.

---

## 6. 성능 확인과 판정 값 조정

학습된 모델은 보통 **휴대폰이나 공개 데이터셋 소리로 학습**되어 있다. XIAO 마이크는 음질과 음량이 달라서 Edge Impulse에 나온 정확도보다 낮게 나올 수 있다. 그래서 이 단계가 이 가이드에서 가장 중요하다.

### 6-1. 점수 관찰

Serial Monitor를 보면서 소리별 `siren=` 점수를 표에 적는다. **학습에 쓰지 않은 사이렌 영상**으로 시험한다.

| 소리 | 거리 | siren 점수 범위 |
|---|---|---|
| 조용한 방 | - | |
| 말소리 | 1m | |
| 음악 | 1m | |
| 자동차 경적 (YouTube) | 1m | |
| 구급차 사이렌 (YouTube) | 30cm | |
| 구급차 사이렌 | 1m | |
| 소방차 사이렌 | 1m | |
| 경찰차 사이렌 | 1m | |

### 6-2. 조정 (`siren_alert.ino` 위쪽)

| 증상 | 조정 |
|---|---|
| 사이렌 점수가 0.5~0.7에 머문다 | `SIREN_THRESHOLD`를 사이렌 점수와 비사이렌 점수의 중간쯤으로 내린다 (예: 0.5) |
| 말소리·음악에도 알림이 온다 | `SIREN_THRESHOLD`를 올리거나 `CONFIRM_WINDOWS`를 3으로 |
| 사이렌이 계속 울릴 때 알림이 너무 잦다 | `COOLDOWN_MS`를 20000으로 |
| 사이렌 점수가 전체적으로 아주 낮다 (0.3 이하) | 소리가 너무 작을 수 있다. `MIC_GAIN`을 2~4로 올려 본다 |
| 어떤 값으로도 사이렌과 비사이렌이 갈리지 않는다 | 모델이 이 마이크에 맞지 않는다. Edge Impulse에서 XIAO로 녹음한 소리를 추가해 다시 학습해야 한다 (`guide_recording.md` 3~4단계) |

값을 바꾸면 `FW_VERSION`을 올리고, README의 판정 설정 표도 고친다.
6-1의 표와 최종 판정 값은 발표 자료에 그대로 쓴다.

---

## 7. 웹 알림 페이지

1. `web/config.js`에 `supabaseUrl`, `supabaseKey`를 채운다.
2. 실행

   ```bash
   python -m http.server -d web 8000
   ```

3. Mac에서 http://localhost:8000 → 오른쪽 위가 **실시간 연결됨**인지 확인한다.
4. 휴대폰에서 열기: Mac과 같은 Wi-Fi에 연결하고 `http://<Mac IP>:8000`을 연다.
   Mac IP는 `ipconfig getifaddr en0`로 확인한다.
5. **알림 켜기**를 누른다 (진동 허용).
6. 1단계의 curl을 다시 보내 화면이 빨갛게 깜빡이는지 확인한다.

- 진동은 Android 브라우저에서만 된다. iPhone은 화면 깜빡임만 된다.
- `http://`로 열면 화면 꺼짐 방지(wake lock)가 동작하지 않는다. 시연 중에는 휴대폰 자동 잠금을 꺼 둔다.

---

## 8. 처음부터 끝까지 시험

| 시험 | 기대 결과 | 결과 |
|---|---|---|
| 조용한 상태로 1분 | 알림 없음 | |
| 말하기, 음악 1분 | 알림 없음 | |
| 경적 소리 | 알림 없음 | |
| 사이렌 1m 거리 | 2~3초 안에 웹 알림, Supabase에 한 줄 | |
| 사이렌 30초 계속 | 10초 간격으로 기록 (쿨다운) | |
| Wi-Fi를 껐다 켜기 | 다시 연결되고 다음 사이렌은 기록됨 | |

---

## 9. 커밋, CI, 병합

평소 작업은 `dev`에서 한다.

```bash
git switch dev
git add esp32/model/ esp32/firmware/siren_alert/siren_alert.ino
git commit -m "feat(esp32): 학습된 사이렌 모델 연결"
git push
```

- 모델 zip은 커밋한다 (CI가 설치해 컴파일한다). `config.h`는 `.gitignore`에 있어 올라가지 않는다. `git status`로 한 번 더 확인한다.
- push하면 GitHub **Actions**에서 CI가 돈다. zip이 들어간 뒤에는 `siren_alert 컴파일`이 건너뛰지 않고 통과해야 한다.
- 마지막에 GitHub에서 **Pull requests → New → base: `main` ← compare: `dev`**로 PR을 만들고, CI가 초록불이면 병합한다.

---

## 10. 발표 준비

- **문제**: 청각장애인은 사이렌·화재경보를 듣지 못한다.
- **해결**: ESP32가 직접 소리를 판단한다(소리를 클라우드로 보내지 않아 프라이버시·지연 면에서 유리하다). 결과만 DB에 기록하고 휴대폰으로 시각·촉각 알림을 준다.
- **필수 조건 대응**: 모델 on ESP32 / Supabase 로그 / GitHub Actions CI (README 표)
- **숫자**: 모델 정확도(Edge Impulse)와 실제 XIAO에서의 점수(6-1 표), 추론 시간(`infer_ms`), 사이렌 시작부터 웹 알림까지 걸린 시간, 8단계 시험표
- **모델 출처**: 다른 사람의 프로젝트를 썼다면 출처를 밝힌다
- **시연**: 휴대폰 A로 사이렌 재생 → 휴대폰 B 화면이 깜빡임 → Supabase 테이블에 줄이 쌓이는 화면
- **한계와 다음 단계**: 보조 수단일 뿐이다. 다음 단계로 XIAO 마이크 데이터로 재학습, 사이렌 종류 구분, 진동 모터 웨어러블

---

## 문제 해결

| 증상 | 해결 |
|---|---|
| `…_inferencing.h: No such file` | zip을 IDE에 추가하지 않았거나 `#include` 이름이 다르다 (3-2) |
| `마이크(소리)용 모델이 아니다` | 소리 모델이 아니다 (3-2의 `EI_CLASSIFIER_SENSOR`) |
| `16kHz 모델이 아니다` | 모델 주파수가 다르다. 펌웨어 수정이 필요하다 |
| `[boot] 모델 라벨에 "siren"이 없다` | `SIREN_LABEL`을 모델 라벨과 똑같이 |
| 업로드가 안 됨 (`No serial data received`) | XIAO의 **BOOT** 버튼을 누른 채 USB를 꽂고 다시 Upload |
| 부팅 후 계속 재부팅 (`CORRUPT HEAP`) | `build_opt.h`가 스케치 폴더에 있는지 확인 |
| `siren=` 점수가 항상 0 근처 | 확장보드(마이크 보드)가 제대로 끼워졌는지 확인. 그다음 `MIC_GAIN` (6-2) |
| `[http] POST -> 401` | `config.h`의 key가 틀렸다 |
| `[http] POST -> 404` | `SUPABASE_URL` 끝에 `/rest/v1/`가 붙어 있다 |
| `[http] POST -> -1` | Wi-Fi가 끊겼거나 URL 오타. 2.4GHz Wi-Fi인지 확인 |
| 웹이 "연결 끊김" | Publications에서 `siren_events`가 켜져 있는지 확인 |
| 웹이 "설정 필요" | `web/config.js`가 비어 있다 |
