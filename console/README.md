# SolarStation Console (ESP32-hosted)

This console UI is hosted directly by the ESP32 from filesystem storage.

Routes served by firmware:

- `/console`
- `/console/index.html`
- `/console/styles.css`
- `/console/app.js`

Deployment:

1. Keep source files in this folder for editing.
2. Copy or mirror files into `data/console`.
3. Upload SPIFFS with `tools/upload_spiffs.ps1`.

Notes:

- The console consumes the same device API endpoints as local pages.
- In AP mode, the default base URL is usually `http://192.168.4.1`.