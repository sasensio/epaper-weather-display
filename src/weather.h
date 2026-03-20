#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================================
// Data structures
// ============================================================

struct ForecastEntry {
    char condition[32];
    float tempHigh;
    float tempLow;
    char datetime[32];   // ISO 8601 string from Home Assistant
};

struct WeatherData {
    char condition[32];
    float temperature;
    float humidity;
    float wind_speed;
    float pressure;
    ForecastEntry forecast[5];
    int forecastCount;
};

// ============================================================
// fetchWeather() — query Home Assistant REST API
// Returns true on success.
// ============================================================
bool fetchWeather(WeatherData &data) {
    WiFiClient wifiClient;
    HttpClient http(wifiClient, HA_HOST, HA_PORT);

    String path = "/api/states/";
    path += HA_ENTITY_ID;

    http.beginRequest();
    http.get(path);
    http.sendHeader("Authorization", String("Bearer ") + HA_TOKEN);
    http.sendHeader("Content-Type", "application/json");
    http.endRequest();

    int statusCode = http.responseStatusCode();
    if (statusCode != 200) {
        Serial.printf("[weather] HTTP %d\n", statusCode);
        http.stop();
        return false;
    }

    String body = http.responseBody();
    http.stop();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[weather] JSON error: %s\n", err.c_str());
        return false;
    }

    // Current state
    strlcpy(data.condition, doc["state"] | "unknown", sizeof(data.condition));

    JsonObject attr = doc["attributes"];
    data.temperature = attr["temperature"]  | 0.0f;
    data.humidity    = attr["humidity"]     | 0.0f;
    data.wind_speed  = attr["wind_speed"]   | 0.0f;
    data.pressure    = attr["pressure"]     | 0.0f;

    // Forecast array
    JsonArray fc = attr["forecast"];
    data.forecastCount = 0;
    for (JsonObject entry : fc) {
        if (data.forecastCount >= 5) break;
        ForecastEntry &fe = data.forecast[data.forecastCount++];
        strlcpy(fe.condition, entry["condition"] | "unknown", sizeof(fe.condition));
        fe.tempHigh = entry["temperature"] | 0.0f;
        // Home Assistant uses "templow" (all-lowercase) for forecast minimums.
        fe.tempLow  = entry["templow"]     | fe.tempHigh;
        strlcpy(fe.datetime, entry["datetime"] | "", sizeof(fe.datetime));
    }

    return true;
}
