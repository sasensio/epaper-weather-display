#pragma once

#include <Arduino.h>
#include <time.h>
#include <SD.h>
#include <SPI.h>
#include <GxEPD2_BW.h>

#include "config.h"
#include "weather.h"

// GxEPD2_213_B74 — 250x122, SSD1680
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(
    GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

struct DisplayDesign2Layout {
    int16_t lastUpdateX;
    int16_t lastUpdateY;
    int16_t lastUpdateLineSpacing;
    int16_t currentIconX;
    int16_t currentIconY;
    int16_t currentIconSize;
    int16_t currentTempX;
    int16_t currentTempY;
    int16_t currentTempJustify;
    int16_t feelTempX;
    int16_t feelTempY;
    int16_t feelDegreeX;
    int16_t feelDegreeY;
    int16_t feelCX;
    int16_t feelCY;
    int16_t highRightX;
    int16_t highY;
    int16_t lowRightX;
    int16_t lowY;
    int16_t dividerY;
    int16_t forecastHourY;
    int16_t forecastTempY;
    int16_t forecastIconOffsetX;
    int16_t forecastIconY;
    int16_t forecastIconSize;
    int16_t lastUpdateFontSize;
    int16_t lastUpdateFontFamily;
    int16_t currentTempFontSize;
    int16_t currentTempFontFamily;
    int16_t feelTempFontSize;
    int16_t feelTempFontFamily;
    int16_t highTempFontSize;
    int16_t highTempFontFamily;
    int16_t lowTempFontSize;
    int16_t lowTempFontFamily;
    int16_t forecastHourFontSize;
    int16_t forecastHourFontFamily;
    int16_t forecastTempFontSize;
    int16_t forecastTempFontFamily;
    int16_t batteryX;
    int16_t batteryY;
};

static const int16_t FONT_FAMILY_SANS = 0;
static const int16_t FONT_FAMILY_MONO = 1;
static const int16_t FONT_FAMILY_SERIF = 2;
static const int16_t JUSTIFY_LEFT = 0;
static const int16_t JUSTIFY_CENTER = 1;
static const int16_t JUSTIFY_RIGHT = 2;

enum FontId : uint8_t {
    FONT_ID_SANS_7 = 0,
    FONT_ID_SANS_9,
    FONT_ID_SANS_12,
    FONT_ID_SANS_18,
    FONT_ID_SANS_24,
    FONT_ID_SANS_32,
    FONT_ID_SANS_BOLD_7,
    FONT_ID_SANS_BOLD_9,
    FONT_ID_SANS_BOLD_12,
    FONT_ID_SANS_BOLD_18,
    FONT_ID_SANS_BOLD_24,
    FONT_ID_SANS_BOLD_32,
    FONT_ID_MONO_7,
    FONT_ID_COUNT
};

struct SdFontGlyph {
    uint32_t bitmapOffset;
    uint8_t width;
    uint8_t height;
    uint8_t xAdvance;
    int8_t xOffset;
    int8_t yOffset;
};

struct SdFontMeta {
    bool loaded;
    bool valid;
    uint8_t first;
    uint8_t last;
    uint8_t yAdvance;
    uint16_t glyphCount;
    uint32_t bitmapStart;
};

static SdFontMeta gSdFontMeta[FONT_ID_COUNT] = {};
static SdFontGlyph gSdFontGlyphs[FONT_ID_COUNT][95] = {};

static FontId selectFont(int16_t family, int16_t size, bool bold);
static void drawText(const String &text, int16_t x, int16_t baselineY, FontId font);
static void drawTextRight(const String &text, int16_t rightX, int16_t baselineY, FontId font);
static void drawTextCentered(const String &text, int16_t centerX, int16_t baselineY, FontId font);

static int16_t normalizeFontSize(int16_t size) {
    if (size <= 8)  return 7;
    if (size <= 10) return 9;
    if (size <= 15) return 12;
    if (size <= 21) return 18;
    if (size <= 30) return 24;
    return 32;
}

static int16_t normalizeFontFamily(int16_t family) {
    (void)family;
    return FONT_FAMILY_SANS;
}

static int16_t normalizeJustify(int16_t justify) {
    return constrain(justify, JUSTIFY_LEFT, JUSTIFY_RIGHT);
}

static FontId selectFont(int16_t family, int16_t size, bool bold) {
    (void)family;
    int16_t s = normalizeFontSize(size);
    if (bold) {
        if (s == 32) return FONT_ID_SANS_BOLD_32;
        if (s == 24) return FONT_ID_SANS_BOLD_24;
        if (s == 18) return FONT_ID_SANS_BOLD_18;
        if (s == 12) return FONT_ID_SANS_BOLD_12;
        if (s == 9)  return FONT_ID_SANS_BOLD_9;
        return FONT_ID_SANS_BOLD_7;
    }
    if (s == 32) return FONT_ID_SANS_32;
    if (s == 24) return FONT_ID_SANS_24;
    if (s == 18) return FONT_ID_SANS_18;
    if (s == 12) return FONT_ID_SANS_12;
    if (s == 9)  return FONT_ID_SANS_9;
    return FONT_ID_SANS_7;
}

static DisplayDesign2Layout defaultDisplayDesign2Layout() {
    DisplayDesign2Layout l;
    l.lastUpdateX = 1;
    l.lastUpdateY = 29;
    l.lastUpdateLineSpacing = 15;
    l.currentIconX = 55;
    l.currentIconY = 21;
    l.currentIconSize = 24;
    l.currentTempX = 150;
    l.currentTempY = 46;
    l.currentTempJustify = JUSTIFY_RIGHT;
    l.feelTempX = 160;
    l.feelTempY = 20;
    l.feelDegreeX = 155;
    l.feelDegreeY = 25;
    l.feelCX = 160;
    l.feelCY = 42;
    l.highRightX = 238;
    l.highY = 20;
    l.lowRightX = 238;
    l.lowY = 45;
    l.dividerY = 62;
    l.forecastHourY = 80;
    l.forecastTempY = 99;
    l.forecastIconOffsetX = 7;
    l.forecastIconY = 106;
    l.forecastIconSize = 13;
    l.lastUpdateFontSize = 7;
    l.lastUpdateFontFamily = FONT_FAMILY_SANS;
    l.currentTempFontSize = 32;
    l.currentTempFontFamily = FONT_FAMILY_SANS;
    l.feelTempFontSize = 12;
    l.feelTempFontFamily = FONT_FAMILY_SANS;
    l.highTempFontSize = 12;
    l.highTempFontFamily = FONT_FAMILY_SANS;
    l.lowTempFontSize = 12;
    l.lowTempFontFamily = FONT_FAMILY_SANS;
    l.forecastHourFontSize = 9;
    l.forecastHourFontFamily = FONT_FAMILY_SANS;
    l.forecastTempFontSize = 9;
    l.forecastTempFontFamily = FONT_FAMILY_SANS;
    l.batteryX = 7;
    l.batteryY = 45;
    return l;
}

static DisplayDesign2Layout gDesign2Layout = defaultDisplayDesign2Layout();

static int gBatteryDisplayPercent = -1;
static bool gWifiConnectionFailed = false;

static const int kIconSlotCount = 9;
static bool gSdInitAttempted = false;
static bool gSdAvailable = false;
static uint32_t gSdLastAttemptMs = 0;
static const uint32_t kSdRetryMs = 2000;
#if ENABLE_SD_ICONS || ENABLE_SD_FONTS
static SPIClass gSdSpi(HSPI);
#endif
static bool gSdIconLoaded[kIconSlotCount] = {false};
static bool gSdIconValid[kIconSlotCount] = {false};
static uint16_t gSdIconRows[kIconSlotCount][16] = {{0}};

static void clearSdAssetCaches() {
    for (int i = 0; i < kIconSlotCount; i++) {
        gSdIconLoaded[i] = false;
        gSdIconValid[i] = false;
    }
    for (uint8_t i = 0; i < FONT_ID_COUNT; i++) {
        gSdFontMeta[i].loaded = false;
        gSdFontMeta[i].valid = false;
        gSdFontMeta[i].first = 0;
        gSdFontMeta[i].last = 0;
        gSdFontMeta[i].yAdvance = 0;
        gSdFontMeta[i].glyphCount = 0;
        gSdFontMeta[i].bitmapStart = 0;
    }
}

static bool initSdCardNow() {
#if ENABLE_SD_ICONS || ENABLE_SD_FONTS
    // Ensure other SPI devices are deselected before talking to the SD card.
    pinMode(EPD_CS, OUTPUT);
    digitalWrite(EPD_CS, HIGH);
    pinMode(SD_CARD_CS_PIN, OUTPUT);
    digitalWrite(SD_CARD_CS_PIN, HIGH);
    gSdSpi.begin(SD_CARD_SCK_PIN, SD_CARD_MISO_PIN, SD_CARD_MOSI_PIN, SD_CARD_CS_PIN);
    bool ok = SD.begin(SD_CARD_CS_PIN, gSdSpi);
    Serial.printf("[sd] %s (CS=%d SCK=%d MISO=%d MOSI=%d)\n",
                  ok ? "ready" : "not found",
                  SD_CARD_CS_PIN,
                  SD_CARD_SCK_PIN,
                  SD_CARD_MISO_PIN,
                  SD_CARD_MOSI_PIN);
    return ok;
#else
    return false;
#endif
}

inline void setBatteryDisplayPercent(int pct) {
    gBatteryDisplayPercent = pct;
}

inline void setWifiConnectionFailed(bool failed) {
    gWifiConnectionFailed = failed;
}

static int iconSlotForCondition(const String &cond) {
    if (cond == "sunny" || cond == "clear-night") return 0;
    if (cond == "partlycloudy") return 1;
    if (cond == "cloudy") return 2;
    if (cond == "rainy" || cond == "pouring") return 3;
    if (cond == "snowy" || cond == "snowy-rainy") return 4;
    if (cond == "lightning" || cond == "lightning-rainy") return 5;
    if (cond == "windy" || cond == "windy-variant") return 6;
    if (cond == "fog" || cond == "hail") return 7;
    return 8;
}

static const char *iconFilePathForSlot(int slot) {
    switch (slot) {
        case 0: return "/icons/sunny.mask16";
        case 1: return "/icons/partlycloudy.mask16";
        case 2: return "/icons/cloudy.mask16";
        case 3: return "/icons/rainy.mask16";
        case 4: return "/icons/snowy.mask16";
        case 5: return "/icons/lightning.mask16";
        case 6: return "/icons/windy.mask16";
        case 7: return "/icons/fog.mask16";
        default: return "/icons/unknown.mask16";
    }
}

static bool parseMask16File(const char *path, uint16_t outRows[16]) {
    File f = SD.open(path, FILE_READ);
    if (!f) {
        return false;
    }

    char line[24];
    for (int row = 0; row < 16; row++) {
        int len = f.readBytesUntil('\n', line, sizeof(line) - 1);
        if (len <= 0) {
            f.close();
            return false;
        }
        line[len] = '\0';

        uint16_t bits = 0;
        int bitCount = 0;
        for (int i = 0; i < len; i++) {
            if (line[i] == '\r') continue;
            if (line[i] == '0' || line[i] == '1') {
                bits = (uint16_t)((bits << 1) | (line[i] == '1' ? 1 : 0));
                bitCount++;
                if (bitCount == 16) break;
            }
        }
        if (bitCount != 16) {
            f.close();
            return false;
        }
        outRows[row] = bits;
    }

    f.close();
    return true;
}

static bool ensureSdReady() {
    if (gSdAvailable) {
        return true;
    }

    uint32_t now = millis();
    if (gSdInitAttempted && (now - gSdLastAttemptMs) < kSdRetryMs) {
        return false;
    }

    gSdInitAttempted = true;
    gSdLastAttemptMs = now;
    gSdAvailable = initSdCardNow();
    if (gSdAvailable) {
        clearSdAssetCaches();
    }
    return gSdAvailable;
}

static bool forceSdRemount() {
    gSdAvailable = false;
    gSdInitAttempted = false;
    return ensureSdReady();
}

static const uint16_t *getSdIconRowsForCondition(const String &cond) {
    if (!ensureSdReady()) return nullptr;

    int slot = iconSlotForCondition(cond);
    if (slot < 0 || slot >= kIconSlotCount) return nullptr;

    if (!gSdIconLoaded[slot]) {
        gSdIconLoaded[slot] = true;
        gSdIconValid[slot] = parseMask16File(iconFilePathForSlot(slot), gSdIconRows[slot]);
        if (!gSdIconValid[slot]) {
            Serial.printf("[icons] Missing/invalid: %s\n", iconFilePathForSlot(slot));
        }
    }

    return gSdIconValid[slot] ? gSdIconRows[slot] : nullptr;
}

static bool iconMaskBit16(const uint16_t *rows, int16_t sx, int16_t sy) {
    if (!rows) return false;
    if (sx < 0 || sx >= 16 || sy < 0 || sy >= 16) return false;
    uint16_t row = rows[sy];
    return (row & (uint16_t)(1u << (15 - sx))) != 0;
}

static void drawIconMask16(const uint16_t *rows, int16_t x, int16_t y, int16_t size) {
    size = constrain(size, 8, 40);
    const bool smallIcon = size <= 20;
    for (int16_t dy = 0; dy < size; dy++) {
        int16_t sy = (dy * 16) / size;
        for (int16_t dx = 0; dx < size; dx++) {
            int16_t sx = (dx * 16) / size;
            if (!iconMaskBit16(rows, sx, sy)) {
                continue;
            }

            display.drawPixel(x + dx, y + dy, GxEPD_BLACK);

#if ICON_RENDER_BOLD
            if (smallIcon) {
                if (dx + 1 < size) display.drawPixel(x + dx + 1, y + dy, GxEPD_BLACK);
                if (dy + 1 < size) display.drawPixel(x + dx, y + dy + 1, GxEPD_BLACK);
            }
#endif
        }
    }
}

static void clampDesign2Layout(DisplayDesign2Layout &l) {
    l.lastUpdateX = constrain(l.lastUpdateX, 0, 240);
    l.lastUpdateY = constrain(l.lastUpdateY, 6, 40);
    l.lastUpdateLineSpacing = constrain(l.lastUpdateLineSpacing, 6, 24);
    l.currentIconX = constrain(l.currentIconX, 0, 240);
    l.currentIconY = constrain(l.currentIconY, 0, 80);
    l.currentIconSize = constrain(l.currentIconSize, 8, 40);
    l.currentTempX = constrain(l.currentTempX, 0, 240);
    l.currentTempY = constrain(l.currentTempY, 12, 70);
    l.currentTempJustify = normalizeJustify(l.currentTempJustify);
    l.feelTempX = constrain(l.feelTempX, 0, 240);
    l.feelTempY = constrain(l.feelTempY, 10, 60);
    l.feelDegreeX = constrain(l.feelDegreeX, 0, 245);
    l.feelDegreeY = constrain(l.feelDegreeY, 10, 80);
    l.feelCX = constrain(l.feelCX, 0, 245);
    l.feelCY = constrain(l.feelCY, 10, 80);
    l.highRightX = constrain(l.highRightX, 80, 249);
    l.highY = constrain(l.highY, 10, 70);
    l.lowRightX = constrain(l.lowRightX, 80, 249);
    l.lowY = constrain(l.lowY, 10, 90);
    l.dividerY = constrain(l.dividerY, 40, 90);
    l.forecastHourY = constrain(l.forecastHourY, l.dividerY + 8, 112);
    l.forecastTempY = constrain(l.forecastTempY, l.forecastHourY + 8, 116);
    l.forecastIconOffsetX = constrain(l.forecastIconOffsetX, 0, 20);
    // Keep icon in forecast band, but allow moving above or below forecast text.
    l.forecastIconY = constrain(l.forecastIconY, l.dividerY + 2, 118);
    l.forecastIconSize = constrain(l.forecastIconSize, 8, 24);
    l.lastUpdateFontSize = normalizeFontSize(l.lastUpdateFontSize);
    l.lastUpdateFontFamily = normalizeFontFamily(l.lastUpdateFontFamily);
    l.currentTempFontSize = normalizeFontSize(l.currentTempFontSize);
    l.currentTempFontFamily = normalizeFontFamily(l.currentTempFontFamily);
    l.feelTempFontSize = normalizeFontSize(l.feelTempFontSize);
    l.feelTempFontFamily = normalizeFontFamily(l.feelTempFontFamily);
    l.highTempFontSize = normalizeFontSize(l.highTempFontSize);
    l.highTempFontFamily = normalizeFontFamily(l.highTempFontFamily);
    l.lowTempFontSize = normalizeFontSize(l.lowTempFontSize);
    l.lowTempFontFamily = normalizeFontFamily(l.lowTempFontFamily);
    l.forecastHourFontSize = normalizeFontSize(l.forecastHourFontSize);
    l.forecastHourFontFamily = normalizeFontFamily(l.forecastHourFontFamily);
    l.forecastTempFontSize = normalizeFontSize(l.forecastTempFontSize);
    l.forecastTempFontFamily = normalizeFontFamily(l.forecastTempFontFamily);
    l.batteryX = constrain(l.batteryX, 0, 220);
    l.batteryY = constrain(l.batteryY, 0, 110);
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

static uint8_t weatherDisplayRotation() {
#if DISPLAY_ROTATE_180
    return 3;  // 180° from landscape rotation 1
#else
    return 1;
#endif
}

static String titleCaseWords(const String &input) {
    String formatted = input;
    formatted.replace("-", " ");

    bool upperNext = true;
    for (int i = 0; i < formatted.length(); i++) {
        char c = formatted[i];
        if (c == ' ') {
            upperNext = true;
            continue;
        }

        formatted.setCharAt(i, upperNext ? static_cast<char>(toupper(c)) : static_cast<char>(tolower(c)));
        upperNext = false;
    }

    return formatted;
}

static String localizedCondition(const String &cond) {
#if DISPLAY_LANGUAGE == DISPLAY_LANG_ES
    if (cond == "sunny") return "Soleado";
    if (cond == "clear-night") return "Despejado";
    if (cond == "partlycloudy") return "Parcial nublado";
    if (cond == "cloudy") return "Nublado";
    if (cond == "rainy") return "Lluvia";
    if (cond == "pouring") return "Lluvia fuerte";
    if (cond == "snowy") return "Nieve";
    if (cond == "snowy-rainy") return "Aguanieve";
    if (cond == "lightning") return "Tormenta";
    if (cond == "lightning-rainy") return "Tormenta lluvia";
    if (cond == "windy") return "Viento";
    if (cond == "windy-variant") return "Viento nubes";
    if (cond == "fog") return "Niebla";
    if (cond == "hail") return "Granizo";
    return titleCaseWords(cond);
#elif DISPLAY_LANGUAGE == DISPLAY_LANG_DE
    if (cond == "sunny") return "Sonnig";
    if (cond == "clear-night") return "Klar";
    if (cond == "partlycloudy") return "Teilwolkig";
    if (cond == "cloudy") return "Bewolkt";
    if (cond == "rainy") return "Regen";
    if (cond == "pouring") return "Starkregen";
    if (cond == "snowy") return "Schnee";
    if (cond == "snowy-rainy") return "Schneeregen";
    if (cond == "lightning") return "Gewitter";
    if (cond == "lightning-rainy") return "Gewitterregen";
    if (cond == "windy") return "Windig";
    if (cond == "windy-variant") return "Windig bewolkt";
    if (cond == "fog") return "Nebel";
    if (cond == "hail") return "Hagel";
    return titleCaseWords(cond);
#else
    return titleCaseWords(cond);
#endif
}

static const char *localizedFeelsLabel() {
#if DISPLAY_LANGUAGE == DISPLAY_LANG_ES
    return "Sens:";
#elif DISPLAY_LANGUAGE == DISPLAY_LANG_DE
    return "Gefuhlt:";
#else
    return "Feels:";
#endif
}

static const char *localizedNoForecastLabel() {
#if DISPLAY_LANGUAGE == DISPLAY_LANG_ES
    return "Sin pronostico";
#elif DISPLAY_LANGUAGE == DISPLAY_LANG_DE
    return "Keine Prognose";
#else
    return "No forecast data";
#endif
}

static const char *localizedErrorTitle() {
#if DISPLAY_LANGUAGE == DISPLAY_LANG_ES
    return "ERROR";
#elif DISPLAY_LANGUAGE == DISPLAY_LANG_DE
    return "FEHLER";
#else
    return "ERROR";
#endif
}

static const char *fontFilePath(FontId font) {
    switch (font) {
        case FONT_ID_SANS_7: return "/fonts/free_sans_7.gfxf";
        case FONT_ID_SANS_9: return "/fonts/free_sans_9.gfxf";
        case FONT_ID_SANS_12: return "/fonts/free_sans_12.gfxf";
        case FONT_ID_SANS_18: return "/fonts/free_sans_18.gfxf";
        case FONT_ID_SANS_24: return "/fonts/free_sans_24.gfxf";
        case FONT_ID_SANS_32: return "/fonts/free_sans_32.gfxf";
        case FONT_ID_SANS_BOLD_7: return "/fonts/free_sans_bold_7.gfxf";
        case FONT_ID_SANS_BOLD_9: return "/fonts/free_sans_bold_9.gfxf";
        case FONT_ID_SANS_BOLD_12: return "/fonts/free_sans_bold_12.gfxf";
        case FONT_ID_SANS_BOLD_18: return "/fonts/free_sans_bold_18.gfxf";
        case FONT_ID_SANS_BOLD_24: return "/fonts/free_sans_bold_24.gfxf";
        case FONT_ID_SANS_BOLD_32: return "/fonts/free_sans_bold_32.gfxf";
        case FONT_ID_MONO_7: return "/fonts/free_mono_7.gfxf";
        default: return nullptr;
    }
}

static int16_t fontPointSize(FontId font) {
    switch (font) {
        case FONT_ID_SANS_32:
        case FONT_ID_SANS_BOLD_32:
            return 32;
        case FONT_ID_SANS_24:
        case FONT_ID_SANS_BOLD_24:
            return 24;
        case FONT_ID_SANS_18:
        case FONT_ID_SANS_BOLD_18:
            return 18;
        case FONT_ID_SANS_12:
        case FONT_ID_SANS_BOLD_12:
            return 12;
        case FONT_ID_SANS_9:
        case FONT_ID_SANS_BOLD_9:
            return 9;
        default:
            return 7;
    }
}

static uint8_t fallbackTextScale(FontId font) {
    int16_t size = fontPointSize(font);
    if (size >= 30) return 4;
    if (size >= 22) return 3;
    if (size >= 16) return 2;
    return 1;
}

static bool readU16LE(File &f, uint16_t &out) {
    int b0 = f.read();
    int b1 = f.read();
    if (b0 < 0 || b1 < 0) return false;
    out = (uint16_t)((uint16_t)b0 | ((uint16_t)b1 << 8));
    return true;
}

static bool readU32LE(File &f, uint32_t &out) {
    int b0 = f.read();
    int b1 = f.read();
    int b2 = f.read();
    int b3 = f.read();
    if (b0 < 0 || b1 < 0 || b2 < 0 || b3 < 0) return false;
    out = (uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 24);
    return true;
}

static bool ensureSdFontLoaded(FontId font) {
#if !ENABLE_SD_FONTS
    (void)font;
    return false;
#else
    if ((uint8_t)font >= FONT_ID_COUNT) return false;
    SdFontMeta &meta = gSdFontMeta[(uint8_t)font];
    if (meta.loaded) return meta.valid;
    meta.loaded = true;
    meta.valid = false;

    if (!ensureSdReady()) {
        return false;
    }

    const char *path = fontFilePath(font);
    if (!path) {
        return false;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[font] Missing: %s\n", path);
        return false;
    }

    char magic[4];
    if (f.readBytes(magic, sizeof(magic)) != (int)sizeof(magic) ||
        magic[0] != 'G' || magic[1] != 'F' || magic[2] != 'X' || magic[3] != 'F') {
        f.close();
        Serial.printf("[font] Bad magic: %s\n", path);
        return false;
    }

    int version = f.read();
    int first = f.read();
    int last = f.read();
    int yAdvance = f.read();
    if (version != 1 || first < 0 || last < 0 || yAdvance < 0) {
        f.close();
        Serial.printf("[font] Bad header: %s\n", path);
        return false;
    }

    uint16_t glyphCount = 0;
    uint32_t bitmapSize = 0;
    if (!readU16LE(f, glyphCount) || !readU32LE(f, bitmapSize)) {
        f.close();
        Serial.printf("[font] Short header: %s\n", path);
        return false;
    }

    if (glyphCount == 0 || glyphCount > 95 || (uint8_t)first > (uint8_t)last) {
        f.close();
        Serial.printf("[font] Unsupported glyph count in %s\n", path);
        return false;
    }

    meta.first = (uint8_t)first;
    meta.last = (uint8_t)last;
    meta.yAdvance = (uint8_t)yAdvance;
    meta.glyphCount = glyphCount;
    meta.bitmapStart = 14u + ((uint32_t)glyphCount * 9u);

    for (uint16_t i = 0; i < glyphCount; i++) {
        SdFontGlyph &g = gSdFontGlyphs[(uint8_t)font][i];
        uint32_t off = 0;
        if (!readU32LE(f, off)) {
            f.close();
            Serial.printf("[font] Short glyph table: %s\n", path);
            return false;
        }
        int w = f.read();
        int h = f.read();
        int xa = f.read();
        int xo = f.read();
        int yo = f.read();
        if (w < 0 || h < 0 || xa < 0 || xo < -128 || yo < -128) {
            f.close();
            return false;
        }
        g.bitmapOffset = off;
        g.width = (uint8_t)w;
        g.height = (uint8_t)h;
        g.xAdvance = (uint8_t)xa;
        g.xOffset = (int8_t)xo;
        g.yOffset = (int8_t)yo;
    }

    (void)bitmapSize;
    f.close();
    meta.valid = true;
    return true;
#endif
}

static bool measureSdTextBounds(const String &text,
                                int16_t x,
                                int16_t baselineY,
                                FontId font,
                                int16_t *x1,
                                int16_t *y1,
                                uint16_t *w,
                                uint16_t *h) {
    if (!ensureSdFontLoaded(font)) return false;

    const SdFontMeta &meta = gSdFontMeta[(uint8_t)font];
    const SdFontGlyph *glyphs = gSdFontGlyphs[(uint8_t)font];
    int16_t cursorX = x;
    bool haveBounds = false;
    int16_t minX = 32767;
    int16_t minY = 32767;
    int16_t maxX = -32768;
    int16_t maxY = -32768;

    for (uint16_t i = 0; i < text.length(); i++) {
        uint8_t c = (uint8_t)text[i];
        if (c == '\n' || c == '\r') break;
        if (c < meta.first || c > meta.last) {
            cursorX += meta.yAdvance / 2;
            continue;
        }

        const SdFontGlyph &g = glyphs[c - meta.first];
        if (g.width > 0 && g.height > 0) {
            int16_t gx1 = cursorX + g.xOffset;
            int16_t gy1 = baselineY + g.yOffset;
            int16_t gx2 = gx1 + g.width - 1;
            int16_t gy2 = gy1 + g.height - 1;
            if (!haveBounds) {
                minX = gx1;
                minY = gy1;
                maxX = gx2;
                maxY = gy2;
                haveBounds = true;
            } else {
                minX = min(minX, gx1);
                minY = min(minY, gy1);
                maxX = max(maxX, gx2);
                maxY = max(maxY, gy2);
            }
        }
        cursorX += g.xAdvance;
    }

    if (!haveBounds) {
        *x1 = x;
        *y1 = baselineY;
        *w = 0;
        *h = 0;
        return true;
    }

    *x1 = minX;
    *y1 = minY;
    *w = (uint16_t)(maxX - minX + 1);
    *h = (uint16_t)(maxY - minY + 1);
    return true;
}

static bool drawSdText(const String &text, int16_t x, int16_t baselineY, FontId font) {
    if (!ensureSdFontLoaded(font)) return false;

    const SdFontMeta &meta = gSdFontMeta[(uint8_t)font];
    const SdFontGlyph *glyphs = gSdFontGlyphs[(uint8_t)font];
    const char *path = fontFilePath(font);
    if (!path) return false;

    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    int16_t cursorX = x;
    for (uint16_t i = 0; i < text.length(); i++) {
        uint8_t c = (uint8_t)text[i];
        if (c == '\n' || c == '\r') break;
        if (c < meta.first || c > meta.last) {
            cursorX += meta.yAdvance / 2;
            continue;
        }

        const SdFontGlyph &g = glyphs[c - meta.first];
        if (g.width > 0 && g.height > 0) {
            uint32_t bitmapPos = meta.bitmapStart + g.bitmapOffset;
            if (!f.seek(bitmapPos)) {
                continue;
            }
            int16_t drawX = cursorX + g.xOffset;
            int16_t drawY = baselineY + g.yOffset;
            uint32_t totalBits = (uint32_t)g.width * (uint32_t)g.height;
            uint8_t bits = 0;
            for (uint32_t bitIndex = 0; bitIndex < totalBits; bitIndex++) {
                if ((bitIndex & 7u) == 0u) {
                    int b = f.read();
                    if (b < 0) break;
                    bits = (uint8_t)b;
                }
                if (bits & 0x80u) {
                    uint16_t localX = (uint16_t)(bitIndex % g.width);
                    uint16_t localY = (uint16_t)(bitIndex / g.width);
                    display.drawPixel(drawX + (int16_t)localX, drawY + (int16_t)localY, GxEPD_BLACK);
                }
                bits <<= 1;
            }
        }
        cursorX += g.xAdvance;
    }

    f.close();
    return true;
}

static void measureTextWithFont(const String &text,
                                int16_t x,
                                int16_t baselineY,
                                FontId font,
                                int16_t *x1,
                                int16_t *y1,
                                uint16_t *w,
                                uint16_t *h) {
    if (measureSdTextBounds(text, x, baselineY, font, x1, y1, w, h)) {
        return;
    }

    uint8_t scale = fallbackTextScale(font);
    display.setFont();
    display.setTextSize(scale);
    display.getTextBounds(text, x, baselineY, x1, y1, w, h);
    display.setTextSize(1);
}

static void drawTextRightSmall(const String &text, int16_t rightX, int16_t baselineY) {
    int16_t x1, y1;
    uint16_t w, h;
    display.setFont();
    display.setTextSize(1);
    display.getTextBounds(text, 0, baselineY, &x1, &y1, &w, &h);
    display.setCursor(rightX - x1 - w, baselineY);
    display.print(text);
}

static void drawText(const String &text, int16_t x, int16_t baselineY, FontId font) {
    if (drawSdText(text, x, baselineY, font)) {
        return;
    }

    uint8_t scale = fallbackTextScale(font);
    display.setFont();
    display.setTextSize(scale);
    display.setCursor(x, baselineY);
    display.print(text);
    display.setTextSize(1);
}

static void drawTextRight(const String &text, int16_t rightX, int16_t baselineY, FontId font) {
    int16_t x1, y1;
    uint16_t w, h;
    measureTextWithFont(text, 0, baselineY, font, &x1, &y1, &w, &h);
    drawText(text, rightX - x1 - w, baselineY, font);
}

static void drawTextCentered(const String &text, int16_t centerX, int16_t baselineY, FontId font) {
    int16_t x1, y1;
    uint16_t w, h;
    measureTextWithFont(text, 0, baselineY, font, &x1, &y1, &w, &h);
    drawText(text, centerX - ((int16_t)w / 2) - x1, baselineY, font);
}

static String formatTempValue(float temperature) {
    return String(lroundf(temperature));
}

static int16_t degreeGlyphScale(FontId font) {
    int16_t size = fontPointSize(font);
    if (size >= 18) {
        return 2;
    }
    return 1;
}

static int16_t degreeGlyphWidth(int16_t scale) {
    return 6 * scale;
}

static void drawDegreeGlyph(int16_t x, int16_t y, int16_t scale) {
    display.cp437(true);
    display.setFont();
    display.setTextSize(scale);
    display.setCursor(x, y);
    display.write((uint8_t)248);  // CP437 degree sign
    display.setTextSize(1);
}

static int16_t tempRenderWidth(float temperature, int16_t baselineY, FontId font) {
    int16_t x1n, y1n, x1c, y1c;
    uint16_t wn, hn, wc, hc;
    measureTextWithFont(formatTempValue(temperature), 0, baselineY, font, &x1n, &y1n, &wn, &hn);
    measureTextWithFont("C", 0, baselineY, font, &x1c, &y1c, &wc, &hc);

    int16_t degreeScale = degreeGlyphScale(font);
    int16_t degreeWidth = degreeGlyphWidth(degreeScale);
    const int16_t gap = 2;
    return (int16_t)wn + gap + degreeWidth + gap + (int16_t)wc;
}

static void drawTempLeft(float temperature, int16_t x, int16_t baselineY, FontId font) {
    String value = formatTempValue(temperature);
    int16_t x1n, y1n, x1c, y1c;
    uint16_t wn, hn, wc, hc;

    drawText(value, x, baselineY, font);
    measureTextWithFont(value, x, baselineY, font, &x1n, &y1n, &wn, &hn);
    measureTextWithFont("C", 0, baselineY, font, &x1c, &y1c, &wc, &hc);

    int16_t degreeScale = degreeGlyphScale(font);
    int16_t degreeWidth = degreeGlyphWidth(degreeScale);
    int16_t valueRight = x1n + (int16_t)wn;
    int16_t degreeX = valueRight + 2;
    int16_t degreeY = y1c + 2;
    drawDegreeGlyph(degreeX, degreeY, degreeScale);

    drawText("C", valueRight + 2 + degreeWidth + 2, baselineY, font);
}

static void drawTempRight(float temperature, int16_t rightX, int16_t baselineY, FontId font) {
    int16_t width = tempRenderWidth(temperature, baselineY, font);
    drawTempLeft(temperature, rightX - width, baselineY, font);
}

static void drawTempCentered(float temperature, int16_t centerX, int16_t baselineY, FontId font) {
    int16_t width = tempRenderWidth(temperature, baselineY, font);
    drawTempLeft(temperature, centerX - width / 2, baselineY, font);
}

static void drawLabelWithTemp(const String &label, float temperature, int16_t x, int16_t baselineY, FontId font) {
    int16_t x1, y1;
    uint16_t w, h;
    measureTextWithFont(label, x, baselineY, font, &x1, &y1, &w, &h);
    drawText(label, x, baselineY, font);
    drawTempLeft(temperature, x1 + (int16_t)w + 4, baselineY, font);
}

static String formatHourLabel(const String &iso) {
    if (iso.length() < 13) return "--";
    return iso.substring(11, 13) + "h";
}

static String formatHourLabelCompact(const String &iso) {
    if (iso.length() < 13) return "--";
    String hour = iso.substring(11, 13);
    if (hour.length() == 2 && hour[0] == '0') {
        return hour.substring(1);
    }
    return hour;
}

static String formatLastUpdateLabel() {
    time_t now = time(nullptr);
    if (now <= 0) {
        return "--/-- --:--";
    }

    struct tm localTm;
    localtime_r(&now, &localTm);

    char buf[15];
    snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d", localTm.tm_mday, localTm.tm_mon + 1,
             localTm.tm_hour, localTm.tm_min);
    return String(buf);
}

static String formatDateLabel() {
    time_t now = time(nullptr);
    if (now <= 0) return "--/--";
    struct tm t;
    localtime_r(&now, &t);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d/%02d", t.tm_mday, t.tm_mon + 1);
    return String(buf);
}

static String formatTimeLabel() {
    time_t now = time(nullptr);
    if (now <= 0) return "--:--";
    struct tm t;
    localtime_r(&now, &t);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return String(buf);
}

static void drawBatteryIcon(int16_t x, int16_t y, int pct) {
    // Body: 18x8, nub: 2x4 on right side, 4 inner segments
    const int16_t bodyW = 18;
    const int16_t bodyH = 8;
    const int16_t nubW  = 2;
    const int16_t nubH  = 4;
    const int16_t numSegs = 4;
    const int16_t segW  = 3;
    const int16_t segH  = bodyH - 2;
    const int16_t segGap = 1;

    display.drawRect(x, y, bodyW, bodyH, GxEPD_BLACK);
    display.fillRect(x + bodyW, y + (bodyH - nubH) / 2, nubW, nubH, GxEPD_BLACK);

    int filled = (pct < 0) ? 0 : constrain((numSegs * pct + 50) / 100, 0, (int)numSegs);
    for (int16_t i = 0; i < numSegs; i++) {
        int16_t sx = x + 1 + i * (segW + segGap);
        if (i < filled) {
            display.fillRect(sx, y + 1, segW, segH, GxEPD_BLACK);
        }
    }
}

static void drawWifiStatusIcon(int16_t x, int16_t y, bool failed) {
    // Compact 13x10 Wi-Fi glyph with optional cross overlay.
    display.drawLine(x + 0, y + 5, x + 6, y + 0, GxEPD_BLACK);
    display.drawLine(x + 12, y + 5, x + 6, y + 0, GxEPD_BLACK);
    display.drawLine(x + 3, y + 5, x + 6, y + 2, GxEPD_BLACK);
    display.drawLine(x + 9, y + 5, x + 6, y + 2, GxEPD_BLACK);
    display.fillCircle(x + 6, y + 7, 1, GxEPD_BLACK);

    if (failed) {
        display.drawLine(x + 0, y + 0, x + 12, y + 8, GxEPD_BLACK);
        display.drawLine(x + 12, y + 0, x + 0, y + 8, GxEPD_BLACK);
    }
}

static time_t parseIsoDateTimeToLocalEpoch(const String &iso) {
    // Expected minimum shape: YYYY-MM-DDTHH:MM
    if (iso.length() < 16) return (time_t)-1;

    if (iso[4] != '-' || iso[7] != '-' || (iso[10] != 'T' && iso[10] != ' ') || iso[13] != ':') {
        return (time_t)-1;
    }

    int year = iso.substring(0, 4).toInt();
    int mon = iso.substring(5, 7).toInt();
    int day = iso.substring(8, 10).toInt();
    int hour = iso.substring(11, 13).toInt();
    int minute = iso.substring(14, 16).toInt();

    if (year < 1970 || mon < 1 || mon > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59) {
        return (time_t)-1;
    }

    struct tm tmValue;
    memset(&tmValue, 0, sizeof(tmValue));
    tmValue.tm_year = year - 1900;
    tmValue.tm_mon = mon - 1;
    tmValue.tm_mday = day;
    tmValue.tm_hour = hour;
    tmValue.tm_min = minute;
    tmValue.tm_sec = 0;
    tmValue.tm_isdst = -1;
    return mktime(&tmValue);
}

static int firstForecastIndexForCurrentTime(const WeatherResult &r) {
    if (r.forecastCount <= 0) return 0;

    time_t now = time(nullptr);
    if (now <= 0) {
        return min(1, r.forecastCount - 1);
    }

    struct tm localNow;
    localtime_r(&now, &localNow);

    int leadHours = (localNow.tm_min < 30) ? 1 : 2;
    // Build exact hour target:
    // h:00-h:29 => (h+1):00, h:30-h:59 => (h+2):00
    struct tm targetTm = localNow;
    targetTm.tm_min = 0;
    targetTm.tm_sec = 0;
    targetTm.tm_hour += leadHours;
    time_t target = mktime(&targetTm);
    if (target <= 0) {
        return min(leadHours, r.forecastCount - 1);
    }

    for (int i = 0; i < r.forecastCount; i++) {
        time_t forecastEpoch = parseIsoDateTimeToLocalEpoch(r.forecast[i].datetime);
        if (forecastEpoch >= target) {
            return i;
        }
    }

    return r.forecastCount - 1;
}

static void drawCloudShape(int16_t x, int16_t y, int16_t size) {
    int16_t left = x + size / 8;
    int16_t baseY = y + (size * 3) / 4;
    int16_t width = (size * 3) / 4;
    display.drawCircle(left + size / 5, baseY - size / 5, size / 5, GxEPD_BLACK);
    display.drawCircle(left + size / 2, baseY - size / 3, size / 4, GxEPD_BLACK);
    display.drawCircle(left + (size * 13) / 20, baseY - size / 6, size / 5, GxEPD_BLACK);
    display.drawFastHLine(left, baseY, width, GxEPD_BLACK);
    display.drawLine(left, baseY, left + size / 10, y + size / 2, GxEPD_BLACK);
    display.drawLine(left + width, baseY, left + width - size / 12, y + size / 2, GxEPD_BLACK);
}

static void drawSunShape(int16_t x, int16_t y, int16_t size) {
    int16_t cx = x + size / 2;
    int16_t cy = y + size / 2;
    int16_t radius = (size / 5 > 3) ? size / 5 : 3;
    int16_t ray = (size / 8 > 2) ? size / 8 : 2;
    display.drawCircle(cx, cy, radius, GxEPD_BLACK);
    display.drawFastVLine(cx, cy - radius - ray, ray, GxEPD_BLACK);
    display.drawFastVLine(cx, cy + radius + 1, ray, GxEPD_BLACK);
    display.drawFastHLine(cx - radius - ray, cy, ray, GxEPD_BLACK);
    display.drawFastHLine(cx + radius + 1, cy, ray, GxEPD_BLACK);
    display.drawLine(cx - radius - ray + 1, cy - radius - ray + 1, cx - radius, cy - radius, GxEPD_BLACK);
    display.drawLine(cx + radius, cy + radius, cx + radius + ray - 1, cy + radius + ray - 1, GxEPD_BLACK);
    display.drawLine(cx + radius, cy - radius, cx + radius + ray - 1, cy - radius - ray + 1, GxEPD_BLACK);
    display.drawLine(cx - radius - ray + 1, cy + radius + ray - 1, cx - radius, cy + radius, GxEPD_BLACK);
}

static void drawRainShape(int16_t x, int16_t y, int16_t size) {
    drawCloudShape(x, y, size);
    int16_t startY = y + (size * 4) / 5;
    display.drawLine(x + size / 4, startY, x + size / 5, startY + size / 6, GxEPD_BLACK);
    display.drawLine(x + size / 2, startY, x + size / 2 - size / 12, startY + size / 6, GxEPD_BLACK);
    display.drawLine(x + (size * 3) / 4, startY, x + (size * 3) / 4 - size / 12, startY + size / 6, GxEPD_BLACK);
}

static void drawSnowShape(int16_t x, int16_t y, int16_t size) {
    drawCloudShape(x, y, size);
    int16_t flakeY = y + (size * 17) / 20;
    for (int i = 0; i < 2; i++) {
        int16_t cx = x + size / 3 + i * (size / 4);
        display.drawFastHLine(cx - 2, flakeY, 5, GxEPD_BLACK);
        display.drawFastVLine(cx, flakeY - 2, 5, GxEPD_BLACK);
        display.drawLine(cx - 2, flakeY - 2, cx + 2, flakeY + 2, GxEPD_BLACK);
        display.drawLine(cx - 2, flakeY + 2, cx + 2, flakeY - 2, GxEPD_BLACK);
    }
}

static void drawLightningShape(int16_t x, int16_t y, int16_t size) {
    drawCloudShape(x, y, size);
    int16_t midX = x + size / 2;
    int16_t topY = y + size / 2;
    display.drawLine(midX, topY, midX - size / 8, topY + size / 5, GxEPD_BLACK);
    display.drawLine(midX - size / 8, topY + size / 5, midX + size / 14, topY + size / 5, GxEPD_BLACK);
    display.drawLine(midX + size / 14, topY + size / 5, midX - size / 10, y + size - 2, GxEPD_BLACK);
}

static void drawFogShape(int16_t x, int16_t y, int16_t size) {
    drawCloudShape(x, y, size - 2);
    int16_t lineY = y + size - 3;
    display.drawFastHLine(x + size / 6, lineY, size / 2, GxEPD_BLACK);
    display.drawFastHLine(x + size / 4, lineY + 3, size / 2, GxEPD_BLACK);
}

static void drawWindShape(int16_t x, int16_t y, int16_t size) {
    int16_t midY = y + size / 2;
    display.drawFastHLine(x + size / 6, midY - 3, size / 2, GxEPD_BLACK);
    display.drawLine(x + (size * 2) / 3, midY - 3, x + (size * 5) / 6, midY - 6, GxEPD_BLACK);
    display.drawFastHLine(x, midY + 2, (size * 2) / 3, GxEPD_BLACK);
    display.drawLine(x + (size * 2) / 3, midY + 2, x + (size * 5) / 6, midY + 5, GxEPD_BLACK);
}

static void drawUnknownShape(int16_t x, int16_t y, int16_t size) {
    display.drawRect(x + size / 4, y + size / 6, size / 2, (size * 2) / 3, GxEPD_BLACK);
    display.drawCircle(x + size / 2, y + size / 3, 1, GxEPD_BLACK);
    display.drawLine(x + size / 2, y + size / 3 + 3, x + size / 2, y + (size * 3) / 5, GxEPD_BLACK);
    display.drawCircle(x + size / 2, y + (size * 3) / 4, 1, GxEPD_BLACK);
}

struct AsciiIcon {
    const char *line1;
    const char *line2;
    const char *line3;
    const char *compact;
};

static AsciiIcon weatherAsciiIcon(const String &cond) {
    if (cond == "sunny" || cond == "clear-night") {
        return {"\\ | /", "- o -", "/ | \\", "SUN"};
    }
    if (cond == "partlycloudy") {
        return {"\\ |  ", " .--.", "(___)", "PCL"};
    }
    if (cond == "cloudy") {
        return {"      ", " .--.", "(___)", "CLD"};
    }
    if (cond == "rainy" || cond == "pouring") {
        return {" .--.", "(___)", " ' ' ", "RAN"};
    }
    if (cond == "snowy" || cond == "snowy-rainy") {
        return {" .--.", "(___)", " * * ", "SNW"};
    }
    if (cond == "lightning" || cond == "lightning-rainy") {
        return {" .--.", "(___)", " /Z  ", "THN"};
    }
    if (cond == "windy" || cond == "windy-variant") {
        return {" ~~> ", " ~~> ", "  ~~>", "WND"};
    }
    if (cond == "fog" || cond == "hail") {
        return {" .--.", "~~~~~", "~~~~~", "FOG"};
    }
    return {"[ ? ]", "[ ? ]", "[ ? ]", "UNK"};
}

static void drawWeatherIcon(const String &cond, int16_t x, int16_t y, int16_t size) {
    const uint16_t *sdRows = getSdIconRowsForCondition(cond);
    if (sdRows) {
        drawIconMask16(sdRows, x, y, size);
        return;
    }

    size = constrain(size, 10, 40);
    int16_t boxW = size;
    int16_t boxH = max((int16_t)10, (int16_t)(size - 2));

    // Icons are externalized; if SD/mapping is missing, draw compact ASCII-art icon.
    display.drawRect(x, y, boxW, boxH, GxEPD_BLACK);

    AsciiIcon icon = weatherAsciiIcon(cond);
    if (size <= 14 || boxH < 16) {
        drawTextCentered(String(icon.compact), x + (boxW / 2), y + boxH - 2, FONT_ID_SANS_BOLD_7);
        return;
    }

    const int16_t centerX = x + (boxW / 2);
    int16_t baseline1 = y + 6;
    int16_t baseline2 = y + 12;
    int16_t baseline3 = y + 18;
    if (boxH < 20) {
        baseline3 = y + boxH - 2;
        baseline2 = baseline3 - 6;
        baseline1 = baseline2 - 6;
    }

    drawTextCentered(String(icon.line1), centerX, baseline1, FONT_ID_MONO_7);
    drawTextCentered(String(icon.line2), centerX, baseline2, FONT_ID_MONO_7);
    drawTextCentered(String(icon.line3), centerX, baseline3, FONT_ID_MONO_7);
}

static void drawWeatherDesign1(const WeatherResult &r) {
    const int16_t screenW = 250;
    const int16_t dividerY = 62;
    String conditionLabel = localizedCondition(r.current.condition);
    String lastUpdate = formatLastUpdateLabel();

    float todayHigh = r.current.temperature;
    float todayLow = r.current.temperature;
    if (r.forecastCount > 0) {
        todayHigh = r.forecast[0].temperature;
        todayLow = r.forecast[0].temperature;
        for (int i = 1; i < r.forecastCount; i++) {
            todayHigh = max(todayHigh, r.forecast[i].temperature);
            todayLow = min(todayLow, r.forecast[i].temperature);
        }
    }

    drawTempLeft(r.current.temperature, 6, 34, FONT_ID_SANS_BOLD_18);
    drawTempRight(todayHigh, 146, 20, FONT_ID_SANS_BOLD_12);
    drawTempRight(todayLow, 146, 42, FONT_ID_SANS_BOLD_12);
    drawLabelWithTemp(localizedFeelsLabel(), r.current.apparent_temperature, 8, 56, FONT_ID_SANS_9);
    drawTextRightSmall(lastUpdate, screenW - 8, 6);
    if (gWifiConnectionFailed) {
        drawWifiStatusIcon(screenW - 15, 8, true);
    }
    drawWeatherIcon(r.current.condition, screenW - 52, 18, 24);
    drawTextRight(conditionLabel, screenW - 10, 54, FONT_ID_SANS_9);

    display.drawFastHLine(0, dividerY, screenW, GxEPD_BLACK);

    int count = min(r.forecastCount, 4);
    int colW  = screenW / (count > 0 ? count : 1);

    if (count == 0) {
        drawText(localizedNoForecastLabel(), 8, 98, FONT_ID_SANS_12);
    }

    for (int i = 0; i < count; i++) {
        const ForecastEntry &fe = r.forecast[i];
        int xCenter = i * colW + (colW / 2);

        drawTextCentered(formatHourLabel(fe.datetime), xCenter, 78, FONT_ID_SANS_BOLD_9);
        drawWeatherIcon(fe.condition, xCenter - 10, 81, 20);
        drawTextCentered(formatTempValue(fe.temperature), xCenter, 118, FONT_ID_SANS_9);
    }
}

static void drawWeatherDesign2(const WeatherResult &r) {
    const int16_t screenW = 250;
    const DisplayDesign2Layout &ui = gDesign2Layout;
    FontId lastUpdateFont = selectFont(ui.lastUpdateFontFamily, ui.lastUpdateFontSize, false);
    FontId currentTempFont = selectFont(ui.currentTempFontFamily, ui.currentTempFontSize, true);
    FontId feelTempFont = selectFont(ui.feelTempFontFamily, ui.feelTempFontSize, false);
    FontId highFont = selectFont(ui.highTempFontFamily, ui.highTempFontSize, true);
    FontId lowFont = selectFont(ui.lowTempFontFamily, ui.lowTempFontSize, true);
    FontId forecastHourFont = selectFont(ui.forecastHourFontFamily, ui.forecastHourFontSize, false);
    FontId forecastTempFont = selectFont(ui.forecastTempFontFamily, ui.forecastTempFontSize, false);

    float topHigh = r.current.temperature;
    float topLow = r.current.temperature;
    if (r.forecastCount > 0) {
        topHigh = r.forecast[0].temperature;
        topLow = r.forecast[0].temperature;
        for (int i = 1; i < r.forecastCount; i++) {
            topHigh = max(topHigh, r.forecast[i].temperature);
            topLow = min(topLow, r.forecast[i].temperature);
        }
    }

    // Two-line date + time
    int16_t lineSpacing = ui.lastUpdateLineSpacing;
    drawText(formatDateLabel(), ui.lastUpdateX, ui.lastUpdateY - lineSpacing, lastUpdateFont);
    drawText(formatTimeLabel(), ui.lastUpdateX, ui.lastUpdateY, lastUpdateFont);

    // Battery icon below the time line
    drawBatteryIcon(ui.batteryX, ui.batteryY, gBatteryDisplayPercent);
    if (gWifiConnectionFailed) {
        drawWifiStatusIcon(screenW - 15, 8, true);
    }

    drawWeatherIcon(r.current.condition, ui.currentIconX, ui.currentIconY, ui.currentIconSize);
    String currentTempText = formatTempValue(r.current.temperature);
    if (ui.currentTempJustify == JUSTIFY_RIGHT) {
        drawTextRight(currentTempText, ui.currentTempX, ui.currentTempY, currentTempFont);
    } else if (ui.currentTempJustify == JUSTIFY_CENTER) {
        drawTextCentered(currentTempText, ui.currentTempX, ui.currentTempY, currentTempFont);
    } else {
        drawText(currentTempText, ui.currentTempX, ui.currentTempY, currentTempFont);
    }
    drawText(formatTempValue(r.current.apparent_temperature), ui.feelTempX, ui.feelTempY, feelTempFont);
    drawDegreeGlyph(ui.feelDegreeX, ui.feelDegreeY, 1);
    drawText("C", ui.feelCX, ui.feelCY, feelTempFont);
    drawTempRight(topHigh, ui.highRightX, ui.highY, highFont);
    drawTempRight(topLow, ui.lowRightX, ui.lowY, lowFont);

    display.drawFastHLine(0, ui.dividerY, screenW, GxEPD_BLACK);

    int forecastStartIndex = firstForecastIndexForCurrentTime(r);
    const int forecastStep = 2;

    int available = 0;
    if (forecastStartIndex < r.forecastCount) {
        available = 1 + ((r.forecastCount - 1 - forecastStartIndex) / forecastStep);
    }
    int count = min(available, MAX_FORECAST);
    int colW  = screenW / (count > 0 ? count : 1);

    if (count == 0) {
        drawText(localizedNoForecastLabel(), 8, 98, FONT_ID_SANS_12);
    }

    for (int i = 0; i < count; i++) {
        const int sourceIndex = forecastStartIndex + (i * forecastStep);
        const ForecastEntry &fe = r.forecast[sourceIndex];
        int xCenter = i * colW + (colW / 2);

        drawTextCentered(formatHourLabelCompact(fe.datetime), xCenter, ui.forecastHourY, forecastHourFont);
        drawTextCentered(formatTempValue(fe.temperature), xCenter, ui.forecastTempY, forecastTempFont);
        drawWeatherIcon(fe.condition, xCenter - ui.forecastIconOffsetX, ui.forecastIconY, ui.forecastIconSize);
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

inline void initDisplay() {
    display.init(115200, true, 2, false);
    ensureSdReady();
}

inline DisplayDesign2Layout getDisplayDesign2Layout() {
    return gDesign2Layout;
}

inline void setDisplayDesign2Layout(const DisplayDesign2Layout &layout) {
    gDesign2Layout = layout;
    clampDesign2Layout(gDesign2Layout);
}

inline void resetDisplayDesign2Layout() {
    gDesign2Layout = defaultDisplayDesign2Layout();
}

// Renders current weather plus a compact next-12-hours forecast.
inline void drawWeather(const WeatherResult &r) {
    display.setRotation(weatherDisplayRotation());
    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
#if DISPLAY_DESIGN == DISPLAY_DESIGN_2
        drawWeatherDesign2(r);
#else
        drawWeatherDesign1(r);
#endif

    } while (display.nextPage());

    display.hibernate();
}

// Renders a short error message centred on the screen.
inline void drawError(const String &message) {
    display.setRotation(weatherDisplayRotation());
    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        drawText(localizedErrorTitle(), 4, 26, FONT_ID_SANS_BOLD_12);
        drawText(message, 4, 50, FONT_ID_SANS_9);
    } while (display.nextPage());

    display.hibernate();
}

inline void drawWebConnectInfo(const String &ip, uint16_t port, bool clientConnected) {
    display.setRotation(weatherDisplayRotation());
    display.setFullWindow();
    display.firstPage();

    String url = String("http://") + ip + ":" + String(port) + "/";
    String status = clientConnected ? String("Client connected") : String("Waiting client");

    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        drawText("TUNING MODE", 4, 20, FONT_ID_SANS_BOLD_12);
        drawText("Open:", 4, 42, FONT_ID_SANS_9);
        drawText(url, 4, 58, FONT_ID_SANS_9);
        drawText(status, 4, 80, FONT_ID_SANS_9);
        drawText("BTN IO39 toggles mode", 4, 104, FONT_ID_SANS_9);
    } while (display.nextPage());

    display.hibernate();
}
