#pragma once

// ============================================================
// WiFi credentials
// ============================================================
#define WIFI_SSID       "your_ssid"
#define WIFI_PASSWORD   "your_password"

// Optional static IP — speeds up DHCP-less connection (~1 s faster).
// Comment out USE_STATIC_IP to use DHCP instead.
#define USE_STATIC_IP
#ifdef USE_STATIC_IP
  #define STATIC_IP       "192.168.1.200"
  #define STATIC_GATEWAY  "192.168.1.1"
  #define STATIC_SUBNET   "255.255.255.0"
  #define STATIC_DNS1     "192.168.1.1"
  #define STATIC_DNS2     "8.8.8.8"
#endif

// ============================================================
// Home Assistant
// ============================================================
#define HA_HOST         "homeassistant.local"
#define HA_PORT         8123
#define HA_TOKEN        "your_long_lived_access_token"
#define HA_ENTITY_ID    "weather.home"

// ============================================================
// NTP
// ============================================================
#define NTP_SERVER      "pool.ntp.org"
#define TIMEZONE        "CET-1CEST,M3.5.0,M10.5.0/3"   // Adjust to your TZ

// ============================================================
// E-paper pin definitions (LilyGO TTGO T5 2.13")
// ============================================================
#define EPD_CS    5
#define EPD_DC    17
#define EPD_RST   16
#define EPD_BUSY  4

// ============================================================
// Battery ADC (GPIO 35 on LilyGO TTGO T5 — 1:2 voltage divider)
// ============================================================
#define BATT_ADC_PIN      35
#define BATT_ADC_SAMPLES  8      // Oversample to reduce noise
#define BATT_FULL_MV      4200   // mV at 100 %
#define BATT_EMPTY_MV     3300   // mV at 0 %

// ============================================================
// Deep sleep / update intervals
// ============================================================
#define SLEEP_INTERVAL_DAY_S    (15 * 60)   // 15 min during the day
#define SLEEP_INTERVAL_NIGHT_S  (60 * 60)   // 60 min at night
#define NIGHT_START_HOUR        22           // 22:00 night begins
#define NIGHT_END_HOUR           7           //  7:00 night ends

// ============================================================
// NTP re-sync cadence
// Keep RTC time accurate without syncing on every wake.
// Sync is only performed every NTP_SYNC_INTERVAL boots.
// ============================================================
#define NTP_SYNC_INTERVAL  8    // re-sync every 8th wake (≈ 2 h daytime)

// ============================================================
// CPU frequency (MHz) — reduce from 240 to 80 to save ~30 % CPU power
// ============================================================
#define CPU_FREQ_MHZ  80

// ============================================================
// WiFi TX power — lower values save power (range 2–20 dBm)
// 17 dBm is the default; 11 dBm cuts TX current noticeably
// while still reaching a typical home router.
// ============================================================
#define WIFI_TX_POWER  WIFI_POWER_11dBm

// ============================================================
// WiFi connection timeout
// ============================================================
#define WIFI_TIMEOUT_MS  10000

// ============================================================
// Fallback sleep duration when active time exceeded the target
// interval (e.g. slow API, long NTP sync).  60 s gives the
// system a brief pause before retrying.
// ============================================================
#define FALLBACK_SLEEP_S  60
