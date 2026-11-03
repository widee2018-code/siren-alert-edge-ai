-- Supabase SQL Editor에 붙여 넣어 실행한다. 여러 번 실행해도 같은 결과가 되도록 작성한다.
-- ESP32가 보내는 필드는 docs/contracts/siren_event.md (tests/contracts가 이 파일과 펌웨어를 대조한다).

create table if not exists public.siren_events (
  id          bigint generated always as identity primary key,
  created_at  timestamptz not null default now(),
  device_id   text not null check (char_length(device_id) between 1 and 64),
  label       text not null default 'siren' check (char_length(label) <= 32),
  confidence  real not null check (confidence between 0 and 1),
  infer_ms    integer check (infer_ms >= 0),
  windows     integer not null check (windows >= 1),
  fw_version  text check (char_length(fw_version) <= 32),
  rssi        integer
);

-- 웹은 최신순으로 조회한다
create index if not exists siren_events_created_at_idx
  on public.siren_events (created_at desc);

-- ESP32와 웹은 같은 anon key를 쓴다. anon은 넣기와 읽기만 할 수 있고 고치기·지우기는 못 한다.
-- (anon key는 공개돼도 되는 키다. 대신 누구나 가짜 이벤트를 넣을 수 있으니 시연용 범위로 둔다)
alter table public.siren_events enable row level security;

drop policy if exists "anon insert siren_events" on public.siren_events;
create policy "anon insert siren_events"
  on public.siren_events for insert
  to anon
  with check (true);

drop policy if exists "anon read siren_events" on public.siren_events;
create policy "anon read siren_events"
  on public.siren_events for select
  to anon
  using (true);

-- 기기 생존 신호. 기기마다 한 줄을 ESP32가 30초마다 덮어쓴다 (docs/contracts/device_heartbeat.md).
-- 웹은 last_seen이 90초 넘게 지나면 "기기 응답 없음"을 띄운다.
create table if not exists public.devices (
  device_id   text primary key check (char_length(device_id) between 1 and 64),
  last_seen   timestamptz not null default now(),
  fw_version  text check (char_length(fw_version) <= 32),
  rssi        integer,
  uptime_s    bigint check (uptime_s >= 0)
);

-- last_seen은 ESP32가 아니라 DB가 받은 시각으로 채운다 (ESP32에는 시계가 없다).
-- upsert의 update에서는 default가 적용되지 않으므로 트리거로 매번 now()를 넣는다.
create or replace function public.devices_touch_last_seen()
returns trigger
language plpgsql
set search_path = ''
as $$
begin
  new.last_seen := now();
  return new;
end $$;

drop trigger if exists devices_touch_last_seen on public.devices;
create trigger devices_touch_last_seen
  before insert or update on public.devices
  for each row execute function public.devices_touch_last_seen();

-- anon은 넣기·고치기(upsert)·읽기만 한다. 지우기는 못 한다 (시연용 RLS: 누구나 어느 기기 줄이든 고칠 수 있다)
alter table public.devices enable row level security;
grant select, insert, update on public.devices to anon;

drop policy if exists "anon insert devices" on public.devices;
create policy "anon insert devices"
  on public.devices for insert
  to anon
  with check (true);

drop policy if exists "anon update devices" on public.devices;
create policy "anon update devices"
  on public.devices for update
  to anon
  using (true)
  with check (true);

drop policy if exists "anon read devices" on public.devices;
create policy "anon read devices"
  on public.devices for select
  to anon
  using (true);

-- 웹 실시간 알림 (사이렌 이벤트, 기기 상태)
do $$
declare
  t text;
begin
  foreach t in array array['siren_events', 'devices'] loop
    if not exists (
      select 1 from pg_publication_tables
      where pubname = 'supabase_realtime'
        and schemaname = 'public' and tablename = t
    ) then
      execute format('alter publication supabase_realtime add table public.%I', t);
    end if;
  end loop;
end $$;
