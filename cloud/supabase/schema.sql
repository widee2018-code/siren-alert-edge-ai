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

-- 웹 실시간 알림
do $$
begin
  if not exists (
    select 1 from pg_publication_tables
    where pubname = 'supabase_realtime'
      and schemaname = 'public' and tablename = 'siren_events'
  ) then
    alter publication supabase_realtime add table public.siren_events;
  end if;
end $$;
