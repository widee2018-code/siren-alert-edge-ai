# Edge Impulse 사이렌 감지 모델

| 항목 | 값 |
|---|---|
| Edge Impulse 프로젝트 | (만들면 링크를 적는다) |
| 프로젝트 이름 | `siren_detect` → 라이브러리 헤더 `<siren_detect_inferencing.h>`. 이름을 다르게 지으면 `siren_alert.ino`의 `#include`를 바꾼다 |
| 라벨 | `siren`, `noise` (사이렌인지 아닌지만 본다) |
| 입력 | 16000Hz, 창 1000ms, stride 500ms |
| 전처리 | MFE (기본값) |
| 모델 | Classification, 기본 1D Conv |
| 배포 | Arduino library, Quantized (int8), EON Compiler |
| 라이브러리 버전 | (받으면 적는다) |
| 정확도 | (Model testing 결과와 실제 시연 결과를 적는다) |

## 데이터

| 라벨 | 목표량 | 넣을 소리 |
|---|---|---|
| `siren` | 3분 이상 | 한국 구급차·소방차·경찰차 사이렌 (YouTube, Freesound), UrbanSound8K·ESC-50의 `siren` |
| `noise` | 5분 이상 | 조용한 방, 말소리, 음악, 경적, 휴대폰 벨소리, 박수, 키보드. **헷갈리는 소리를 많이 넣을수록 오경보가 준다** |

1. 내려받은 소리는 휴대폰 스피커로 틀고 **XIAO 마이크로 다시 녹음**한다. 시연 때 듣는 소리와 학습 소리가 같아진다.
   거리(30cm, 1m, 2m)와 볼륨을 바꿔 가며 녹음한다.
   ```bash
   # esp32/firmware/mic_recorder를 보드에 올린 뒤
   python tools/record_wav.py --port /dev/cu.usbmodem101 --label siren
   python tools/record_wav.py --port /dev/cu.usbmodem101 --label noise --count 60
   ```
2. 다시 녹음하지 않고 그대로 올릴 파일은 16kHz 모노로 바꾼다: `ffmpeg -i in.wav -ar 16000 -ac 1 siren.xxx.wav`
3. 올리기: `edge-impulse-uploader --category split data/*.wav` (파일 이름의 첫 점 앞이 라벨이 된다)
4. 같은 YouTube 영상에서 나온 조각이 학습·테스트에 섞이면 정확도가 부풀려진다. 가능하면 영상 단위로 테스트 세트를 따로 둔다.

## 받는 법

1. Deployment → Arduino library로 내려받는다.
2. 받은 zip을 이 폴더(`esp32/model/`)에 넣고 커밋한다. CI가 이 zip을 설치해 `siren_alert`를 함께 컴파일한다.
   zip이 없으면 CI는 `mic_recorder`만 컴파일한다.
3. 내 PC에서는 Arduino IDE의 Sketch → Include Library → Add .ZIP Library로 같은 zip을 추가한다.
4. 위 표의 버전·정확도 칸을 채운다.
