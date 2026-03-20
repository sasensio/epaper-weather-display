#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sleep.h>
#include <time.h>
#include <Preferences.h>
#include <PubSubClient.h>
#if ENABLE_HA_MQTT && ENABLE_MQTT_OTA
#include <HTTPUpdate.h>
#endif

#include "config.h"
#include "weather.h"
#include "display.h"

#if ENABLE_UI_WEB_TUNER
#include <WebServer.h>
#include <ArduinoJson.h>
#endif

// Survives deep sleep in RTC memory
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR uint32_t gCurrentDataInvalidMinutes = 0;

// Forward declarations used by tuning helpers defined before core function bodies.
static bool connectWiFi();
static int syncAndGetHour();
static uint64_t getSleepInterval(int hour);
static uint32_t getSleepIntervalMinutes(int hour);
static void publishBatteryToMqtt();
static bool checkAndHandleMqttOta();

static float gBatteryVoltage = -1.0f;
static int gBatteryPercent = -1;

#if ENABLE_HA_MQTT
static WiFiClient gMqttNetClient;
static PubSubClient gMqttClient(gMqttNetClient);
static bool gMqttDiscoveryPublished = false;
static uint32_t gLastMqttRetryMs = 0;
static const uint32_t kMqttRetryMs = 5000;
#if ENABLE_MQTT_OTA
static bool gMqttCallbackRegistered = false;
static bool gOtaCommandReceived = false;
static String gOtaCommandPayload;
static String gOtaCommandTopic;
#endif
static bool connectMqttIfNeeded();
static void publishMqttDiscovery();
static void serviceMqtt();

static String mqttDeviceId() {
    uint64_t chipId = ESP.getEfuseMac();
    char id[24];
    snprintf(id, sizeof(id), "epaper_%04X%08X", (uint16_t)(chipId >> 32), (uint32_t)chipId);
    return String(id);
}

static String mqttTopicBase() {
    return String(MQTT_TOPIC_PREFIX) + "/" + mqttDeviceId();
}

static String mqttTopicWithSuffix(const char *suffix) {
    return mqttTopicBase() + "/" + String(suffix);
}

static bool mqttPublishRetained(const String &topic, const String &payload) {
    return gMqttClient.publish(topic.c_str(), payload.c_str(), true);
}

#if ENABLE_MQTT_OTA
static void mqttPublishOtaStatus(const char *status) {
    if (!connectMqttIfNeeded()) {
        return;
    }
    publishMqttDiscovery();
    String statusTopic = mqttTopicWithSuffix(MQTT_OTA_STATUS_TOPIC_SUFFIX);
    if (!mqttPublishRetained(statusTopic, String(status))) {
        Serial.printf("[ota] Failed to publish status '%s'\n", status);
    }
}

static void mqttPublishOtaError(const String &error) {
    if (!connectMqttIfNeeded()) {
        return;
    }
    publishMqttDiscovery();
    String errTopic = mqttTopicWithSuffix(MQTT_OTA_LAST_ERROR_TOPIC_SUFFIX);
    if (!mqttPublishRetained(errTopic, error)) {
        Serial.printf("[ota] Failed to publish error '%s'\n", error.c_str());
    }
}

static void mqttMessageCallback(char *topic, uint8_t *payload, unsigned int length) {
    if (!topic) {
        return;
    }

    String topicStr(topic);
    if (topicStr != gOtaCommandTopic) {
        return;
    }

    gOtaCommandPayload = "";
    for (unsigned int i = 0; i < length; i++) {
        gOtaCommandPayload += (char)payload[i];
    }
    gOtaCommandPayload.trim();
    gOtaCommandReceived = true;
    Serial.printf("[ota] Command received on %s: %s\n",
                  gOtaCommandTopic.c_str(),
                  gOtaCommandPayload.length() ? gOtaCommandPayload.c_str() : "<empty>");
}

static bool runOtaFromUrl(const String &url) {
    if (url.length() == 0) {
        mqttPublishOtaStatus("ignored_empty_command");
        return false;
    }

    if (gBatteryPercent >= 0 && gBatteryPercent < MQTT_OTA_MIN_BATTERY_PERCENT) {
        Serial.printf("[ota] Skipping OTA; battery too low (%d%% < %d%%)\n",
                      gBatteryPercent, MQTT_OTA_MIN_BATTERY_PERCENT);
        mqttPublishOtaStatus("blocked_low_battery");
        mqttPublishOtaError(String("battery_") + gBatteryPercent);
        return false;
    }

    if (!(url.startsWith("http://") || url.startsWith("https://"))) {
        Serial.printf("[ota] Invalid OTA URL: %s\n", url.c_str());
        mqttPublishOtaStatus("failed_invalid_url");
        mqttPublishOtaError("invalid_url");
        return false;
    }

    mqttPublishOtaStatus("updating");
    Serial.printf("[ota] Starting OTA from %s\n", url.c_str());

    t_httpUpdate_return result;
    if (url.startsWith("https://")) {
        WiFiClientSecure secureClient;
#if MQTT_OTA_ALLOW_INSECURE_TLS
        secureClient.setInsecure();
#endif
        result = httpUpdate.update(secureClient, url);
    } else {
        WiFiClient plainClient;
        result = httpUpdate.update(plainClient, url);
    }

    if (result == HTTP_UPDATE_OK) {
        mqttPublishOtaStatus("updated_restarting");
        delay(200);
        ESP.restart();
        return true;
    }

    if (result == HTTP_UPDATE_NO_UPDATES) {
        Serial.println("[ota] No updates available");
        mqttPublishOtaStatus("no_update");
        return false;
    }

    int errCode = httpUpdate.getLastError();
    String errText = httpUpdate.getLastErrorString();
    Serial.printf("[ota] Update failed (%d): %s\n", errCode, errText.c_str());
    mqttPublishOtaStatus("failed");
    mqttPublishOtaError(String(errCode) + ":" + errText);
    return false;
}

static bool checkAndHandleMqttOta() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    if (!connectMqttIfNeeded()) {
        return false;
    }

    if (!gMqttCallbackRegistered) {
        gMqttClient.setCallback(mqttMessageCallback);
        gMqttCallbackRegistered = true;
    }

    gOtaCommandTopic = mqttTopicWithSuffix(MQTT_OTA_COMMAND_TOPIC_SUFFIX);
    gOtaCommandReceived = false;
    gOtaCommandPayload = "";

    bool subOk = gMqttClient.subscribe(gOtaCommandTopic.c_str());
    if (!subOk) {
        Serial.printf("[ota] Failed to subscribe command topic: %s\n", gOtaCommandTopic.c_str());
        mqttPublishOtaStatus("failed_subscribe");
        return false;
    }

    mqttPublishOtaStatus("idle");

    unsigned long start = millis();
    while ((millis() - start) < MQTT_OTA_COMMAND_WAIT_MS) {
        gMqttClient.loop();
        delay(20);
        if (gOtaCommandReceived) {
            break;
        }
    }

    if (!gOtaCommandReceived) {
        return false;
    }

    String cmd = gOtaCommandPayload;
    cmd.trim();
    if (cmd.length() == 0 || cmd == "none" || cmd == "clear") {
        mqttPublishOtaStatus("idle");
        return false;
    }

    bool otaTriggered = runOtaFromUrl(cmd);
    // Clear retained command to avoid repeating the same OTA command every wake cycle.
    if (!gMqttClient.publish(gOtaCommandTopic.c_str(), "", true)) {
        Serial.println("[ota] Failed to clear retained OTA command");
    }
    return otaTriggered;
}
#else
static bool checkAndHandleMqttOta() {
    return false;
}
#endif

static bool connectMqttIfNeeded() {
    if (gMqttClient.connected()) {
        return true;
    }

    gMqttClient.setServer(MQTT_HOST, MQTT_PORT);
    if (!gMqttClient.setBufferSize(MQTT_PACKET_SIZE)) {
        Serial.printf("[mqtt] Failed to set packet buffer to %u bytes\n", (unsigned)MQTT_PACKET_SIZE);
    }

    String clientId = mqttDeviceId();
    unsigned long start = millis();

    while (!gMqttClient.connected() && (millis() - start) < MQTT_CONNECT_TIMEOUT_MS) {
        bool ok = false;
        if (strlen(MQTT_USERNAME) == 0) {
            ok = gMqttClient.connect(clientId.c_str());
        } else {
            ok = gMqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD);
        }

        if (!ok) {
            delay(200);
        }
    }

    if (!gMqttClient.connected()) {
        Serial.printf("[mqtt] Connect failed (state=%d)\n", gMqttClient.state());
        return false;
    }

    Serial.println("[mqtt] Connected");
    return true;
}

static void publishMqttDiscovery() {
    if (gMqttDiscoveryPublished) {
        return;
    }

    String deviceId = mqttDeviceId();
    String topicBase = mqttTopicBase();

    String percentObjectId = deviceId + "_battery_percent";
    String voltageObjectId = deviceId + "_battery_voltage";
#if ENABLE_MQTT_OTA
    String otaStatusObjectId = deviceId + "_ota_status";
    String otaErrorObjectId = deviceId + "_ota_last_error";
#endif

    String percentStateTopic = topicBase + "/battery_percent/state";
    String voltageStateTopic = topicBase + "/battery_voltage/state";
#if ENABLE_MQTT_OTA
    String otaStatusStateTopic = topicBase + "/" + String(MQTT_OTA_STATUS_TOPIC_SUFFIX);
    String otaErrorStateTopic = topicBase + "/" + String(MQTT_OTA_LAST_ERROR_TOPIC_SUFFIX);
#endif

    String deviceJson =
        "\"device\":{\"identifiers\":[\"" + deviceId +
        "\"],\"name\":\"" + String(MQTT_DEVICE_NAME) +
        "\",\"manufacturer\":\"DIY\",\"model\":\"ESP32 Epaper Weather\"}";

    String percentConfig =
        "{\"name\":\"" + String(MQTT_DEVICE_NAME) + " Battery\","
        "\"uniq_id\":\"" + percentObjectId + "\","
        "\"stat_t\":\"" + percentStateTopic + "\","
        "\"unit_of_meas\":\"%\","
        "\"dev_cla\":\"battery\","
        "\"stat_cla\":\"measurement\"," + deviceJson + "}";

    String voltageConfig =
        "{\"name\":\"" + String(MQTT_DEVICE_NAME) + " Battery Voltage\","
        "\"uniq_id\":\"" + voltageObjectId + "\","
        "\"stat_t\":\"" + voltageStateTopic + "\","
        "\"unit_of_meas\":\"V\","
        "\"dev_cla\":\"voltage\","
        "\"stat_cla\":\"measurement\"," + deviceJson + "}";

    String percentConfigTopic =
        String(MQTT_DISCOVERY_PREFIX) + "/sensor/" + percentObjectId + "/config";
    String voltageConfigTopic =
        String(MQTT_DISCOVERY_PREFIX) + "/sensor/" + voltageObjectId + "/config";

#if ENABLE_MQTT_OTA
    String otaStatusConfig =
        "{\"name\":\"" + String(MQTT_DEVICE_NAME) + " OTA Status\"," 
        "\"uniq_id\":\"" + otaStatusObjectId + "\"," 
        "\"stat_t\":\"" + otaStatusStateTopic + "\"," + deviceJson + "}";

    String otaErrorConfig =
        "{\"name\":\"" + String(MQTT_DEVICE_NAME) + " OTA Last Error\"," 
        "\"uniq_id\":\"" + otaErrorObjectId + "\"," 
        "\"stat_t\":\"" + otaErrorStateTopic + "\"," + deviceJson + "}";

    String otaStatusConfigTopic =
        String(MQTT_DISCOVERY_PREFIX) + "/sensor/" + otaStatusObjectId + "/config";
    String otaErrorConfigTopic =
        String(MQTT_DISCOVERY_PREFIX) + "/sensor/" + otaErrorObjectId + "/config";
#endif

    bool ok1 = gMqttClient.publish(percentConfigTopic.c_str(), percentConfig.c_str(), true);
    bool ok2 = gMqttClient.publish(voltageConfigTopic.c_str(), voltageConfig.c_str(), true);
#if ENABLE_MQTT_OTA
    bool ok3 = gMqttClient.publish(otaStatusConfigTopic.c_str(), otaStatusConfig.c_str(), true);
    bool ok4 = gMqttClient.publish(otaErrorConfigTopic.c_str(), otaErrorConfig.c_str(), true);
#else
    bool ok3 = true;
    bool ok4 = true;
#endif

    if (ok1 && ok2 && ok3 && ok4) {
        gMqttDiscoveryPublished = true;
        Serial.println("[mqtt] Discovery published");
    } else {
        Serial.printf("[mqtt] Discovery publish failed (state=%d, buf=%u)\n",
                      gMqttClient.state(), (unsigned)MQTT_PACKET_SIZE);
#if ENABLE_MQTT_OTA
        Serial.printf("[mqtt] cfg lens p=%u v=%u ota_s=%u ota_e=%u\n",
                      (unsigned)percentConfig.length(),
                      (unsigned)voltageConfig.length(),
                      (unsigned)otaStatusConfig.length(),
                      (unsigned)otaErrorConfig.length());
        Serial.printf("[mqtt] topic lens p=%u v=%u ota_s=%u ota_e=%u\n",
                      (unsigned)percentConfigTopic.length(),
                      (unsigned)voltageConfigTopic.length(),
                      (unsigned)otaStatusConfigTopic.length(),
                      (unsigned)otaErrorConfigTopic.length());
#else
        Serial.printf("[mqtt] cfg lens p=%u v=%u\n",
                      (unsigned)percentConfig.length(),
                      (unsigned)voltageConfig.length());
        Serial.printf("[mqtt] topic lens p=%u v=%u\n",
                      (unsigned)percentConfigTopic.length(),
                      (unsigned)voltageConfigTopic.length());
#endif
    }
}

static void publishBatteryToMqtt() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    if (gBatteryVoltage <= 0.0f && gBatteryPercent < 0) {
        return;
    }
    if (!connectMqttIfNeeded()) {
        return;
    }

    publishMqttDiscovery();

    String topicBase = mqttTopicBase();
    bool ok = true;

    if (gBatteryPercent >= 0) {
        char percentBuf[8];
        snprintf(percentBuf, sizeof(percentBuf), "%d", gBatteryPercent);
        ok = ok && gMqttClient.publish((topicBase + "/battery_percent/state").c_str(), percentBuf, true);
    }

    if (gBatteryVoltage > 0.0f) {
        char voltageBuf[16];
        snprintf(voltageBuf, sizeof(voltageBuf), "%.3f", gBatteryVoltage);
        ok = ok && gMqttClient.publish((topicBase + "/battery_voltage/state").c_str(), voltageBuf, true);
    }

    Serial.printf("[mqtt] Battery publish %s\n", ok ? "ok" : "failed");
    gMqttClient.loop();
}

static void serviceMqtt() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (gMqttClient.connected()) {
        gMqttClient.loop();
        return;
    }

    uint32_t now = millis();
    if ((now - gLastMqttRetryMs) < kMqttRetryMs) {
        return;
    }
    gLastMqttRetryMs = now;

    if (connectMqttIfNeeded()) {
        publishMqttDiscovery();
        gMqttClient.loop();
    }
}
#else
static void publishBatteryToMqtt() {
}

static bool checkAndHandleMqttOta() {
    return false;
}

static void serviceMqtt() {
}
#endif

static const char *kWeatherCacheNamespace = "weather";
static const char *kWeatherCacheKey = "last";
static const uint32_t kWeatherCacheMagic = 0x57544852;  // "WTHR"
static const uint16_t kWeatherCacheVersion = 1;

struct PersistedForecastEntry {
    char condition[24];
    char datetime[28];
    int16_t temperatureTenths;
};

struct PersistedWeather {
    uint32_t magic;
    uint16_t version;
    uint16_t forecastCount;
    char currentCondition[24];
    int16_t currentTempTenths;
    int16_t apparentTempTenths;
    PersistedForecastEntry forecast[MAX_FORECAST];
};

template <size_t N>
static void copyStringToFixed(char (&dest)[N], const String &src) {
    size_t len = src.length();
    if (len >= N) len = N - 1;
    memcpy(dest, src.c_str(), len);
    dest[len] = '\0';
}

static int16_t tempToTenths(float value) {
    return (int16_t)lroundf(value * 10.0f);
}

static float tenthsToTemp(int16_t value) {
    return ((float)value) / 10.0f;
}

static void toPersistedWeather(const WeatherResult &src, PersistedWeather &dst) {
    memset(&dst, 0, sizeof(dst));
    dst.magic = kWeatherCacheMagic;
    dst.version = kWeatherCacheVersion;
    dst.forecastCount = (uint16_t)min(src.forecastCount, MAX_FORECAST);
    copyStringToFixed(dst.currentCondition, src.current.condition);
    dst.currentTempTenths = tempToTenths(src.current.temperature);
    dst.apparentTempTenths = tempToTenths(src.current.apparent_temperature);

    for (int i = 0; i < dst.forecastCount; i++) {
        copyStringToFixed(dst.forecast[i].condition, src.forecast[i].condition);
        copyStringToFixed(dst.forecast[i].datetime, src.forecast[i].datetime);
        dst.forecast[i].temperatureTenths = tempToTenths(src.forecast[i].temperature);
    }
}

static void fromPersistedWeather(const PersistedWeather &src, WeatherResult &dst) {
    dst.current.condition = String(src.currentCondition);
    dst.current.temperature = tenthsToTemp(src.currentTempTenths);
    dst.current.apparent_temperature = tenthsToTemp(src.apparentTempTenths);
    dst.current.humidity = 0.0f;
    dst.current.wind_speed = 0.0f;
    dst.current.pressure = 0.0f;
    dst.current.valid = true;

    dst.forecastCount = min((int)src.forecastCount, MAX_FORECAST);
    for (int i = 0; i < dst.forecastCount; i++) {
        dst.forecast[i].condition = String(src.forecast[i].condition);
        dst.forecast[i].datetime = String(src.forecast[i].datetime);
        dst.forecast[i].temperature = tenthsToTemp(src.forecast[i].temperatureTenths);
    }
}

static bool saveWeatherCache(const WeatherResult &weather) {
    PersistedWeather persisted;
    toPersistedWeather(weather, persisted);

    Preferences prefs;
    if (!prefs.begin(kWeatherCacheNamespace, false)) {
        Serial.println("[cache] Failed to open weather namespace for save");
        return false;
    }

    size_t written = prefs.putBytes(kWeatherCacheKey, &persisted, sizeof(persisted));
    prefs.end();
    bool ok = written == sizeof(persisted);
    Serial.printf("[cache] Save %s (%u bytes)\n", ok ? "ok" : "failed", (unsigned)written);
    return ok;
}

static bool loadWeatherCache(WeatherResult &weather) {
    Preferences prefs;
    if (!prefs.begin(kWeatherCacheNamespace, true)) {
        Serial.println("[cache] Failed to open weather namespace for load");
        return false;
    }

    PersistedWeather persisted;
    memset(&persisted, 0, sizeof(persisted));
    size_t got = prefs.getBytes(kWeatherCacheKey, &persisted, sizeof(persisted));
    prefs.end();

    if (got != sizeof(persisted)) {
        Serial.println("[cache] No cached weather yet");
        return false;
    }
    if (persisted.magic != kWeatherCacheMagic || persisted.version != kWeatherCacheVersion) {
        Serial.println("[cache] Cached weather format mismatch");
        return false;
    }

    fromPersistedWeather(persisted, weather);
    Serial.printf("[cache] Loaded cached weather (forecast=%d)\n", weather.forecastCount);
    return true;
}

static bool applyCachedForecastIfMissing(WeatherResult &weather) {
    if (weather.forecastCount > 0) return false;

    WeatherResult cached;
    if (!loadWeatherCache(cached) || cached.forecastCount <= 0) {
        return false;
    }

    weather.forecastCount = cached.forecastCount;
    for (int i = 0; i < weather.forecastCount; i++) {
        weather.forecast[i] = cached.forecast[i];
    }
    Serial.printf("[cache] Reused cached forecast (%d entries)\n", weather.forecastCount);
    return true;
}

static float forecastTargetTemperature(const WeatherResult &weather, int maxPoints) {
    int usable = min(weather.forecastCount, maxPoints);
    if (usable <= 0) {
        return weather.current.temperature;
    }

    float sum = 0.0f;
    for (int i = 0; i < usable; i++) {
        sum += weather.forecast[i].temperature;
    }
    return sum / (float)usable;
}

static float interpolateTowardsTarget(float fromTemp, float targetTemp, uint32_t invalidMinutes) {
    if (invalidMinutes <= HA_INVALID_GRACE_MINUTES) {
        return fromTemp;
    }

    uint32_t excess = invalidMinutes - HA_INVALID_GRACE_MINUTES;
    float blend = (float)excess / (float)HA_INTERPOLATION_RAMP_MINUTES;
    if (blend < 0.0f) blend = 0.0f;
    if (blend > 1.0f) blend = 1.0f;

    float blended = fromTemp + (targetTemp - fromTemp) * blend;
    float delta = blended - fromTemp;
    if (delta > HA_INTERPOLATION_MAX_STEP_C_PER_CYCLE) {
        blended = fromTemp + HA_INTERPOLATION_MAX_STEP_C_PER_CYCLE;
    } else if (delta < -HA_INTERPOLATION_MAX_STEP_C_PER_CYCLE) {
        blended = fromTemp - HA_INTERPOLATION_MAX_STEP_C_PER_CYCLE;
    }
    return blended;
}

static bool applyForecastInterpolationIfNeeded(WeatherResult &weather, uint32_t invalidMinutes) {
    if (invalidMinutes <= HA_INVALID_GRACE_MINUTES || weather.forecastCount <= 0) {
        return false;
    }

    const float fromTemp = weather.current.temperature;
    const float targetTemp = forecastTargetTemperature(weather, HA_INTERPOLATE_FORECAST_POINTS);
    const float nextTemp = interpolateTowardsTarget(fromTemp, targetTemp, invalidMinutes);
    const float delta = nextTemp - fromTemp;

    weather.current.temperature = nextTemp;
    weather.current.apparent_temperature = weather.current.apparent_temperature + delta;

    Serial.printf("[weather] Interpolated current %.1fC -> %.1fC (target %.1fC, invalid=%u min)\n",
                  fromTemp, nextTemp, targetTemp, (unsigned)invalidMinutes);
    return true;
}

static bool applyInvalidCurrentFallback(WeatherResult &weather,
                                        const WeatherResult &cachedWeather,
                                        bool hasCachedWeather,
                                        uint32_t invalidMinutes) {
    if (weather.currentDataValid) {
        return true;
    }
    if (!hasCachedWeather || !cachedWeather.current.valid) {
        return false;
    }

    // Keep the last known valid current reading when HA current data is unavailable.
    weather.current = cachedWeather.current;
    weather.current.valid = true;

    if (weather.forecastCount <= 0) {
        weather.forecastCount = cachedWeather.forecastCount;
        for (int i = 0; i < weather.forecastCount; i++) {
            weather.forecast[i] = cachedWeather.forecast[i];
        }
    }

    if (!applyForecastInterpolationIfNeeded(weather, invalidMinutes)) {
        Serial.printf("[weather] Keeping cached current %.1fC (invalid=%u min)\n",
                      weather.current.temperature,
                      (unsigned)invalidMinutes);
    }

    return true;
}

static int voltageToBatteryPercent(float voltage) {
    if (voltage <= 0.0f) return -1;
    const float emptyV = 3.30f;
    const float fullV = 4.20f;
    float p = ((voltage - emptyV) * 100.0f) / (fullV - emptyV);
    if (p < 0.0f) p = 0.0f;
    if (p > 100.0f) p = 100.0f;
    return (int)(p + 0.5f);
}

static float readBatteryVoltage() {
#if ENABLE_BATTERY_MEASUREMENT
    int samples = BATTERY_ADC_SAMPLES;
    if (samples < 1) samples = 1;

    analogReadResolution(12);
    analogSetPinAttenuation(BATTERY_SENSE_PIN, ADC_11db);

    uint32_t sumMilliVolts = 0;
    for (int i = 0; i < samples; i++) {
        sumMilliVolts += (uint32_t)analogReadMilliVolts(BATTERY_SENSE_PIN);
        delay(2);
    }

    float pinVolts = ((float)sumMilliVolts / (float)samples) / 1000.0f;
    return pinVolts * BATTERY_DIVIDER_RATIO;
#else
    return -1.0f;
#endif
}

static void updateBatteryMeasurement() {
    gBatteryVoltage = readBatteryVoltage();
    gBatteryPercent = voltageToBatteryPercent(gBatteryVoltage);
    if (gBatteryVoltage > 0.0f) {
        Serial.printf("[bat] %.3fV (%d%%)\n", gBatteryVoltage, gBatteryPercent);
    } else {
        Serial.println("[bat] Measurement unavailable");
    }
}

#if ENABLE_UI_WEB_TUNER
RTC_DATA_ATTR bool gInfoScreenMode = false;
RTC_DATA_ATTR bool gButtonLatchedAcrossBoot = false;
#endif

#define UI_TUNING_ENABLED (ENABLE_UI_TUNING_CONSOLE || ENABLE_UI_WEB_TUNER)

#if UI_TUNING_ENABLED
static Preferences gUiPrefs;
static WeatherResult gLastWeather;
static bool gHasWeather = false;
static int gLastHour = -1;
static bool gUseRealDisplayValues = true;
static int16_t gManualDisplayValue = 25;

struct LayoutFieldBinding {
    const char *name;
    int16_t DisplayDesign2Layout::*member;
};

static const LayoutFieldBinding kLayoutFields[] = {
    {"lastUpdateX", &DisplayDesign2Layout::lastUpdateX},
    {"lastUpdateY", &DisplayDesign2Layout::lastUpdateY},
    {"lastUpdateLineSpacing", &DisplayDesign2Layout::lastUpdateLineSpacing},
    {"currentIconX", &DisplayDesign2Layout::currentIconX},
    {"currentIconY", &DisplayDesign2Layout::currentIconY},
    {"currentIconSize", &DisplayDesign2Layout::currentIconSize},
    {"currentTempX", &DisplayDesign2Layout::currentTempX},
    {"currentTempY", &DisplayDesign2Layout::currentTempY},
    {"currentTempJustify", &DisplayDesign2Layout::currentTempJustify},
    {"feelTempX", &DisplayDesign2Layout::feelTempX},
    {"feelTempY", &DisplayDesign2Layout::feelTempY},
    {"feelDegreeX", &DisplayDesign2Layout::feelDegreeX},
    {"feelDegreeY", &DisplayDesign2Layout::feelDegreeY},
    {"feelCX", &DisplayDesign2Layout::feelCX},
    {"feelCY", &DisplayDesign2Layout::feelCY},
    {"highRightX", &DisplayDesign2Layout::highRightX},
    {"highY", &DisplayDesign2Layout::highY},
    {"lowRightX", &DisplayDesign2Layout::lowRightX},
    {"lowY", &DisplayDesign2Layout::lowY},
    {"dividerY", &DisplayDesign2Layout::dividerY},
    {"forecastHourY", &DisplayDesign2Layout::forecastHourY},
    {"forecastTempY", &DisplayDesign2Layout::forecastTempY},
    {"forecastIconOffsetX", &DisplayDesign2Layout::forecastIconOffsetX},
    {"forecastIconY", &DisplayDesign2Layout::forecastIconY},
    {"forecastIconSize", &DisplayDesign2Layout::forecastIconSize},
    {"lastUpdateFontSize", &DisplayDesign2Layout::lastUpdateFontSize},
    {"lastUpdateFontFamily", &DisplayDesign2Layout::lastUpdateFontFamily},
    {"currentTempFontSize", &DisplayDesign2Layout::currentTempFontSize},
    {"currentTempFontFamily", &DisplayDesign2Layout::currentTempFontFamily},
    {"feelTempFontSize", &DisplayDesign2Layout::feelTempFontSize},
    {"feelTempFontFamily", &DisplayDesign2Layout::feelTempFontFamily},
    {"highTempFontSize", &DisplayDesign2Layout::highTempFontSize},
    {"highTempFontFamily", &DisplayDesign2Layout::highTempFontFamily},
    {"lowTempFontSize", &DisplayDesign2Layout::lowTempFontSize},
    {"lowTempFontFamily", &DisplayDesign2Layout::lowTempFontFamily},
    {"forecastHourFontSize", &DisplayDesign2Layout::forecastHourFontSize},
    {"forecastHourFontFamily", &DisplayDesign2Layout::forecastHourFontFamily},
    {"forecastTempFontSize", &DisplayDesign2Layout::forecastTempFontSize},
    {"forecastTempFontFamily", &DisplayDesign2Layout::forecastTempFontFamily},
    {"batteryX", &DisplayDesign2Layout::batteryX},
    {"batteryY", &DisplayDesign2Layout::batteryY},
};

static const size_t kLayoutFieldCount = sizeof(kLayoutFields) / sizeof(kLayoutFields[0]);

static int layoutFieldIndex(const String &name) {
    for (size_t i = 0; i < kLayoutFieldCount; i++) {
        if (name.equalsIgnoreCase(kLayoutFields[i].name)) {
            return (int)i;
        }
    }
    return -1;
}

static void printLayoutFields() {
    Serial.println("[ui] Available fields:");
    for (size_t i = 0; i < kLayoutFieldCount; i++) {
        Serial.printf("  %s\n", kLayoutFields[i].name);
    }
}

static void printCurrentLayout() {
    DisplayDesign2Layout layout = getDisplayDesign2Layout();
    Serial.println("[ui] Current Design 2 layout:");
    for (size_t i = 0; i < kLayoutFieldCount; i++) {
        int16_t value = layout.*(kLayoutFields[i].member);
        Serial.printf("  %-18s %d\n", kLayoutFields[i].name, (int)value);
    }
}

static bool setLayoutFieldValue(const String &name, int value, int16_t *appliedValue = nullptr) {
    int idx = layoutFieldIndex(name);
    if (idx < 0) return false;

    DisplayDesign2Layout layout = getDisplayDesign2Layout();
    layout.*(kLayoutFields[idx].member) = (int16_t)value;
    setDisplayDesign2Layout(layout);

    if (appliedValue != nullptr) {
        DisplayDesign2Layout updated = getDisplayDesign2Layout();
        *appliedValue = updated.*(kLayoutFields[idx].member);
    }
    return true;
}

static bool setLayoutFieldValueOnLayout(DisplayDesign2Layout &layout, const String &name, int value) {
    int idx = layoutFieldIndex(name);
    if (idx < 0) return false;

    layout.*(kLayoutFields[idx].member) = (int16_t)value;
    return true;
}

static void saveLayoutToPrefs() {
    DisplayDesign2Layout layout = getDisplayDesign2Layout();
    if (!gUiPrefs.begin("ui_tuning", false)) {
        Serial.println("[ui] Failed to open NVS for save");
        return;
    }

    for (size_t i = 0; i < kLayoutFieldCount; i++) {
        int16_t value = layout.*(kLayoutFields[i].member);
        gUiPrefs.putShort(kLayoutFields[i].name, value);
    }
    gUiPrefs.end();
    Serial.println("[ui] Layout saved to NVS");
}

static void loadLayoutFromPrefs() {
    DisplayDesign2Layout layout = getDisplayDesign2Layout();

    if (!gUiPrefs.begin("ui_tuning", true)) {
        Serial.println("[ui] Failed to open NVS for load");
        return;
    }

    for (size_t i = 0; i < kLayoutFieldCount; i++) {
        int16_t current = layout.*(kLayoutFields[i].member);
        layout.*(kLayoutFields[i].member) = gUiPrefs.getShort(kLayoutFields[i].name, current);
    }
    gUiPrefs.end();

    setDisplayDesign2Layout(layout);
    Serial.println("[ui] Layout loaded from NVS");
}

static void redrawCurrentWeather() {
    if (!gHasWeather) {
        Serial.println("[ui] No weather cached yet. Run: fetch");
        return;
    }

    WeatherResult displayWeather = gLastWeather;
    if (!gUseRealDisplayValues) {
        float v = (float)gManualDisplayValue;
        displayWeather.current.temperature = v;
        displayWeather.current.apparent_temperature = v;
        for (int i = 0; i < displayWeather.forecastCount; i++) {
            displayWeather.forecast[i].temperature = v;
        }
    }

    updateBatteryMeasurement();
    setBatteryDisplayPercent(gBatteryPercent);

    drawWeather(displayWeather);
    Serial.println("[ui] Redrawn");
}

static bool fetchAndDrawWeatherForTuning(bool keepWifiConnected) {
    bool hadWifi = WiFi.status() == WL_CONNECTED;
    if (!hadWifi && !connectWiFi()) {
        Serial.println("[ui] WiFi connect failed");
        setWifiConnectionFailed(true);
        if (gHasWeather) {
            redrawCurrentWeather();
        } else if (loadWeatherCache(gLastWeather)) {
            gHasWeather = true;
            redrawCurrentWeather();
        } else {
            drawError("WiFi failed");
        }
        return false;
    }

    int hour = syncAndGetHour();
    gLastHour = hour;

    WeatherResult result;
    if (!fetchWeather(result)) {
        gCurrentDataInvalidMinutes += getSleepIntervalMinutes(hour);
        if (!keepWifiConnected) {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
        }
        Serial.println("[ui] Weather fetch failed");
        setWifiConnectionFailed(WiFi.status() != WL_CONNECTED);
        if (gHasWeather) {
            WeatherResult displayWeather = gLastWeather;
            applyForecastInterpolationIfNeeded(displayWeather, gCurrentDataInvalidMinutes);
            gLastWeather = displayWeather;
            redrawCurrentWeather();
        } else if (loadWeatherCache(gLastWeather)) {
            gHasWeather = true;
            applyForecastInterpolationIfNeeded(gLastWeather, gCurrentDataInvalidMinutes);
            redrawCurrentWeather();
        } else {
            drawError("HA fetch failed");
        }
        return false;
    }

    uint32_t invalidMinutes = gCurrentDataInvalidMinutes;
    if (!result.currentDataValid) {
        invalidMinutes += getSleepIntervalMinutes(hour);
        bool hasCurrent = applyInvalidCurrentFallback(result, gLastWeather, gHasWeather, invalidMinutes);
        if (!hasCurrent) {
            WeatherResult cached;
            if (loadWeatherCache(cached)) {
                hasCurrent = applyInvalidCurrentFallback(result, cached, true, invalidMinutes);
            }
        }
        if (!hasCurrent) {
            if (!keepWifiConnected) {
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
            }
            Serial.println("[ui] Current data unavailable and no cache to preserve last value");
            drawError("Current data unavailable");
            return false;
        }
        gCurrentDataInvalidMinutes = invalidMinutes;
    } else {
        gCurrentDataInvalidMinutes = 0;
    }

    applyCachedForecastIfMissing(result);

    if (!keepWifiConnected) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    gLastWeather = result;
    gHasWeather = true;
    if (result.currentDataValid) {
        saveWeatherCache(result);
    }
    setWifiConnectionFailed(false);
    redrawCurrentWeather();
    Serial.println("[ui] Weather fetched and redrawn");
    return true;
}
#endif

#if ENABLE_UI_WEB_TUNER
static WebServer gWebServer(UI_WEB_TUNER_PORT);
static bool gWebServerRunning = false;
static bool gWebClientActive = false;
static bool gWebClientEverConnected = false;
static uint32_t gWebLastClientMs = 0;
static uint32_t gWebAwakeSinceMs = 0;
static bool gButtonPressedLastLoop = false;
static uint32_t gLastButtonToggleMs = 0;
static uint32_t gLastWifiRetryMs = 0;
static const uint32_t kButtonDebounceMs = 300;
static const uint32_t kWifiRetryMs = 5000;
static File gSdUploadFile;
static String gSdUploadPath;
static String gSdUploadError;
static bool gSdUploadInProgress = false;

static String normalizeSdPath(const String &rawPath) {
    String p = rawPath;
    p.trim();
    if (!p.startsWith("/")) {
        p = "/" + p;
    }
    return p;
}

static bool isAllowedSdPath(const String &path) {
    if (!path.startsWith("/")) return false;
    if (path.indexOf("..") >= 0) return false;
    if (path.endsWith("/")) return false;
    return path.startsWith("/icons/") || path.startsWith("/fonts/") || path.startsWith("/ui/");
}

static bool isUserButtonPressed() {
#if USER_BUTTON_ACTIVE_LOW
    return digitalRead(USER_BUTTON_PIN) == LOW;
#else
    return digitalRead(USER_BUTTON_PIN) == HIGH;
#endif
}

static void initUserButton() {
    pinMode(USER_BUTTON_PIN, INPUT);
}

static void updateInfoModeByBootButton() {
    bool pressed = isUserButtonPressed();
    uint32_t now = millis();
    if (pressed && !gButtonLatchedAcrossBoot && (now - gLastButtonToggleMs) > kButtonDebounceMs) {
        gInfoScreenMode = !gInfoScreenMode;
        gLastButtonToggleMs = now;
        gButtonLatchedAcrossBoot = true;
        Serial.printf("[btn] Toggled mode -> %s\n", gInfoScreenMode ? "INFO" : "NORMAL");
    } else if (!pressed) {
        gButtonLatchedAcrossBoot = false;
    }
}

static void markWebClientActivity(bool sessionKeepAlive) {
    bool firstRequest = !gWebClientEverConnected;
    bool sessionBecameActive = sessionKeepAlive && !gWebClientActive;
    gWebClientEverConnected = true;
    if (sessionKeepAlive) {
        gWebClientActive = true;
        gWebLastClientMs = millis();
    }

    if (firstRequest || sessionBecameActive) {
        String remoteIp = gWebServer.client().remoteIP().toString();
        Serial.printf("[web] Client %s from %s -> %s\n",
                      sessionKeepAlive ? "session" : "request",
                      remoteIp.c_str(),
                      gWebServer.uri().c_str());
    }
}

static void addNoCacheHeaders() {
    gWebServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    gWebServer.sendHeader("Pragma", "no-cache");
    gWebServer.sendHeader("Expires", "0");
}

static void drawCurrentInfoScreen() {
    drawWebConnectInfo(WiFi.localIP().toString(), UI_WEB_TUNER_PORT, gWebClientActive);
}

static bool webMinimumAwakeElapsed() {
    if (UI_WEB_MIN_AWAKE_MS == 0) {
        return true;
    }
    return (millis() - gWebAwakeSinceMs) >= UI_WEB_MIN_AWAKE_MS;
}

static String layoutToJson(bool includeMeta) {
    StaticJsonDocument<2048> doc;
    JsonObject root = doc.to<JsonObject>();

    DisplayDesign2Layout layout = getDisplayDesign2Layout();
    JsonObject fields = root.createNestedObject("fields");
    for (size_t i = 0; i < kLayoutFieldCount; i++) {
        fields[kLayoutFields[i].name] = layout.*(kLayoutFields[i].member);
    }

    if (includeMeta) {
        root["hasWeather"] = gHasWeather;
        root["ip"] = WiFi.localIP().toString();
        root["design"] = DISPLAY_DESIGN;
        root["useRealValues"] = gUseRealDisplayValues;
        root["testValue"] = gManualDisplayValue;
        root["batteryVoltage"] = gBatteryVoltage;
        root["batteryPercent"] = gBatteryPercent;
    }

    String out;
    serializeJsonPretty(doc, out);
    return out;
}

static void handleWebRoot() {
    markWebClientActivity(false);
    addNoCacheHeaders();
    if (ensureSdReady()) {
        File uiFile = SD.open("/ui/index.html", FILE_READ);
        if (uiFile) {
            gWebServer.streamFile(uiFile, "text/html");
            uiFile.close();
            return;
        }
    }

    static const char FALLBACK_HTML[] PROGMEM =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 Display Tuner</title></head><body>"
        "<h1>ESP32 Display Tuner</h1>"
        "<p>Upload <code>/ui/index.html</code> to SD to enable the full web UI.</p>"
        "<p>API endpoints remain available at <code>/api/*</code>.</p>"
        "</body></html>";
    gWebServer.send(200, "text/html", FALLBACK_HTML);
}

static void handleApiLayoutGet() {
    markWebClientActivity(true);
    addNoCacheHeaders();
    gWebServer.send(200, "application/json", layoutToJson(true));
}

static void handleApiLayoutPost() {
    markWebClientActivity(true);
    if (!gWebServer.hasArg("plain")) {
        gWebServer.send(400, "text/plain", "Missing request body");
        return;
    }

    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, gWebServer.arg("plain").c_str());
    if (err) {
        gWebServer.send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
        return;
    }

    JsonVariant fieldsVar = doc["fields"];
    if (!fieldsVar.is<JsonObject>()) {
        gWebServer.send(400, "text/plain", "JSON must include object: fields");
        return;
    }

    DisplayDesign2Layout updatedLayout = getDisplayDesign2Layout();
    JsonObject fields = fieldsVar.as<JsonObject>();
    for (JsonPair kv : fields) {
        const char *key = kv.key().c_str();
        int value = kv.value().as<int>();
        setLayoutFieldValueOnLayout(updatedLayout, String(key), value);
    }
    setDisplayDesign2Layout(updatedLayout);

    if (doc.containsKey("useRealValues")) {
        gUseRealDisplayValues = doc["useRealValues"].as<bool>();
    }
    if (doc.containsKey("testValue")) {
        int requested = doc["testValue"].as<int>();
        gManualDisplayValue = (int16_t)constrain(requested, -99, 99);
    }

    redrawCurrentWeather();
    gWebServer.send(200, "text/plain", "OK");
}

static void handleApiRedraw() {
    markWebClientActivity(true);
    redrawCurrentWeather();
    gWebServer.send(200, "text/plain", "Redrawn");
}

static void handleApiFetch() {
    markWebClientActivity(true);
    bool ok = fetchAndDrawWeatherForTuning(true);
    gWebServer.send(ok ? 200 : 500, "text/plain", ok ? "Fetched" : "Fetch failed");
}

static void handleApiSave() {
    markWebClientActivity(true);
    saveLayoutToPrefs();
    gWebServer.send(200, "text/plain", "Saved");
}

static void handleApiLoad() {
    markWebClientActivity(true);
    loadLayoutFromPrefs();
    redrawCurrentWeather();
    gWebServer.send(200, "text/plain", "Loaded");
}

static void handleApiReset() {
    markWebClientActivity(true);
    resetDisplayDesign2Layout();
    redrawCurrentWeather();
    gWebServer.send(200, "text/plain", "Reset");
}

static void handleDownloadLayoutJson() {
    markWebClientActivity(true);
    String body = layoutToJson(true);
    addNoCacheHeaders();
    gWebServer.sendHeader("Content-Disposition", "attachment; filename=layout.json");
    gWebServer.send(200, "application/json", body);
}

static void handleApiPing() {
    markWebClientActivity(true);
    gWebServer.send(200, "text/plain", "pong");
}

static void handleApiSdUpload() {
    markWebClientActivity(true);
    gSdUploadInProgress = false;
    if (gSdUploadError.length() > 0) {
        gWebServer.send(500, "text/plain", gSdUploadError);
        return;
    }

    if (gSdUploadPath.length() == 0) {
        gWebServer.send(400, "text/plain", "Missing path");
        return;
    }

    redrawCurrentWeather();
    gWebServer.send(200, "text/plain", String("Uploaded to ") + gSdUploadPath);
}

static void handleApiSdUploadData() {
    HTTPUpload &upload = gWebServer.upload();

    if (upload.status == UPLOAD_FILE_START) {
        gSdUploadInProgress = true;
        gSdUploadError = "";
        gSdUploadPath = normalizeSdPath(gWebServer.arg("path"));

        if (!isAllowedSdPath(gSdUploadPath)) {
            gSdUploadError = "Invalid path. Allowed roots: /icons/, /fonts/, or /ui/";
            gSdUploadInProgress = false;
            return;
        }

        if (!ensureSdReady() && !forceSdRemount()) {
            gSdUploadError = "SD not available";
            Serial.println("[sd] Upload mount failed; card unavailable");
            gSdUploadInProgress = false;
            return;
        }

        if (SD.exists(gSdUploadPath)) {
            SD.remove(gSdUploadPath);
        }

        gSdUploadFile = SD.open(gSdUploadPath, FILE_WRITE);
        if (!gSdUploadFile) {
            gSdUploadError = String("Cannot open for write: ") + gSdUploadPath;
            gSdUploadInProgress = false;
            return;
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (gSdUploadError.length() > 0) return;
        if (!gSdUploadFile) {
            gSdUploadError = "Upload file handle is closed";
            gSdUploadInProgress = false;
            return;
        }
        size_t written = gSdUploadFile.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
            gSdUploadError = "Short write to SD card";
            gSdUploadFile.close();
            SD.remove(gSdUploadPath);
            gSdUploadInProgress = false;
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (gSdUploadFile) {
            gSdUploadFile.close();
        }
        gSdUploadInProgress = false;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (gSdUploadFile) {
            gSdUploadFile.close();
        }
        if (gSdUploadPath.length() > 0) {
            SD.remove(gSdUploadPath);
        }
        gSdUploadError = "Upload aborted";
        gSdUploadInProgress = false;
    }
}

static void startUiWebServer() {
    gWebServer.on("/", HTTP_GET, handleWebRoot);
    gWebServer.on("/api/layout", HTTP_GET, handleApiLayoutGet);
    gWebServer.on("/api/layout", HTTP_POST, handleApiLayoutPost);
    gWebServer.on("/api/redraw", HTTP_POST, handleApiRedraw);
    gWebServer.on("/api/fetch", HTTP_POST, handleApiFetch);
    gWebServer.on("/api/save", HTTP_POST, handleApiSave);
    gWebServer.on("/api/load", HTTP_POST, handleApiLoad);
    gWebServer.on("/api/reset", HTTP_POST, handleApiReset);
    gWebServer.on("/api/ping", HTTP_POST, handleApiPing);
    gWebServer.on("/api/sd/upload", HTTP_POST, handleApiSdUpload, handleApiSdUploadData);
    gWebServer.on("/download/layout.json", HTTP_GET, handleDownloadLayoutJson);
    gWebServer.begin();
    gWebServerRunning = true;
    gWebClientActive = false;
    gWebClientEverConnected = false;
    gWebLastClientMs = millis();
    gWebAwakeSinceMs = gWebLastClientMs;

    Serial.printf("[web] UI tuner available at http://%s:%d/\n",
                  WiFi.localIP().toString().c_str(), UI_WEB_TUNER_PORT);
}
#endif

// ─── WiFi ─────────────────────────────────────────────────────────────────────

static bool connectWiFi() {
#if USE_STATIC_IP
    IPAddress ip, gw, sn, dns1, dns2;
    ip.fromString(STATIC_IP);
    gw.fromString(GATEWAY);
    sn.fromString(SUBNET);
    dns1.fromString(DNS1);
    dns2.fromString(DNS2);
    WiFi.config(ip, gw, sn, dns1, dns2);
#endif

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_TIMEOUT_MS) {
            Serial.println("[wifi] Connection timed out");
            return false;
        }
        delay(100);
    }
    Serial.printf("[wifi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    publishBatteryToMqtt();
    return true;
}

// ─── Time ─────────────────────────────────────────────────────────────────────

// Syncs NTP and returns current local hour, or -1 on failure.
static int syncAndGetHour() {
    configTime(NTP_GMT_OFFSET, NTP_DST_OFFSET, NTP_SERVER);

    struct tm timeinfo;
    // Wait up to 5 s for time sync
    for (int i = 0; i < 50; i++) {
        if (getLocalTime(&timeinfo)) {
            Serial.printf("[ntp] Time: %02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min);
            return timeinfo.tm_hour;
        }
        delay(100);
    }
    Serial.println("[ntp] Failed to sync time");
    return -1;
}

// ─── Sleep ────────────────────────────────────────────────────────────────────

static uint64_t getSleepInterval(int hour) {
    bool isNight = (hour == -1)
                || (hour >= NIGHT_HOUR_START)
                || (hour <  NIGHT_HOUR_END);

    uint32_t minutes = isNight ? SLEEP_NIGHTTIME_MIN : SLEEP_DAYTIME_MIN;
    Serial.printf("[sleep] Next update in %u min (%s)\n",
                  minutes, isNight ? "night" : "day");
    return (uint64_t)minutes * 60ULL * 1000000ULL;  // microseconds
}

static uint32_t getSleepIntervalMinutes(int hour) {
    bool isNight = (hour == -1)
                || (hour >= NIGHT_HOUR_START)
                || (hour < NIGHT_HOUR_END);
    return isNight ? SLEEP_NIGHTTIME_MIN : SLEEP_DAYTIME_MIN;
}

#if ENABLE_UI_TUNING_CONSOLE
static bool parseIntStrict(const String &text, int &value) {
    if (text.length() == 0) return false;

    char *endPtr = nullptr;
    long parsed = strtol(text.c_str(), &endPtr, 10);
    if (endPtr == text.c_str() || *endPtr != '\0') return false;

    value = (int)parsed;
    return true;
}

static void printTuningHelp() {
    Serial.println("[ui] Commands:");
    Serial.println("  help                     Show this help");
    Serial.println("  fields                   List editable fields");
    Serial.println("  show                     Show current field values");
    Serial.println("  set <field> <value>      Set field absolute value");
    Serial.println("  nudge <field> <delta>    Add delta to field");
    Serial.println("  redraw                   Redraw using cached weather");
    Serial.println("  fetch                    Fetch weather and redraw");
    Serial.println("  save                     Save current layout to NVS");
    Serial.println("  load                     Load layout from NVS and redraw");
    Serial.println("  reset                    Reset to built-in defaults and redraw");
    Serial.println("  sleep [minutes]          Sleep now (or scheduled interval)");
    Serial.println("  quit                     Exit console and sleep with schedule");
}

static bool handleTuningCommand(const String &line) {
    String trimmed = line;
    trimmed.trim();
    if (trimmed.length() == 0) return false;

    int firstSpace = trimmed.indexOf(' ');
    String cmd = firstSpace < 0 ? trimmed : trimmed.substring(0, firstSpace);
    String rest = firstSpace < 0 ? "" : trimmed.substring(firstSpace + 1);
    cmd.toLowerCase();
    rest.trim();

    if (cmd == "help") {
        printTuningHelp();
        return false;
    }
    if (cmd == "fields") {
        printLayoutFields();
        return false;
    }
    if (cmd == "show") {
        printCurrentLayout();
        return false;
    }
    if (cmd == "redraw") {
        redrawCurrentWeather();
        return false;
    }
    if (cmd == "fetch") {
        fetchAndDrawWeatherForTuning(false);
        return false;
    }
    if (cmd == "save") {
        saveLayoutToPrefs();
        return false;
    }
    if (cmd == "load") {
        loadLayoutFromPrefs();
        redrawCurrentWeather();
        return false;
    }
    if (cmd == "reset") {
        resetDisplayDesign2Layout();
        redrawCurrentWeather();
        Serial.println("[ui] Layout reset to defaults");
        return false;
    }
    if (cmd == "sleep") {
        int minutes = -1;
        if (rest.length() > 0) {
            int parsed;
            if (!parseIntStrict(rest, parsed) || parsed <= 0) {
                Serial.println("[ui] Usage: sleep [minutes]");
                return false;
            }
            minutes = parsed;
        }

        uint64_t us;
        if (minutes > 0) {
            us = (uint64_t)minutes * 60ULL * 1000000ULL;
            Serial.printf("[ui] Sleeping for %d min\n", minutes);
        } else {
            us = getSleepInterval(gLastHour);
            Serial.println("[ui] Sleeping with configured schedule");
        }
        esp_deep_sleep(us);
        return true;
    }
    if (cmd == "quit") {
        Serial.println("[ui] Exiting console");
        return true;
    }

    if (cmd == "set" || cmd == "nudge") {
        int split = rest.indexOf(' ');
        if (split < 0) {
            Serial.println("[ui] Usage: set <field> <value> | nudge <field> <delta>");
            return false;
        }

        String fieldName = rest.substring(0, split);
        String valueText = rest.substring(split + 1);
        fieldName.trim();
        valueText.trim();

        int value;
        if (!parseIntStrict(valueText, value)) {
            Serial.println("[ui] Value must be an integer");
            return false;
        }

        int idx = layoutFieldIndex(fieldName);
        if (idx < 0) {
            Serial.printf("[ui] Unknown field: %s\n", fieldName.c_str());
            printLayoutFields();
            return false;
        }

        DisplayDesign2Layout beforeLayout = getDisplayDesign2Layout();
        int16_t before = beforeLayout.*(kLayoutFields[idx].member);
        int16_t clamped = 0;
        int target = (cmd == "set") ? value : (before + value);
        setLayoutFieldValue(fieldName, target, &clamped);
        Serial.printf("[ui] %s: %d -> %d\n", kLayoutFields[idx].name, (int)before, (int)clamped);
        redrawCurrentWeather();
        return false;
    }

    Serial.printf("[ui] Unknown command: %s\n", cmd.c_str());
    Serial.println("[ui] Type 'help' for available commands");
    return false;
}

static void runUiTuningConsole() {
    Serial.println();
    Serial.println("[ui] Interactive tuning console enabled");
    printTuningHelp();
    Serial.println("[ui] Tip: run 'fetch' after changing conditions or time");
    Serial.print("ui> ");

    while (true) {
        if (!Serial.available()) {
            delay(20);
            continue;
        }

        String line = Serial.readStringUntil('\n');
        bool shouldExit = handleTuningCommand(line);
        if (shouldExit) {
            esp_deep_sleep(getSleepInterval(gLastHour));
            return;
        }
        Serial.print("ui> ");
    }
}
#endif

// ─── Entry point ──────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    bootCount++;
    Serial.printf("\n[boot] Boot #%d\n", bootCount);

    initDisplay();
    updateBatteryMeasurement();

#if ENABLE_UI_WEB_TUNER
    initUserButton();
    updateInfoModeByBootButton();
#endif

#if UI_TUNING_ENABLED
    loadLayoutFromPrefs();
#endif

    WeatherResult cachedWeather;
    bool hasCachedWeather = loadWeatherCache(cachedWeather);
#if UI_TUNING_ENABLED
    if (hasCachedWeather) {
        gLastWeather = cachedWeather;
        gHasWeather = true;
    }
#endif

    bool wifiOk = connectWiFi();
    if (!wifiOk) {
        gCurrentDataInvalidMinutes += getSleepIntervalMinutes(-1);
        setBatteryDisplayPercent(gBatteryPercent);
        setWifiConnectionFailed(true);
        if (hasCachedWeather) {
            WeatherResult displayCached = cachedWeather;
            applyForecastInterpolationIfNeeded(displayCached, gCurrentDataInvalidMinutes);
            drawWeather(displayCached);
            Serial.println("[wifi] Using cached weather after WiFi failure");
        } else {
            drawError("WiFi failed");
        }
#if ENABLE_UI_TUNING_CONSOLE
    #if ENABLE_UI_WEB_TUNER
        if (gInfoScreenMode) {
            Serial.println("[web] WiFi failed in info mode. Staying awake and retrying...");
            return;
        }
    #endif
        Serial.println("[ui] Startup WiFi failed. Use console command 'fetch' to retry.");
        runUiTuningConsole();
        return;
#elif ENABLE_UI_WEB_TUNER
        if (gInfoScreenMode) {
            Serial.println("[web] WiFi failed in info mode. Staying awake and retrying...");
            return;
        }
        Serial.println("[web] WiFi failed outside info mode; sleeping.");
        esp_deep_sleep(getSleepInterval(-1));
        return;
#else
        esp_deep_sleep(getSleepInterval(-1));
#endif
    }

    bool otaTriggered = checkAndHandleMqttOta();
    if (otaTriggered) {
        return;
    }

    int hour = syncAndGetHour();

#if UI_TUNING_ENABLED
    gLastHour = hour;
#endif

    WeatherResult result;
    bool fetched = fetchWeather(result);
    bool currentValidFromHa = fetched && result.currentDataValid;
    if (fetched) {
        applyCachedForecastIfMissing(result);
        if (!result.currentDataValid) {
            gCurrentDataInvalidMinutes += getSleepIntervalMinutes(hour);
            bool hasCurrent = applyInvalidCurrentFallback(result,
                                                          cachedWeather,
                                                          hasCachedWeather,
                                                          gCurrentDataInvalidMinutes);
            if (!hasCurrent) {
                fetched = false;
                Serial.println("[weather] Current data unavailable and no cache to preserve last value");
            }
        } else {
            gCurrentDataInvalidMinutes = 0;
        }
    }
    if (!fetched) {
        gCurrentDataInvalidMinutes += getSleepIntervalMinutes(hour);
#if !ENABLE_UI_WEB_TUNER
#if ENABLE_HA_MQTT
        gMqttClient.disconnect();
#endif
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
#endif
        setBatteryDisplayPercent(gBatteryPercent);
        setWifiConnectionFailed(WiFi.status() != WL_CONNECTED);
        if (hasCachedWeather) {
            WeatherResult displayCached = cachedWeather;
            applyForecastInterpolationIfNeeded(displayCached, gCurrentDataInvalidMinutes);
            drawWeather(displayCached);
            Serial.println("[weather] Using cached weather after fetch failure");
        } else {
            drawError("HA fetch failed");
        }
#if ENABLE_UI_TUNING_CONSOLE
        Serial.println("[ui] Startup weather fetch failed. Use console command 'fetch' to retry.");
        runUiTuningConsole();
        return;
#elif ENABLE_UI_WEB_TUNER
        Serial.println("[web] Startup weather fetch failed. Use /api/fetch from web UI.");
#else
        esp_deep_sleep(getSleepInterval(hour));
#endif
    }

    if (fetched) {
        if (currentValidFromHa) {
            saveWeatherCache(result);
        }
        setWifiConnectionFailed(false);
#if UI_TUNING_ENABLED
        gLastWeather = result;
        gHasWeather = true;
        redrawCurrentWeather();
    #else
        setBatteryDisplayPercent(gBatteryPercent);
        drawWeather(result);
#endif
    }

#if ENABLE_UI_WEB_TUNER
    if (gInfoScreenMode) {
        startUiWebServer();
        drawCurrentInfoScreen();
        return;
    }
#endif

#if ENABLE_UI_TUNING_CONSOLE
    #if !ENABLE_UI_WEB_TUNER
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        runUiTuningConsole();
        return;
    #else
        startUiWebServer();
        return;
    #endif
#elif ENABLE_UI_WEB_TUNER
    startUiWebServer();
    return;
#else
#if ENABLE_HA_MQTT
    gMqttClient.disconnect();
#endif
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_deep_sleep(getSleepInterval(hour));
#endif
}

void loop() {
    serviceMqtt();

#if ENABLE_UI_WEB_TUNER
    bool btnPressed = isUserButtonPressed();
    uint32_t now = millis();
    bool buttonToggleLocked = gWebClientActive || gSdUploadInProgress;
    if (buttonToggleLocked) {
        if (btnPressed && !gButtonPressedLastLoop) {
            Serial.println("[btn] Ignored while web client/upload is active");
        }
    } else if (btnPressed && !gButtonPressedLastLoop && (now - gLastButtonToggleMs) > kButtonDebounceMs) {
        gInfoScreenMode = !gInfoScreenMode;
        gLastButtonToggleMs = now;
        Serial.printf("[btn] Runtime toggle -> %s\n", gInfoScreenMode ? "INFO" : "NORMAL");
        if (gInfoScreenMode) {
            gWebClientActive = false;
            gWebClientEverConnected = false;
            if (WiFi.status() != WL_CONNECTED) {
                connectWiFi();
            }
            if (!gWebServerRunning) {
                startUiWebServer();
            }
            drawCurrentInfoScreen();
        } else {
            if (gHasWeather) {
                redrawCurrentWeather();
            }
            if (gWebClientActive) {
                Serial.println("[web] Main screen active; keeping device awake until client disconnects");
            } else {
                if (webMinimumAwakeElapsed()) {
                    Serial.println("[web] No active client; sleeping from main screen");
                    esp_deep_sleep(getSleepInterval(gLastHour));
                } else {
                    Serial.printf("[web] No active client; staying awake for minimum window (%lu ms)\n",
                                  (unsigned long)UI_WEB_MIN_AWAKE_MS);
                }
            }
        }
    }
    gButtonPressedLastLoop = btnPressed;

    // In info mode we never sleep while trying to connect; keep retrying WiFi/server.
    if (gInfoScreenMode) {
        if (WiFi.status() != WL_CONNECTED && (now - gLastWifiRetryMs) > kWifiRetryMs) {
            gLastWifiRetryMs = now;
            Serial.println("[web] Retrying WiFi for info mode...");
            connectWiFi();
            drawCurrentInfoScreen();
        }

        if (WiFi.status() == WL_CONNECTED && !gWebServerRunning) {
            startUiWebServer();
            drawCurrentInfoScreen();
        }
    }

    if (gWebServerRunning) {
        gWebServer.handleClient();

        if (gWebClientActive) {
            if ((millis() - gWebLastClientMs) > UI_WEB_CLIENT_TIMEOUT_MS) {
                gWebClientActive = false;
                if (webMinimumAwakeElapsed()) {
                    Serial.println("[web] Client disconnected; sleeping");
                    gInfoScreenMode = false;
                    if (gHasWeather) {
                        redrawCurrentWeather();
                    }
                    esp_deep_sleep(getSleepInterval(gLastHour));
                } else {
                    Serial.printf("[web] Client disconnected; staying awake for minimum window (%lu ms)\n",
                                  (unsigned long)UI_WEB_MIN_AWAKE_MS);
                }
            }
        }

        // If no active web session remains, sleep once the minimum awake window has elapsed.
        if (!gInfoScreenMode && !gWebClientActive && webMinimumAwakeElapsed()) {
            Serial.println("[web] Idle and minimum awake elapsed; sleeping");
            esp_deep_sleep(getSleepInterval(gLastHour));
        }

        static uint32_t lastInfoRefreshMs = 0;
        if (gInfoScreenMode && (millis() - lastInfoRefreshMs > 5000)) {
            drawCurrentInfoScreen();
            lastInfoRefreshMs = millis();
        }

        delay(2);
        return;
    }
#endif
    // Deep-sleep mode has nothing to do here.
}
