// config.h 로 복사해 값을 채운다. config.h는 커밋하지 않는다.
#pragma once

#define WIFI_SSID     ""
#define WIFI_PASSWORD ""

// Supabase → Project Settings → API (Data API URL, anon 또는 publishable key)
// service_role(secret) key는 절대 넣지 않는다. RLS가 anon에게 insert·select만 허용한다
#define SUPABASE_URL  "https://xxxxxxxx.supabase.co"
#define SUPABASE_KEY  ""
#define DEVICE_ID     "mic-01"
