// siren_events를 최신순으로 보여 주고, Realtime으로 새 이벤트가 들어오면 화면 깜빡임·진동으로 알린다.
// 읽는 컬럼은 cloud/supabase/schema.sql과 같아야 한다 (tests/contracts가 대조한다).

const HISTORY_LIMIT = 20;
const VIBRATE_PATTERN = [600, 300, 600, 300, 600];
const VIBRATE_EVERY_MS = 3000;
const VIBRATE_FOR_MS = 30000; // 확인을 누르지 않아도 이 시간이 지나면 진동을 멈춘다

const $ = (id) => document.getElementById(id);
const cfg = window.SIREN_CONFIG || {};
let armed = false;
let wakeLock = null;
let vibrateTimer = null;

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

  db.channel("siren_events")
    .on("postgres_changes", { event: "INSERT", schema: "public", table: "siren_events" }, ({ new: ev }) => {
      addToHistory(ev, true);
      alarm(ev);
    })
    .subscribe((status) => {
      if (status === "SUBSCRIBED") setStatus("실시간 연결됨", "on");
      else if (status === "CHANNEL_ERROR" || status === "TIMED_OUT" || status === "CLOSED") setStatus("연결 끊김", "off");
    });
}

start();
