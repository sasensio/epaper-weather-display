#pragma once

// ─── WiFi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID        "your_ssid"
#define WIFI_PASSWORD    "your_password"

// Optional: static IP for faster connection (set USE_STATIC_IP to false to use DHCP)
#define USE_STATIC_IP    false
#define STATIC_IP        "192.168.1.100"
#define GATEWAY          "192.168.1.1"
#define SUBNET           "255.255.255.0"
#define DNS1             "192.168.1.1"
#define DNS2             "8.8.8.8"

// ─── Home Assistant ───────────────────────────────────────────────────────────
#define HA_HOST          "homeassistant.local"
#define HA_PORT          8123
#define HA_TOKEN         "your_long_lived_access_token"
#define HA_WEATHER_ENTITY "weather.home"  // e.g. weather.openweathermap

// ─── Home Assistant MQTT (battery telemetry + auto-discovery) ──────────────
#define ENABLE_HA_MQTT false
#define MQTT_HOST "homeassistant.local"
#define MQTT_PORT 1883
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define MQTT_DISCOVERY_PREFIX "homeassistant"
#define MQTT_TOPIC_PREFIX "epaper-weather-display"
#define MQTT_DEVICE_NAME "Epaper Weather Display"
#define MQTT_CONNECT_TIMEOUT_MS 3000
#define MQTT_PACKET_SIZE 1024

// ─── MQTT OTA Command (retained URL payload) ───────────────────────────────
// Publish a retained firmware URL to <topic_prefix>/<device_id>/ota/command.
#define ENABLE_MQTT_OTA false
#define MQTT_OTA_COMMAND_TOPIC_SUFFIX "ota/command"
#define MQTT_OTA_STATUS_TOPIC_SUFFIX "ota/status"
#define MQTT_OTA_LAST_ERROR_TOPIC_SUFFIX "ota/last_error"
#define MQTT_OTA_COMMAND_WAIT_MS 3500
#define MQTT_OTA_MIN_BATTERY_PERCENT 30
#define MQTT_OTA_ALLOW_INSECURE_TLS true

// ─── E-Paper Pin Definitions (LilyGO TTGO T5 2.13") ─────────────────────────
#define EPD_CS           5
#define EPD_DC           17
#define EPD_RST          16
#define EPD_BUSY         4

// Rotate the final image by 180 degrees (useful to keep USB at the bottom)
#define DISPLAY_ROTATE_180 false

// UI language
#define DISPLAY_LANG_EN 0
#define DISPLAY_LANG_ES 1
#define DISPLAY_LANG_DE 2
#define DISPLAY_LANGUAGE DISPLAY_LANG_EN

// Display layout
#define DISPLAY_DESIGN_1 1
#define DISPLAY_DESIGN_2 2
#define DISPLAY_DESIGN DISPLAY_DESIGN_1

// Development helper: keep device awake and allow live UI position tuning over Serial.
#define ENABLE_UI_TUNING_CONSOLE false

// Development helper: host a web UI for live layout tuning.
#define ENABLE_UI_WEB_TUNER false
#define UI_WEB_TUNER_PORT 80
#define UI_WEB_CLIENT_TIMEOUT_MS 15000
#define UI_WEB_MIN_AWAKE_MS 30000

// LilyGO TTGO T5 user button on GPIO39 (input-only pin, usually active low)
#define USER_BUTTON_PIN 39
#define USER_BUTTON_ACTIVE_LOW true

// Battery measurement (GPIO35 reads divider midpoint; 100k/100k => multiply by 2)
#define ENABLE_BATTERY_MEASUREMENT true
#define BATTERY_SENSE_PIN 35
#define BATTERY_DIVIDER_RATIO 2.0f
#define BATTERY_ADC_SAMPLES 8

// ─── Deep Sleep Intervals ─────────────────────────────────────────────────────
#define SLEEP_DAYTIME_MIN   15   // minutes between updates during the day
#define SLEEP_NIGHTTIME_MIN 60   // minutes between updates at night

// Night hours (24h format)
#define NIGHT_HOUR_START 22
#define NIGHT_HOUR_END    7

// ─── NTP ──────────────────────────────────────────────────────────────────────
#define NTP_SERVER       "pool.ntp.org"
#define NTP_GMT_OFFSET   0      // seconds; adjust for your timezone
#define NTP_DST_OFFSET   3600   // seconds; set to 0 if no DST

// ─── WiFi Timeout ────────────────────────────────────────────────────────────
#define WIFI_TIMEOUT_MS  10000  // 10 seconds

// ─── HA Data Resilience ─────────────────────────────────────────────────────
// If current weather data is unavailable, keep last valid values and only
// start blending toward forecast temperatures after this grace period.
#define HA_INVALID_GRACE_MINUTES 30
#define HA_INTERPOLATE_FORECAST_POINTS 3
#define HA_INTERPOLATION_RAMP_MINUTES 120
#define HA_INTERPOLATION_MAX_STEP_C_PER_CYCLE 1.5f

// ─── Icon Rendering ─────────────────────────────────────────────────────────
// Enable bolder rendering for small icons on e-paper for improved visibility.
#define ICON_RENDER_BOLD true

// ─── SD Card Icons ──────────────────────────────────────────────────────────
// Runtime weather icon loading from /icons/*.mask16 on SD card.
#define ENABLE_SD_ICONS false
// Runtime text font loading from /fonts/*.gfxf on SD card.
#define ENABLE_SD_FONTS false
#define SD_CARD_CS_PIN 13
#define SD_CARD_SCK_PIN 14
#define SD_CARD_MISO_PIN 2
#define SD_CARD_MOSI_PIN 15
 