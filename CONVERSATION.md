# Project Conversation Log

## Initial Request
User requested a new GitHub project for a T5-2.13inch E-paper display with the following requirements:
- Use PlatformIO
- Connect to Home Assistant
- Low power with variable update rate
- Show current weather and forecast

## Project Details Defined

### Hardware
- LilyGO TTGO T5 2.13" E-Paper display
- ESP32-WROOM-32 microcontroller
- Panel variant: GxEPD2_213_B74

### Software Stack
- PlatformIO (Arduino framework)
- GxEPD2 library for E-paper control
- ArduinoJson for parsing Home Assistant API responses
- ArduinoHttpClient for HTTP requests
- Home Assistant REST API integration
- Deep sleep for low power operation

## Project Structure Defined

```
epaper-weather-display/
├── platformio.ini
├── src/
│   ├── main.cpp
│   ├── config.h
│   ├── weather.h
│   └── display.h
└── data/
```

## Files Created

### platformio.ini
```ini
[env:ttgo-t5-213]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600

lib_deps =
  ZinggJM/GxEPD2 @ ^1.6.0
  bblanchon/ArduinoJson @ ^7.0.0
  arduino-libraries/ArduinoHttpClient @ ^0.6.0

build_flags =
  -DCORE_DEBUG_LEVEL=0
```

### src/config.h
- WiFi credentials (SSID, password)
- Static IP support for faster connection
- Home Assistant host, port, token
- Weather entity ID
- E-paper pin definitions (CS=5, DC=17, RST=16, BUSY=4)
- Deep sleep intervals: 15min daytime, 60min nighttime
- Night hours: 22:00 - 07:00

### src/weather.h
- WeatherData struct (condition, temperature, humidity, wind_speed, pressure)
- ForecastEntry struct (condition, tempHigh, tempLow, datetime)
- fetchWeather() function using Home Assistant REST API
- Parses up to 5-day forecast

### src/display.h
- GxEPD2_BW display instance (250x122)
- conditionIcon() maps HA condition strings to symbols
- shortDay() parses ISO 8601 datetime to weekday
- drawWeather() renders current + forecast on screen
- drawError() renders error messages
- display.hibernate() called after each refresh to save power

### src/main.cpp
- RTC_DATA_ATTR bootCount survives deep sleep
- connectWiFi() with static IP and timeout support
- getSleepInterval() returns adaptive interval based on time of day
- NTP time sync via pool.ntp.org
- Full cycle: wake -> WiFi -> fetch -> display -> sleep

## Key Features

### Low Power Design
| Phase | Duration | Current |
|---|---|---|
| Deep sleep | ~14.5 min | ~10 µA |
| WiFi connect + fetch | ~3-5 s | ~90 mA* |
| E-paper refresh | ~2 s | ~30 mA |
| Avg (15 min cycle) | - | ~1.1 mA* |

*After power optimisations (see below). Estimated battery life: ~38 days on 1000mAh LiPo.

### Power Optimisations Implemented

| Optimisation | Saving | Notes |
|---|---|---|
| Bluetooth disabled at compile time | ~25 mA / boot | `-DCONFIG_BT_ENABLED=0` in `platformio.ini` |
| CPU frequency 80 MHz (was 240 MHz) | ~30 % CPU current | `setCpuFrequencyMhz(80)` at boot |
| Static IP (no DHCP) | ~1 s WiFi-on time | `USE_STATIC_IP` in `config.h` |
| WiFi TX power 11 dBm (was 20 dBm) | lower TX current | adequate for home router range |
| NTP sync cached in RTC memory | 1 network RTT per `NTP_SYNC_INTERVAL` wakes | re-syncs every 8th wake by default |
| Active time deducted from sleep budget | true cadence maintained | avoids drift accumulation |
| `display.hibernate()` after every refresh | ~0.1 mA standby eliminated | called in both `drawWeather` and `drawError` |
| WiFi fully powered off before deep sleep | no residual radio current | `WiFi.mode(WIFI_OFF)` + `esp_wifi_stop()` |
| Battery voltage monitoring | awareness of state | oversampled ADC on GPIO 35 |

### Adaptive Update Rate
- Daytime (07:00-22:00): update every 15 minutes
- Nighttime (22:00-07:00): update every 60 minutes
- Configurable in `config.h` (`SLEEP_INTERVAL_DAY_S`, `SLEEP_INTERVAL_NIGHT_S`)

### Home Assistant Integration
- Uses Long-Lived Access Token
- Calls /api/states/{entity_id} REST endpoint
- Parses current weather + forecast array
- Requires weather entity with forecast attribute

## Development Roadmap

### Phase 1 - Core (Current)
- [x] Basic weather fetch from Home Assistant
- [x] Display current weather + forecast
- [x] Deep sleep + adaptive update rate
- [x] Battery level display
- [ ] Stable OTA updates

### Phase 2 - UI Improvements
- [ ] Custom bitmap weather icons
- [ ] Partial screen refresh
- [ ] Better font layout
- [ ] Sunrise/sunset times display

### Phase 3 - Configuration & Reliability
- [ ] Web portal for WiFi + HA token configuration
- [ ] Watchdog timer
- [ ] MQTT support as alternative to REST API
- [ ] Configurable timezone

### Phase 4 - Advanced Features
- [ ] Solar panel + battery management
- [ ] Multiple weather location support
- [ ] Indoor temperature/humidity via HA sensor
- [ ] Air quality index display

## Next Steps
1. Create GitHub repository (sasensio/epaper-weather-display)
2. Push all project files
3. Clone locally and configure src/config.h
4. Flash to device and test
5. Iterate on display layout and features