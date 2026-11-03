"""펌웨어가 보내는 필드, 웹이 읽는 컬럼이 schema.sql의 테이블과 맞는지 대조한다 (docs/contracts/)."""

import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "cloud" / "supabase" / "schema.sql"
FIRMWARE = ROOT / "esp32" / "firmware" / "siren_alert" / "siren_alert.ino"
WEB = ROOT / "web" / "app.js"

COLUMN = re.compile(r"^\s+([a-z_]+)\s+(bigint|integer|real|text|timestamptz|boolean)\b(.*)$")

# 펌웨어의 JSON 형식 상수 → 그 JSON을 받는 테이블
PAYLOADS = {"EVENT_JSON": "siren_events", "HEARTBEAT_JSON": "devices"}


def schema_columns(table: str) -> dict[str, str]:
    """컬럼 이름 → 나머지 정의 (not null, default 등)"""
    sql = SCHEMA.read_text()
    body = re.search(rf"create table if not exists public\.{table} \((.*?)\n\);", sql, re.S)
    assert body, f"schema.sql에서 {table} 정의를 찾지 못함"
    cols = {}
    for line in body.group(1).splitlines():
        m = COLUMN.match(line)
        if m:
            cols[m.group(1)] = m.group(3)
    return cols


def firmware_fields(const: str) -> set[str]:
    src = FIRMWARE.read_text()
    m = re.search(rf"static const char {const}\[\] =(.*?);", src, re.S)
    assert m, f"siren_alert.ino에서 {const}를 찾지 못함"
    fields = set(re.findall(r'\\"([a-z_]+)\\":', m.group(1)))
    assert fields, f"{const}에서 JSON 필드를 찾지 못함"
    return fields


def web_selects() -> list[tuple[str, set[str]]]:
    pairs = re.findall(r'\.from\("(\w+)"\)\s*\.select\("([^"]+)"\)', WEB.read_text())
    assert pairs, "app.js에서 .from(...).select(...)를 찾지 못함"
    return [(table, {c.strip() for c in cols.split(",")}) for table, cols in pairs]


@pytest.mark.parametrize("const,table", PAYLOADS.items())
def test_firmware_fields_exist_in_schema(const, table):
    missing = firmware_fields(const) - schema_columns(table).keys()
    assert not missing, f"{const}가 {table}에 없는 필드를 보냄: {sorted(missing)}"


@pytest.mark.parametrize("const,table", PAYLOADS.items())
def test_firmware_sends_required_columns(const, table):
    required = {
        name
        for name, rest in schema_columns(table).items()
        if "not null" in rest or "primary key" in rest
        if "default" not in rest and "generated" not in rest
    }
    missing = required - firmware_fields(const)
    assert not missing, f"{const}가 {table}의 필수 컬럼을 보내지 않음: {sorted(missing)}"


def test_web_reads_existing_columns():
    tables = {table for table, _ in web_selects()}
    assert tables == {"siren_events", "devices"}, f"웹이 읽는 테이블: {sorted(tables)}"
    for table, used in web_selects():
        missing = used - schema_columns(table).keys()
        assert not missing, f"app.js가 {table}에 없는 컬럼을 읽음: {sorted(missing)}"


def test_heartbeat_last_seen_set_by_db():
    """last_seen은 ESP32가 보내지 않고 DB 트리거가 채운다. 트리거가 없으면 upsert 때 갱신되지 않는다"""
    assert "last_seen" not in firmware_fields("HEARTBEAT_JSON")
    sql = SCHEMA.read_text()
    assert re.search(r"before insert or update on public\.devices", sql), "devices last_seen 트리거 없음"
