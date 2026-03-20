#pragma once

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include "config.h"
#include "weather.h"

// ============================================================
// Display instance — GxEPD2_213_B74 (250 x 122, GDEH0213B74)
// ============================================================
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(
    GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ============================================================
// conditionIcon() — map HA condition string to an ASCII symbol
// ============================================================
static const char* conditionIcon(const char* cond) {
    if (strstr(cond, "sunny"))           return "*";
    if (strstr(cond, "clear"))           return "*";
    if (strstr(cond, "partlycloudy"))    return "~";
    if (strstr(cond, "cloudy"))          return "=";
    if (strstr(cond, "fog"))             return "-";
    if (strstr(cond, "hail"))            return "o";
    if (strstr(cond, "lightning"))       return "!";
    if (strstr(cond, "pouring"))         return "v";
    if (strstr(cond, "rainy"))           return ":";
    if (strstr(cond, "snowy"))           return "#";
    if (strstr(cond, "wind"))            return ">";
    return "?";
}

// ============================================================
// shortDay() — extract 3-letter weekday from ISO 8601 string
// e.g. "2024-03-15T12:00:00+00:00" -> "Fri"
// ============================================================
static const char* shortDay(const char* iso) {
    static const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    struct tm t = {};
    // Parse date part only
    int y, m, d;
    if (sscanf(iso, "%d-%d-%d", &y, &m, &d) == 3) {
        t.tm_year = y - 1900;
        t.tm_mon  = m - 1;
        t.tm_mday = d;
        mktime(&t);
        return days[t.tm_wday];
    }
    return "---";
}

// ============================================================
// batteryPercent() — convert ADC mV to percentage
// ============================================================
static int batteryPercent(int mv) {
    if (mv >= BATT_FULL_MV)  return 100;
    if (mv <= BATT_EMPTY_MV) return 0;
    return (int)(((float)(mv - BATT_EMPTY_MV) /
                  (float)(BATT_FULL_MV - BATT_EMPTY_MV)) * 100.0f);
}

// ============================================================
// drawWeather() — render current weather + forecast
// ============================================================
void drawWeather(const WeatherData &w, int battMv) {
    display.setRotation(1);   // Landscape 250 x 122
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // ---- Current weather (left half) ----
        display.setFont(&FreeMonoBold9pt7b);
        display.setCursor(2, 18);
        display.printf("%s", conditionIcon(w.condition));

        display.setFont(&FreeSans9pt7b);
        display.setCursor(22, 18);
        display.printf("%.1f\xB0C", w.temperature);

        display.setCursor(2, 36);
        display.printf("Hum:%.0f%%", w.humidity);

        display.setCursor(2, 52);
        display.printf("Wind:%.1f", w.wind_speed);

        display.setCursor(2, 68);
        display.printf("Pres:%.0f", w.pressure);

        // Battery
        if (battMv > 0) {
            int pct = batteryPercent(battMv);
            display.setCursor(2, 110);
            display.printf("Bat:%d%%", pct);
        }

        // Divider
        display.drawFastVLine(125, 0, 122, GxEPD_BLACK);

        // ---- Forecast (right half) ----
        for (int i = 0; i < w.forecastCount && i < 5; i++) {
            int x = 128 + (i * 24);
            int y = 14;
            display.setFont(&FreeSans9pt7b);
            display.setCursor(x, y);
            display.printf("%s", shortDay(w.forecast[i].datetime));
            display.setCursor(x, y + 16);
            display.printf("%s", conditionIcon(w.forecast[i].condition));
            display.setCursor(x, y + 32);
            display.printf("%.0f", w.forecast[i].tempHigh);
            display.setCursor(x, y + 48);
            display.printf("%.0f", w.forecast[i].tempLow);
        }

        // Timestamp (bottom right)
        time_t now = time(nullptr);
        struct tm *ti = localtime(&now);
        char ts[16];
        strftime(ts, sizeof(ts), "%H:%M", ti);
        display.setFont(&FreeSans9pt7b);
        display.setCursor(128, 110);
        display.printf("Upd:%s", ts);

    } while (display.nextPage());

    // Power down the display controller to save ~0.1 mA standby current.
    display.hibernate();
}

// ============================================================
// drawError() — show an error message on the screen
// ============================================================
void drawError(const char* msg) {
    display.setRotation(1);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeSans9pt7b);
        display.setCursor(4, 30);
        display.print("ERROR:");
        display.setCursor(4, 50);
        display.print(msg);
    } while (display.nextPage());
    display.hibernate();
}
