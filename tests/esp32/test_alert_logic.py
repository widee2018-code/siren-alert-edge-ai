"""펌웨어의 알림 판정(alert_logic.h)을 PC에서 g++로 컴파일해 돌린다. 마이크·모델 없이 연속 확인·쿨다운 규칙만 본다."""

import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SKETCH = ROOT / "esp32" / "firmware" / "siren_alert"
TEST_SRC = pathlib.Path(__file__).with_name("alert_logic_test.cpp")


@pytest.mark.skipif(shutil.which("g++") is None, reason="g++ 없음")
def test_alert_logic(tmp_path):
    exe = tmp_path / "alert_logic_test"
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", f"-I{SKETCH}", str(TEST_SRC), "-o", str(exe)],
        check=True,
    )
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
