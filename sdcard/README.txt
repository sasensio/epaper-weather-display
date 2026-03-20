SD card payload for external weather icons and fonts.

Copy this folder content to the root of your SD card:
- /icons/*.mask16
- /icons/manifest.json
- /fonts/*.gfxf
- /ui/index.html

File format: .mask16
- 16 lines
- each line has 16 chars
- '1' = black pixel, '0' = white pixel
- top-left pixel is first char of first line

Condition-to-icon mapping is defined in /icons/manifest.json.
Aliases are included for Home Assistant weather states.

Example load path (future firmware):
- /icons/sunny.mask16
- /icons/rainy.mask16

Note:
These files are exported from the current firmware icon set and are ready
for SD-based icon loading integration.

Font format: .gfxf
- binary font payload exported from Adafruit GFX-compatible headers
- includes glyph metrics + packed bitmap bytes
- loaded lazily at runtime to reduce firmware flash usage

Regenerate fonts after changing source font headers:
- from repo root run: `python3 tools/export_sd_fonts.py`

Update SD files without removing the card:
- enable web tuner in firmware: `ENABLE_UI_WEB_TUNER true`
- open tuner page: `http://<device-ip>/`
- use `Upload SD File` with destination path under `/icons/`, `/fonts/`, or `/ui/`

Web tuner page location:
- firmware serves `/ui/index.html` from SD card at `/`
- if missing, firmware shows a minimal fallback page while APIs remain available

API upload endpoint:
- `POST /api/sd/upload?path=/fonts/free_sans_9.gfxf`
- multipart/form-data body with one file field (name can be `file`)

Example with curl:
- `curl -F "file=@sdcard/fonts/free_sans_9.gfxf" "http://<device-ip>/api/sd/upload?path=/fonts/free_sans_9.gfxf"`
