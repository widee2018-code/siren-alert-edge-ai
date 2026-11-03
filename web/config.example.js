// config.js 로 복사해 값을 채운다. config.js는 커밋하지 않는다.
// Supabase → Project Settings → API의 URL과 anon(publishable) key.
// anon key는 브라우저에 공개돼도 되는 키다 (RLS가 insert·select만 허용). service_role key는 절대 넣지 않는다.
window.SIREN_CONFIG = {
  supabaseUrl: "",
  supabaseKey: "",
  // 감시할 ESP32의 DEVICE_ID (config.h와 같게). 이 기기만 표시하고, 90초 넘게 신호가 없으면 경고한다.
  // 비워 두면 신호를 보낸 적 있는 모든 기기를 감시한다 (시험용 기기가 남아 있으면 경고가 계속 뜬다).
  watchDevices: ["mic-01"],
};
