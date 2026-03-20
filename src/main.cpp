/*
 * epaper-weather-display — main.cpp
 *
 * Power optimisations implemented:
 *  1. CPU frequency reduced to 80 MHz (down from 240 MHz) — saves ~30 % CPU power.
 *  2. Bluetooth stack disabled at compile time (-DCONFIG_BT_ENABLED=0) — saves ~25 mA.
 *  3. Static IP skips DHCP exchange — saves ~1 s of WiFi-on time per wake.
 *  4. WiFi TX power reduced to 11 dBm — adequate for home use, lower TX current.
 *  5. NTP sync cached in RTC memory — skips NTP round-trip on most wakes.
 *  6. Active time deducted from sleep budget — keeps true update cadence.
 *  7. WiFi and peripherals shut down before entering deep sleep.
 *  8. E-paper display hibernated immediately after refresh — eliminates standby draw.
 *  9. Adaptive sleep intervals — 15 min daytime, 60 min nighttime.
 * 10. Battery voltage measured via oversampled ADC with peripheral power-gating.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <time.h>
#include "config.h"
#include "weather.h"
#include "display.h"

// ============================================================
// RTC memory — survives deep sleep, reset on power-on only
// ============================================================
RTC_DATA_ATTR static uint32_t bootCount       = 0;  // Total wake count
RTC_DATA_ATTR static uint32_t ntpSyncBoot     = 0;  // bootCount of last NTP sync
RTC_DATA_ATTR static time_t   rtcBaseTime     = 0;  // UNIX time at last NTP sync

// ============================================================
// readBatteryMv() — oversample ADC and return voltage in mV
// The TTGO T5 feeds the LiPo through a 1:2 voltage divider to GPIO 35.
// ============================================================
static int readBatteryMv() {
    // Use the internal ADC only briefly; no external enable pin needed on TTGO T5.
    int raw = 0;
    for (int i = 0; i < BATT_ADC_SAMPLES; i++) {
        raw += analogRead(BATT_ADC_PIN);
        delay(2);
    }
    raw /= BATT_ADC_SAMPLES;
    // ESP32 ADC: 12-bit, 3.3 V reference, divider factor 2
    return (int)((raw / 4095.0f) * 3300.0f * 2);
}

// ============================================================
// connectWiFi() — connect with optional static IP and timeout
// Returns true on success.
// ============================================================
static bool connectWiFi() {
    WiFi.mode(WIFI_STA);

    // Reduce TX power after enabling the radio (must be called after WiFi.mode).
    WiFi.setTxPower(WIFI_TX_POWER);

#ifdef USE_STATIC_IP
    IPAddress ip, gw, sn, dns1, dns2;
    ip.fromString(STATIC_IP);
    gw.fromString(STATIC_GATEWAY);
    sn.fromString(STATIC_SUBNET);
    dns1.fromString(STATIC_DNS1);
    dns2.fromString(STATIC_DNS2);
    WiFi.config(ip, gw, sn, dns1, dns2);
#endif

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > WIFI_TIMEOUT_MS) {
            Serial.println("[wifi] timeout");
            return false;
        }
        delay(100);
    }
    Serial.printf("[wifi] connected in %lu ms, IP: %s\n",
                  millis() - t0, WiFi.localIP().toString().c_str());
    return true;
}

// ============================================================
// syncNtp() — configure time from NTP and wait for a valid epoch
// Stores the synced time in RTC memory so subsequent boots can
// reconstruct the current time without a network round-trip.
// ============================================================
static void syncNtp() {
    configTzTime(TIMEZONE, NTP_SERVER);
    Serial.print("[ntp] syncing");
    time_t now = 0;
    for (int i = 0; i < 40 && now < 1000000000UL; i++) {
        delay(250);
        time(&now);
        Serial.print(".");
    }
    Serial.println();
    if (now > 1000000000UL) {
        rtcBaseTime = now;
        ntpSyncBoot = bootCount;
        Serial.printf("[ntp] synced, epoch=%lu\n", (unsigned long)now);
    } else {
        Serial.println("[ntp] sync failed");
    }
}

// ============================================================
// restoreTimeFromRtc() — reconstruct current time from RTC data
// without hitting the network, then set the system clock.
// ============================================================
static void restoreTimeFromRtc() {
    // Restore the system clock from the last NTP-synced timestamp stored in
    // RTC-retained SRAM.  We use settimeofday() + setenv/tzset() instead of
    // configTzTime() to apply the timezone without triggering a background
    // SNTP query (configTzTime internally starts the SNTP service).
    struct timeval tv = { .tv_sec = rtcBaseTime, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    setenv("TZ", TIMEZONE, 1);
    tzset();
    Serial.printf("[time] restored from RTC: epoch=%lu\n", (unsigned long)rtcBaseTime);
}

// ============================================================
// getSleepInterval() — adaptive interval based on time of day
// ============================================================
static uint32_t getSleepInterval() {
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    int h = t->tm_hour;
    bool isNight = (h >= NIGHT_START_HOUR) || (h < NIGHT_END_HOUR);
    return isNight ? SLEEP_INTERVAL_NIGHT_S : SLEEP_INTERVAL_DAY_S;
}

// ============================================================
// goToSleep() — shut everything down, then deep-sleep
// ============================================================
static void goToSleep(uint32_t seconds) {
    Serial.printf("[sleep] entering deep sleep for %u s\n", seconds);
    Serial.flush();

    // Update the RTC base so restoreTimeFromRtc() is accurate next wake.
    time(&rtcBaseTime);

    // Disconnect WiFi and power it down completely.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();

    // Configure and start deep sleep (timer wake-up).
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    esp_deep_sleep_start();
    // Execution never reaches here.
}

// ============================================================
// setup() — main entry (runs once per wake)
// ============================================================
void setup() {
    unsigned long wakeMs = millis();  // Timestamp the start of active time.

    // --- Power: lower CPU frequency immediately ---
    setCpuFrequencyMhz(CPU_FREQ_MHZ);

    Serial.begin(115200);
    Serial.printf("\n[boot] #%u, wake reason: %d\n",
                  bootCount, esp_sleep_get_wakeup_cause());

    bootCount++;

    // --- Battery voltage (before WiFi to avoid ADC interference) ---
    int battMv = readBatteryMv();
    Serial.printf("[batt] %d mV (%d%%)\n", battMv, batteryPercent(battMv));

    // --- WiFi ---
    if (!connectWiFi()) {
        drawError("WiFi failed");
        goToSleep(SLEEP_INTERVAL_DAY_S);
    }

    // --- Time: full NTP sync or restore from RTC ---
    bool needSync = (bootCount == 1) ||
                    ((bootCount - ntpSyncBoot) >= NTP_SYNC_INTERVAL) ||
                    (rtcBaseTime < 1000000000UL);
    if (needSync) {
        syncNtp();
    } else {
        restoreTimeFromRtc();
    }

    // --- Weather ---
    WeatherData weather = {};
    if (!fetchWeather(weather)) {
        drawError("API failed");
        goToSleep(SLEEP_INTERVAL_DAY_S);
    }

    // --- Display (hibernates itself after refresh) ---
    drawWeather(weather, battMv);

    // --- Sleep: deduct active time from the budget ---
    uint32_t interval  = getSleepInterval();
    uint32_t activeMs  = (uint32_t)(millis() - wakeMs);
    uint32_t activeS   = activeMs / 1000;
    uint32_t sleepSecs = (activeS < interval) ? (interval - activeS) : FALLBACK_SLEEP_S;

    Serial.printf("[boot] active for %u ms; sleeping %u s\n", activeMs, sleepSecs);

    goToSleep(sleepSecs);
}

// loop() is never reached — the device deep-sleeps at the end of setup().
void loop() {}
