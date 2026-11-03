// Supabase → Project Settings → API의 URL과 anon(publishable) key.
// anon key는 브라우저에 공개돼도 되는 키다 (RLS가 insert·select만 허용). service_role key는 절대 넣지 않는다.
window.SIREN_CONFIG = {
  supabaseUrl: "",
  supabaseKey: "",
};
