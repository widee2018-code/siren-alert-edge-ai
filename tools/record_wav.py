"""mic_recorder 펌웨어가 올라간 XIAO ESP32S3로 녹음해 WAV로 저장한다 (학습 데이터 수집용).

    pip install -r tools/requirements.txt
    python tools/record_wav.py --port /dev/cu.usbmodem101 --label siren
    python tools/record_wav.py --port /dev/cu.usbmodem101 --label noise --count 60

--count가 없으면 Enter를 누를 때마다 한 개씩 녹음하고, q + Enter로 끝낸다.
--count N이면 쉬지 않고 N개를 이어서 녹음한다 (배경 소음을 길게 모을 때).
파일은 data/<label>.<시각>.wav로 저장한다. 첫 점 앞이 라벨이라
`edge-impulse-uploader --category split data/*.wav`가 라벨을 알아서 붙인다.
"""

import argparse
import array
import pathlib
import time
import wave

import serial

SAMPLE_RATE = 16000
QUIET_PEAK = 500  # 최고 진폭이 이보다 작으면 마이크·볼륨을 의심한다 (16bit 최대 32767)


def record(ser: serial.Serial, seconds: int) -> bytes:
    ser.reset_input_buffer()
    ser.write(f"R{seconds}\n".encode())
    deadline = time.time() + seconds + 5
    while True:
        if time.time() > deadline:
            raise TimeoutError("기기 응답 없음. mic_recorder가 올라갔는지, 포트가 맞는지 확인")
        line = ser.readline().decode(errors="replace").strip()
        if line.startswith("PCM "):
            size = int(line.split()[1])
            break
        if line:
            print(f"  (기기) {line}")
    data = ser.read(size)
    if len(data) != size:
        raise TimeoutError(f"{size}바이트 중 {len(data)}바이트만 받음")
    return data


def save(path: pathlib.Path, pcm: bytes) -> None:
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", required=True, help="예: /dev/cu.usbmodem101, COM5")
    p.add_argument("--label", required=True, help="siren 또는 noise")
    p.add_argument("--seconds", type=int, default=3, help="한 개 길이 (1~5초)")
    p.add_argument("--count", type=int, default=0, help="0이면 Enter마다 하나씩")
    p.add_argument("--out", default="data", help="저장 폴더")
    args = p.parse_args()

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    ser = serial.Serial(args.port, 921600, timeout=1)
    time.sleep(1.5)  # 포트를 열면 보드가 다시 시작될 수 있다
    ser.timeout = args.seconds + 3

    n = 0
    while args.count == 0 or n < args.count:
        if args.count == 0 and input(f"[{args.label} {n}] Enter = 녹음, q = 끝 > ").strip().lower() == "q":
            break
        pcm = record(ser, args.seconds)
        peak = max((abs(s) for s in array.array("h", pcm)), default=0)
        path = out / f"{args.label}.{time.strftime('%Y%m%d-%H%M%S')}-{n:03d}.wav"
        save(path, pcm)
        warn = "  ← 너무 조용함" if peak < QUIET_PEAK else ""
        print(f"  저장 {path} (최고 진폭 {peak}){warn}")
        n += 1
    print(f"{n}개 저장")


if __name__ == "__main__":
    main()
