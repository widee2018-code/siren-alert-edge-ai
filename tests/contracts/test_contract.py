"""펌웨어가 보내는 필드, 웹이 읽는 컬럼이 schema.sql의 siren_events와 맞는지 대조한다 (docs/contracts/siren_event.md)."""

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "cloud" / "supabase" / "schema.sql"
FIRMWARE = ROOT / "esp32" / "firmware" / "siren_alert" / "siren_alert.ino"
WEB = ROOT / "web" / "app.js"

COLUMN = re.compile(r"^\s+([a-z_]+)\s+(bigint|integer|real|text|timestamptz|boolean)\b(.*)$")


def schema_columns() -> dict[str, str]:
    """컬럼 이름 → 나머지 정의 (not null, default 등)"""
    sql = SCHEMA.read_text()
    body = re.search(r"create table if not exists public\.siren_events \((.*?)\n\);", sql, re.S)
    assert body, "schema.sql에서 siren_events 정의를 찾지 못함"
    cols = {}
    for line in body.group(1).splitlines():
        m = COLUMN.match(line)
        if m:
            cols[m.group(1)] = m.group(3)
    return cols


def firmware_fields() -> set[str]:
    src = FIRMWARE.read_text()
    fields = set(re.findall(r'\\"([a-z_]+)\\":', src))
    assert fields, "siren_alert.ino에서 JSON 필드를 찾지 못함"
    return fields


def test_firmware_fields_exist_in_schema():
    missing = firmware_fields() - schema_columns().keys()
    assert not missing, f"schema.sql에 없는 필드를 보냄: {sorted(missing)}"


def test_firmware_sends_required_columns():
    required = {
        name
        for name, rest in schema_columns().items()
        if "not null" in rest and "default" not in rest and "generated" not in rest
    }
    missing = required - firmware_fields()
    assert not missing, f"필수 컬럼을 보내지 않음: {sorted(missing)}"


def test_web_reads_existing_columns():
    m = re.search(r'\.select\("([^"]+)"\)', WEB.read_text())
    assert m, "app.js에서 select를 찾지 못함"
    used = {c.strip() for c in m.group(1).split(",")}
    missing = used - schema_columns().keys()
    assert not missing, f"schema.sql에 없는 컬럼을 읽음: {sorted(missing)}"
