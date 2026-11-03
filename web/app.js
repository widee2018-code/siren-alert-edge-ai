// siren_events를 최신순으로 보여 주고, Realtime으로 새 이벤트가 들어오면 화면 깜빡임·진동으로 알린다.
// devices의 last_seen(ESP32 생존 신호)으로 기기가 살아 있는지 보여 준다.
// 오른쪽 위 상태는 이 페이지와 Supabase 사이의 연결이고, 기기 상태는 "기기 상태" 카드에 따로 나온다.
// 읽는 컬럼은 cloud/supabase/schema.sql과 같아야 한다 (tests/contracts가 대조한다).

const HISTORY_LIMIT = 20;
const VIBRATE_PATTERN = [600, 300, 600, 300, 600];
const VIBRATE_EVERY_MS = 3000;
const VIBRATE_FOR_MS = 30000; // 확인을 누르지 않아도 이 시간이 지나면 진동을 멈춘다
const OFFLINE_AFTER_MS = 90000; // ESP32 생존 신호(30초) 3번을 놓치면 응답 없음으로 본다
const OFFLINE_VIBRATE = [200, 100, 200, 100, 200]; // 사이렌 진동과 다르게 짧게
const DEVICE_TICK_MS = 5000; // "n초 전" 표시와 응답 없음 판정을 다시 계산하는 간격

const $ = (id) => document.getElementById(id);
const cfg = window.SIREN_CONFIG || {};
let armed = false;
let wakeLock = null;
let vibrateTimer = null;
let serverOk = false;
const devices = new Map(); // device_id → devices 행
let offlineBefore = new Set();
// 감시할 기기. config.js의 watchDevices에 적은 기기만 표시·경고한다. 비어 있으면 신호를 보낸 모든 기기를 감시한다
const watchList = Array.isArray(cfg.watchDevices) ? cfg.watchDevices.filter(Boolean) : [];

function showError(msg) {
  $("error").textContent = msg;
  $("error").hidden = false;
}

function setStatus(text, state) {
  $("status").textContent = text;
  $("status").className = state;
}

function fmtTime(iso) {
  return new Date(iso).toLocaleString("ko-KR", { month: "numeric", day: "numeric", hour: "2-digit", minute: "2-digit", second: "2-digit" });
}

function eventRow(ev) {
  const li = document.createElement("li");
  const left = document.createElement("span");
  left.textContent = fmtTime(ev.created_at);
  const right = document.createElement("span");
  right.className = "meta";
  right.textContent = `${ev.device_id} · ${Math.round(ev.confidence * 100)}%`;
  li.append(left, right);
  return li;
}

function addToHistory(ev, atTop) {
  const list = $("history");
  const row = eventRow(ev);
  if (atTop) list.prepend(row);
  else list.append(row);
  while (list.children.length > HISTORY_LIMIT) list.lastChild.remove();
  $("empty").hidden = true;
}

function fmtAgo(ms) {
  const s = Math.max(0, Math.round(ms / 1000));
  if (s < 60) return `${s}초 전`;
  if (s < 3600) return `${Math.floor(s / 60)}분 전`;
  return `${Math.floor(s / 3600)}시간 전`;
}

function deviceAge(d) {
  return Date.now() - new Date(d.last_seen).getTime();
}

function renderDevices() {
  const list = $("devices");
  list.replaceChildren();
  const offline = [];
  const ids = watchList.length ? [...watchList] : [...devices.keys()];
  for (const id of ids.sort((a, b) => a.localeCompare(b))) {
    const d = devices.get(id); // 감시 목록에 있지만 한 번도 신호를 보내지 않았으면 없다
    const age = d ? deviceAge(d) : Infinity;
    const on = age < OFFLINE_AFTER_MS;
    if (!on) offline.push(id);

    const li = document.createElement("li");
    const left = document.createElement("span");
    left.className = `dev-state ${on ? "on" : "off"}`;
    left.textContent = `${id} ${on ? "정상" : d ? "응답 없음" : "신호 없음"}`;
    const right = document.createElement("span");
    right.className = "meta";
    right.textContent = d
      ? [`마지막 신호 ${fmtAgo(age)}`, d.rssi != null ? `${d.rssi} dBm` : null, d.fw_version].filter(Boolean).join(" · ")
      : "아직 한 번도 신호를 받지 못함";
    li.append(left, right);
    list.append(li);
  }
  $("no-device").hidden = ids.length > 0;

  const warn = $("device-warn");
  if (!serverOk) {
    warn.textContent = "서버 연결이 끊겨 기기 상태와 사이렌 알림을 받을 수 없습니다.";
  } else if (offline.length) {
    warn.textContent = `${offline.join(", ")} 응답 없음: 지금은 사이렌을 감지하지 못합니다. 전원과 Wi-Fi를 확인하세요.`;
  }
  warn.hidden = serverOk && offline.length === 0;

  // 정상이던 기기가 응답 없음으로 바뀐 순간에 한 번 짧게 진동한다
  if (armed && navigator.vibrate && offline.some((id) => !offlineBefore.has(id))) {
    navigator.vibrate(OFFLINE_VIBRATE);
  }
  offlineBefore = new Set(offline);
}

async function loadDevices(db) {
  const { data, error } = await db.from("devices").select("device_id,last_seen,fw_version,rssi");
  if (error) return showError(`기기 상태를 불러오지 못했습니다: ${error.message}`);
  devices.clear();
  data.forEach((d) => devices.set(d.device_id, d));
  renderDevices();
}

async function requestWakeLock() {
  if (!armed || !("wakeLock" in navigator) || document.visibilityState !== "visible") return;
  try {
    wakeLock = await navigator.wakeLock.request("screen");
  } catch {
    // 지원하지 않거나 거부되면 그냥 넘어간다 (화면이 꺼지면 알림을 못 볼 수 있다)
  }
}

function stopVibrate() {
  clearInterval(vibrateTimer);
  vibrateTimer = null;
  if (navigator.vibrate) navigator.vibrate(0);
}

function alarm(ev) {
  $("ov-meta").textContent = `${fmtTime(ev.created_at)} · ${ev.device_id} · 신뢰도 ${Math.round(ev.confidence * 100)}%`;
  $("overlay").classList.add("show");
  $("ack").focus();
  if (armed && navigator.vibrate) {
    stopVibrate();
    navigator.vibrate(VIBRATE_PATTERN);
    const started = Date.now();
    vibrateTimer = setInterval(() => {
      if (Date.now() - started > VIBRATE_FOR_MS) return stopVibrate();
      navigator.vibrate(VIBRATE_PATTERN);
    }, VIBRATE_EVERY_MS);
  }
}

$("arm").addEventListener("click", () => {
  armed = !armed;
  $("arm").setAttribute("aria-pressed", String(armed));
  $("arm").textContent = armed ? "알림 켜짐 (누르면 끄기)" : "알림 켜기";
  if (armed) {
    // 진동은 사용자가 한 번 누른 뒤에만 허용되므로 여기서 짧게 한 번 울려 둔다
    if (navigator.vibrate) navigator.vibrate(100);
    requestWakeLock();
  } else {
    stopVibrate();
    wakeLock?.release();
    wakeLock = null;
  }
});

$("ack").addEventListener("click", () => {
  $("overlay").classList.remove("show");
  stopVibrate();
});

document.addEventListener("visibilitychange", requestWakeLock);

async function start() {
  if (!cfg.supabaseUrl || !cfg.supabaseKey || !window.supabase) {
    setStatus("설정 필요", "off");
    showError("web/config.js에 Supabase URL과 anon key를 채워 주세요.");
    return;
  }
  const db = window.supabase.createClient(cfg.supabaseUrl, cfg.supabaseKey);

  const { data, error } = await db
    .from("siren_events")
    .select("id,created_at,device_id,confidence")
    .order("created_at", { ascending: false })
    .limit(HISTORY_LIMIT);
  if (error) showError(`기록을 불러오지 못했습니다: ${error.message}`);
  else data.forEach((ev) => addToHistory(ev, false));

  db.channel("siren_alert")
    .on("postgres_changes", { event: "INSERT", schema: "public", table: "siren_events" }, ({ new: ev }) => {
      addToHistory(ev, true);
      alarm(ev);
    })
    .on("postgres_changes", { event: "*", schema: "public", table: "devices" }, ({ new: d }) => {
      if (d && d.device_id) devices.set(d.device_id, d);
      renderDevices();
    })
    .subscribe((status) => {
      if (status === "SUBSCRIBED") {
        serverOk = true;
        setStatus("서버 연결됨", "on");
        loadDevices(db); // 끊긴 동안 놓친 생존 신호를 다시 읽는다
      } else if (status === "CHANNEL_ERROR" || status === "TIMED_OUT" || status === "CLOSED") {
        serverOk = false;
        setStatus("서버 연결 끊김", "off");
        renderDevices();
      }
    });

  setInterval(renderDevices, DEVICE_TICK_MS);
}

start();
