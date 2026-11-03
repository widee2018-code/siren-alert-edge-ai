# 진행 가이드: 직접 녹음·학습 (2026-11-03)

사이렌 감지 엣지 AI 알림을 데이터 녹음부터 직접 학습해 완성하는 순서와 방법이다. 이미 학습된 모델을 쓰면 `guide_pretrained.md`를 본다. 위에서부터 차례로 진행하고, 끝낸 단계는 `[x]`로 바꾼다.

## 전체 순서와 시간

| 단계 | 할 일 | 예상 시간 | 상태 |
|---|---|---|---|
| 0 | 저장소 만들기, GitHub push (`main`, `dev`) | - | [x] |
| 1 | Supabase 테이블 만들기, curl로 저장 확인 | 20분 | [ ] |
| 2 | Arduino IDE 준비 | 15분 | [ ] |
| 3 | 데이터 녹음 (siren, noise) | 1시간 | [ ] |
| 4 | Edge Impulse 학습, Arduino 라이브러리 받기 | 1시간 | [ ] |
| 5 | 본 펌웨어 올리기, 판정 값 조정 | 1시간 | [ ] |
| 6 | 웹 알림 페이지 | 30분 | [ ] |
| 7 | 처음부터 끝까지 시험 | 30분 | [ ] |
| 8 | 커밋, CI 확인, `dev → main` 병합 | 30분 | [ ] |
| 9 | 발표 준비 | 남는 시간 | [ ] |

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

## 3. 데이터 녹음

### 3-1. 녹음 펌웨어 올리기

1. `esp32/firmware/mic_recorder/mic_recorder.ino`를 열고 **Upload**.
2. Serial Monitor를 열어 `# ready 16000Hz max 5s`가 보이면 정상이다.
3. **Serial Monitor를 닫는다.** 열려 있으면 다음 단계의 녹음 스크립트가 포트를 열 수 없다.

### 3-2. 녹음

```bash
cd siren-alert-edge-ai
pip install -r tools/requirements.txt

# siren: Enter를 누를 때마다 3초씩 녹음, q로 끝
python tools/record_wav.py --port /dev/cu.usbmodem101 --label siren

# noise: 3초짜리 60개(3분)를 쉬지 않고 녹음
python tools/record_wav.py --port /dev/cu.usbmodem101 --label noise --count 60
```

파일은 `data/siren.<시각>.wav`, `data/noise.<시각>.wav`로 저장된다. 녹음할 때마다 찍히는 **최고 진폭**이 500보다 작으면 "너무 조용함"이 뜬다. 소리를 더 크게 하거나 마이크 가까이에서 다시 녹음한다.

### 3-3. 무엇을 녹음하나

| 라벨 | 목표 | 방법 |
|---|---|---|
| `siren` | 60개 이상 (3분) | YouTube에서 "구급차 사이렌", "소방차 사이렌", "경찰차 사이렌"을 휴대폰으로 틀고 XIAO 마이크로 녹음한다. **영상 3~4개 이상**, 거리(30cm, 1m, 2m)와 볼륨을 바꿔 가며 녹음한다 |
| `noise` | 100개 이상 (5분) | 조용한 방, 내 말소리, 음악, 키보드, 박수, 휴대폰 벨소리, **자동차 경적**(YouTube). 사이렌과 헷갈릴 만한 소리를 많이 넣을수록 오경보가 준다 |

- 사이렌이 아닌 소리는 모두 `noise`에 넣는다.
- 소리 하나가 시작되기 전에 녹음이 끝나지 않도록, 영상을 재생하고 사이렌이 들리기 시작한 뒤 Enter를 누른다.

---

## 4. Edge Impulse 학습

### 4-1. 프로젝트와 업로드

1. https://studio.edgeimpulse.com → **Create new project**. 이름은 **`siren_detect`**로 짓는다.
   이름이 다르면 라이브러리 헤더 이름이 달라져 `siren_alert.ino`의 `#include`를 바꿔야 한다.
2. **Data acquisition → Upload data**
   - 파일: `data/` 폴더의 wav 전부
   - Upload into category: **Automatically split between training and testing**
   - Label: **Infer from filename** (파일 이름의 첫 점 앞이 라벨이 된다)

   CLI를 쓰려면: `npm install -g edge-impulse-cli` 후 `edge-impulse-uploader --category split data/*.wav`
3. 라벨이 `siren`, `noise` 두 개로 들어갔는지, 학습:테스트가 약 80:20인지 확인한다.

### 4-2. Impulse 설정

**Create impulse**

| 칸 | 값 |
|---|---|
| Window size | 1000 ms |
| Window increase | 500 ms |
| Frequency | 16000 Hz |
| Zero-pad data | 체크 |
| Processing block | **Audio (MFE)** |
| Learning block | **Classification** |

**Save Impulse** → **MFE** 탭에서 기본값으로 **Save parameters** → **Generate features**.

### 4-3. 학습

1. **Classifier** 탭에서 기본 설정(1D Conv, Training cycles 100)으로 **Start training**.
2. 결과 확인
   - Accuracy가 **90% 이상**인지 본다.
   - Confusion matrix에서 **noise를 siren으로 잘못 본 비율**이 오경보로 이어진다. 이 값이 크면 noise 데이터를 더 넣는다.
3. **Model testing → Classify all**로 테스트 세트 정확도를 본다. 이 값을 `esp32/model/README.md`의 정확도 칸에 적는다.

### 4-4. 배포

1. **Deployment** → **Arduino library** 검색해서 선택.
2. **Quantized (int8)**, **EON Compiler** 켜기 → **Build**.
3. zip이 내려받아진다. 이 zip을 `esp32/model/`에 복사한다.

---

## 5. 본 펌웨어

1. Arduino IDE: **Sketch → Include Library → Add .ZIP Library** → 방금 받은 zip.
2. 설정 파일 만들기

   ```bash
   cp esp32/firmware/siren_alert/config.example.h esp32/firmware/siren_alert/config.h
   ```

   `config.h`에 Wi-Fi 이름·비밀번호, `SUPABASE_URL`, `SUPABASE_KEY`를 채운다 (1단계 주의 사항 그대로).
3. `esp32/firmware/siren_alert/siren_alert.ino`를 열고 **Upload**. 첫 컴파일은 모델 때문에 몇 분 걸린다.
4. Serial Monitor (115200)에서 확인

   ```
   FW_VERSION 0.1.0
   [boot] model=siren_detect 16000샘플(16000Hz), label=siren, threshold=0.70, confirm=2, cooldown=10000ms
   [wifi] 연결됨 192.168.x.x (RSSI -50 dBm)
   [boot] 준비됨, device=mic-01
   siren=0.02 infer_ms=35 hits=0
   siren=0.01 infer_ms=35 hits=0
   ```

5. 휴대폰으로 사이렌을 틀면 `siren=0.9x`가 이어지다가 다음이 찍혀야 한다.

   ```
   >> 사이렌 확인 (최고 0.95), 기록
   [http] POST -> 201 (1200 ms)
   ```

### 판정 값 조정 (`siren_alert.ino` 위쪽)

| 증상 | 조정 |
|---|---|
| 말소리·음악에도 알림이 온다 (오경보) | `SIREN_THRESHOLD`를 0.8로 올리거나 `CONFIRM_WINDOWS`를 3으로 |
| 사이렌을 틀어도 알림이 안 온다 | `SIREN_THRESHOLD`를 0.6으로 내린다. 그래도 점수가 낮으면 데이터 문제다 (3단계로 돌아가 녹음 추가) |
| 사이렌이 계속 울릴 때 알림이 너무 잦다 | `COOLDOWN_MS`를 20000으로 |

값을 바꾸면 `FW_VERSION`을 올리고, README의 판정 설정 표도 고친다.

---

## 6. 웹 알림 페이지

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
- `http://`로 열면 화면 꺼짐 방지(wake lock)가 동작하지 않는다. 시연 중에는 휴대폰 화면 자동 잠금을 꺼 둔다.

---

## 7. 처음부터 끝까지 시험

| 시험 | 기대 결과 | 결과 |
|---|---|---|
| 조용한 상태로 1분 | 알림 없음 | |
| 말하기, 음악 1분 | 알림 없음 | |
| 경적 소리 | 알림 없음 | |
| 사이렌 1m 거리 | 2~3초 안에 웹 알림, Supabase에 한 줄 | |
| 사이렌 30초 계속 | 10초 간격으로 기록 (쿨다운) | |
| Wi-Fi를 껐다 켜기 | 다시 연결되고 다음 사이렌은 기록됨 | |

결과는 발표 자료에 그대로 쓴다. **학습에 쓰지 않은 사이렌 영상**으로 시험해야 의미가 있다.

---

## 8. 커밋, CI, 병합

평소 작업은 `dev`에서 한다.

```bash
git switch dev
git add esp32/model/*.zip esp32/model/README.md      # 모델 zip은 커밋한다
git commit -m "feat(esp32): siren_detect 모델 v1 추가"
git push
```

- `config.h`, `data/`, `*.wav`는 `.gitignore`에 있어 올라가지 않는다. `git status`로 한 번 더 확인한다.
- push하면 GitHub **Actions**에서 CI가 돈다. 모델 zip이 들어간 뒤에는 `siren_alert 컴파일`이 건너뛰지 않고 통과해야 한다.
- 마지막에 GitHub에서 **Pull requests → New → base: `main` ← compare: `dev`**로 PR을 만들고, CI가 초록불이면 병합한다.

---

## 9. 발표 준비

- **문제**: 청각장애인은 사이렌·화재경보를 듣지 못한다.
- **해결**: ESP32가 직접 소리를 판단한다(클라우드에 소리를 보내지 않으니 프라이버시와 지연 면에서 유리하다). 결과만 DB에 기록하고 휴대폰으로 시각·촉각 알림을 준다.
- **필수 조건 대응**: 모델 on ESP32 / Supabase 로그 / GitHub Actions CI (README 표)
- **숫자**: 모델 정확도, 추론 시간(`infer_ms`), 사이렌 시작 → 웹 알림까지 걸린 시간, 7단계 시험표
- **시연**: 휴대폰 A로 사이렌 재생 → 휴대폰 B 화면이 깜빡임 → Supabase 테이블에 줄이 쌓이는 화면
- **한계와 다음 단계**: 보조 수단일 뿐이다. 다음 단계로 사이렌 종류 구분(화재경보, 구급차, 민방위), 진동 모터 웨어러블

---

## 문제 해결

| 증상 | 해결 |
|---|---|
| 업로드가 안 됨 (`No serial data received`) | XIAO의 **BOOT** 버튼을 누른 채 USB를 꽂고 다시 Upload |
| `record_wav.py`가 포트를 못 엶 | Arduino Serial Monitor를 닫는다 |
| `기기 응답 없음` | 보드에 `mic_recorder`가 올라가 있는지, `--port`가 맞는지 확인 |
| 녹음이 전부 "너무 조용함" | 확장보드(카메라·마이크 보드)가 제대로 끼워졌는지 확인. 그래도 작으면 두 펌웨어의 `MIC_GAIN`을 똑같이 4로 올리고 처음부터 다시 녹음 |
| `siren_detect_inferencing.h: No such file` | zip을 IDE에 추가하지 않았거나 Edge Impulse 프로젝트 이름이 다르다 |
| `마이크(소리)용 모델이 아니다` / `16kHz 모델이 아니다` | Impulse 설정(4-2)을 다시 확인 |
| 부팅 후 계속 재부팅 (`CORRUPT HEAP`) | `build_opt.h`가 스케치 폴더에 있는지 확인 |
| `[http] POST -> 401` | `config.h`의 key가 틀렸다 |
| `[http] POST -> 404` | `SUPABASE_URL` 끝에 `/rest/v1/`가 붙어 있다 |
| `[http] POST -> -1` | Wi-Fi가 끊겼거나 URL 오타. 2.4GHz Wi-Fi인지 확인 (ESP32는 5GHz 불가) |
| 웹이 "연결 끊김" | Publications에서 `siren_events`가 켜져 있는지 확인 |
| 웹이 "설정 필요" | `web/config.js`가 비어 있다 |
