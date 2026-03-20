#pragma once

#include <Arduino.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "config.h"

struct WeatherData {
    String condition;
    float  temperature;
    float  apparent_temperature;
    float  humidity;
    float  wind_speed;
    float  pressure;
    bool   valid = false;
};

struct ForecastEntry {
    String condition;
    float  temperature;
    String datetime;  // ISO 8601
};

// Keep enough hourly points so the renderer can choose a start hour first,
// then downsample in 2-hour increments.
static const int MAX_FORECAST = 14;

struct WeatherResult {
    WeatherData    current;
    ForecastEntry  forecast[MAX_FORECAST];
    int            forecastCount = 0;
    bool           currentDataValid = false;
    bool           forecastDataValid = false;
};

inline bool isUnavailableWeatherState(const String &stateRaw) {
    String state = stateRaw;
    state.trim();
    state.toLowerCase();
    return state.length() == 0
        || state == "unavailable"
        || state == "unknown"
        || state == "none"
        || state == "null";
}

inline bool jsonNumberToFloat(JsonVariantConst value, float &out) {
    if (value.is<float>()) {
        out = value.as<float>();
        return true;
    }
    if (value.is<int>()) {
        out = (float)value.as<int>();
        return true;
    }
    if (value.is<long>()) {
        out = (float)value.as<long>();
        return true;
    }
    if (value.is<const char*>()) {
        const char *text = value.as<const char*>();
        if (!text) return false;
        char *endPtr = nullptr;
        float parsed = strtof(text, &endPtr);
        if (endPtr == text || (endPtr && *endPtr != '\0')) {
            return false;
        }
        out = parsed;
        return true;
    }
    return false;
}

inline void parseForecastArray(JsonArray arr, WeatherResult &result) {
    result.forecastCount = 0;
    for (JsonObject entry : arr) {
        if (result.forecastCount >= MAX_FORECAST) break;
        float temp = 0.0f;
        bool hasTemp = jsonNumberToFloat(entry["temperature"], temp)
                    || jsonNumberToFloat(entry["templow"], temp);
        String condition = entry["condition"].as<String>();
        String datetime = entry["datetime"].as<String>();
        if (!hasTemp || datetime.length() == 0 || isUnavailableWeatherState(condition)) {
            continue;
        }

        ForecastEntry &fe = result.forecast[result.forecastCount++];
        fe.condition = condition;
        fe.temperature = temp;
        fe.datetime = datetime;
    }
    result.forecastDataValid = result.forecastCount > 0;
}

inline void parseForecastVariant(JsonVariant variant, WeatherResult &result) {
    if (variant.is<JsonArray>()) {
        parseForecastArray(variant.as<JsonArray>(), result);
    }
}

inline void parseForecastResponse(JsonDocument &doc, WeatherResult &result) {
    JsonVariant root = doc.as<JsonVariant>();

    if (root.is<JsonArray>()) {
        JsonArray arr = root.as<JsonArray>();
        if (!arr.isNull() && arr.size() > 0) {
            JsonObject first = arr[0];
            parseForecastVariant(first["forecast"], result);
            if (result.forecastCount == 0) {
                parseForecastVariant(first[HA_WEATHER_ENTITY]["forecast"], result);
            }
            if (result.forecastCount == 0) {
                parseForecastVariant(first["service_response"][HA_WEATHER_ENTITY]["forecast"], result);
            }
        }
    }

    if (result.forecastCount == 0) {
        parseForecastVariant(root["forecast"], result);
    }
    if (result.forecastCount == 0) {
        parseForecastVariant(root[HA_WEATHER_ENTITY]["forecast"], result);
    }
    if (result.forecastCount == 0) {
        parseForecastVariant(root["service_response"][HA_WEATHER_ENTITY]["forecast"], result);
    }
}

inline bool haGetJson(const String &path, JsonDocument &doc, int &statusCode) {
    WiFiClient wifi;
    HttpClient client(wifi, HA_HOST, HA_PORT);

    client.beginRequest();
    client.get(path);
    client.sendHeader("Authorization", String("Bearer ") + HA_TOKEN);
    client.sendHeader("Content-Type", "application/json");
    client.endRequest();

    statusCode = client.responseStatusCode();
    String body = client.responseBody();
    client.stop();

    if (statusCode != 200) {
        return false;
    }

    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[weather] JSON parse error on %s: %s\n", path.c_str(), err.c_str());
        return false;
    }

    return true;
}

inline bool haPostJson(const String &path, const String &payload, JsonDocument &doc, int &statusCode) {
    WiFiClient wifi;
    HttpClient client(wifi, HA_HOST, HA_PORT);

    client.beginRequest();
    client.post(path);
    client.sendHeader("Authorization", String("Bearer ") + HA_TOKEN);
    client.sendHeader("Content-Type", "application/json");
    client.sendHeader("Content-Length", payload.length());
    client.beginBody();
    client.print(payload);
    client.endRequest();

    statusCode = client.responseStatusCode();
    String body = client.responseBody();
    client.stop();

    if (statusCode != 200 && statusCode != 201) {
        Serial.printf("[weather] POST %s failed (%d): %s\n", path.c_str(), statusCode, body.c_str());
        return false;
    }

    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[weather] JSON parse error on %s: %s\n", path.c_str(), err.c_str());
        Serial.printf("[weather] Response body: %s\n", body.c_str());
        return false;
    }

    return true;
}

// Fetches weather data from Home Assistant REST API.
// Returns true on success, false on any error.
inline bool fetchWeather(WeatherResult &result) {
    result.forecastCount = 0;
    result.currentDataValid = false;
    result.forecastDataValid = false;

    int statusCode = 0;
    JsonDocument stateDoc;
    String statePath = "/api/states/" + String(HA_WEATHER_ENTITY);
    if (!haGetJson(statePath, stateDoc, statusCode)) {
        Serial.printf("[weather] State fetch failed (%d)\n", statusCode);
        return false;
    }

    // Current conditions (semantic validity check to avoid turning invalid data into 0C)
    result.current.condition   = stateDoc["state"].as<String>();
    JsonObject attrs           = stateDoc["attributes"];
    float currentTemp = 0.0f;
    float apparentTemp = 0.0f;
    float humidity = 0.0f;
    float windSpeed = 0.0f;
    float pressure = 0.0f;
    bool hasCurrentTemp = jsonNumberToFloat(attrs["temperature"], currentTemp);
    bool hasApparentTemp = jsonNumberToFloat(attrs["apparent_temperature"], apparentTemp);
    bool stateValid = !isUnavailableWeatherState(result.current.condition);

    (void)jsonNumberToFloat(attrs["humidity"], humidity);
    (void)jsonNumberToFloat(attrs["wind_speed"], windSpeed);
    (void)jsonNumberToFloat(attrs["pressure"], pressure);

    result.current.temperature = hasCurrentTemp ? currentTemp : 0.0f;
    result.current.apparent_temperature = hasApparentTemp ? apparentTemp : result.current.temperature;
    result.current.humidity = humidity;
    result.current.wind_speed = windSpeed;
    result.current.pressure = pressure;
    result.current.valid = stateValid && hasCurrentTemp;
    result.currentDataValid = result.current.valid;

    if (!result.currentDataValid) {
        Serial.printf("[weather] Current data invalid (state='%s', hasTemp=%s)\n",
                      result.current.condition.c_str(), hasCurrentTemp ? "yes" : "no");
    }

    // Prefer hourly forecast from the dedicated service.
    if (result.forecastCount == 0) {
        JsonDocument forecastDoc;
        String forecastPayload = String("{\"entity_id\":\"") + HA_WEATHER_ENTITY + "\",\"type\":\"hourly\"}";
        if (haPostJson("/api/services/weather/get_forecasts?return_response", forecastPayload, forecastDoc, statusCode)) {
            parseForecastResponse(forecastDoc, result);
        } else {
            Serial.printf("[weather] Forecast service failed (%d)\n", statusCode);
        }
    }

    // Fallback for older/custom setups that still expose a direct forecast endpoint.
    if (result.forecastCount == 0) {
        JsonDocument forecastDoc;
        String fallbackPath = "/api/weather/forecast?entity_id=" + String(HA_WEATHER_ENTITY) + "&type=hourly";
        if (haGetJson(fallbackPath, forecastDoc, statusCode)) {
            parseForecastResponse(forecastDoc, result);
        }
    }

    // Final fallback for older integrations that only expose forecast in state attributes.
    if (result.forecastCount == 0 && attrs["forecast"].is<JsonArray>()) {
        parseForecastArray(attrs["forecast"].as<JsonArray>(), result);
    }

    result.forecastDataValid = result.forecastCount > 0;

    Serial.printf("[weather] Current valid=%s, forecast entries=%d\n",
                  result.currentDataValid ? "yes" : "no",
                  result.forecastCount);
    return true;
}
