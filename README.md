# SolarStation

ESP32-based solar station firmware for panel positioning, output scheduling, diagnostics, and network control.

This README reflects the current project status after the latest firmware refactor.

## Current Status

The project is in an active integration phase.

Implemented and working in firmware:

- FastAccelStepper integration with 3 stepper channels initialized in hardware.
- Axis control on real steppers:
	- Azimuth controller uses Stepper1.
	- Elevation controller uses Stepper2.
- Configurable axis parameters (persisted):
	- Min/max angle limits.
	- Steps-per-degree ratio.
	- Step speed and acceleration.
- Portal dashboard + configuration UI (`/` and `/portal`) with sections for:
	- Solar tracking settings.
	- Axis settings.
	- Wi-Fi and NTP.
	- Output manual/auto control and schedules.
	- Hardware diagnostics (limit switches, driver DIAG, currents, sunrise/sunset).
- Output scheduler (3 outputs) with:
	- Fixed time or sunrise/sunset windows.
	- Duty cycle control.
	- AUTO/MANUAL mode per output.
- Wi-Fi behavior:
	- STA when configured.
	- AP fallback/setup mode when STA is not configured or fails.
- Sunset return preposition behavior:
	- Always active.
	- Executes once per target day.
	- Uses a fixed delay after local sunset before prepositioning to next sunrise target.
- Diagnostics override workflow:
	- Dedicated page (`/diag` and `/portal/diag`) to force azimuth/elevation for testing.
	- Canceling test mode returns panel to current calculated solar position.

## Hardware Notes (Current Wiring Assumptions)

- MCU: ESP32.
- Stepper drivers: TMC2209 (3 channels).
- I/O expander: PCA9538 (I2C) for shared microstep pins, status LED, and limit switch inputs.
- External SPI EEPROM module support included (`M95PxxModule`).

## Main Runtime Modules

- `SolarStation.ino`: orchestration, hardware init, scheduler, diagnostics helpers, sunset return, test override state.
- `ConfigModule.h` / `ConfigModule.ino`: persisted configuration storage and defaults.
- `AzimuthController.*`: azimuth axis control on FastAccelStepper.
- `ElevationController.*`: elevation axis control on FastAccelStepper.
- `WebServerModule.*`: synchronous web server routes and HTML placeholder binding.
- `WifiModule.*`: STA/AP behavior and reconnect strategy.
- `portal/index.html`: AP/setup portal page.
- `portal/diag.html`: diagnostics override page.
- `console/index.html`: full network console page.

Web UI deployment model:

- Local UI files are served from device filesystem (LittleFS/SPIFFS data partition).
- Use the `data` folder content and upload it with `tools/upload_spiffs.ps1` after firmware upload.
- Filesystem layout:
	- `data/portal/*` for AP/setup pages.
	- `data/console/*` for full network console pages.

## Web Endpoints (Current)

Main pages:

- `GET /` main dashboard/configuration page.
- `GET /portal` AP/setup portal page.
- `GET /diag` diagnostics test page.
- `GET /portal/diag` diagnostics test page.
- `GET /console` full network console page.
- `GET /diag/status` plain-text status snapshot for diagnostics page refresh.

Tracking diagnostics actions:

- `POST /setTrackingOverride` (form fields: `azimuth`, `elevation`).
- `POST /cancelTrackingOverride`.

Configuration actions (examples):

- `POST /saveSolarTrackingConfig`
- `POST /saveAzimuthConfig`
- `POST /saveElevationConfig`
- `POST /saveNTPConfig`
- `POST /saveWiFiConfig`
- `POST /saveScheduleConfig`

## Configuration Persistence

Configuration is stored with ESP32 Preferences.

- Main configuration uses a raw blob (`ConfigData_t`).
- Output AUTO/MANUAL flags are stored in separate keys (`out_auto_0..2`).

Important:

- Any structural change to `ConfigData_t` size causes existing blob data to be considered incompatible and defaults are restored on next boot.

## Build Portability (Another PC)

This project now includes the async network libraries in the project `libraries` folder, so the same code and patches follow the project:

- `libraries/ESPAsyncWebServer` (v3.1.0, with local compatibility patches)
- `libraries/AsyncTCP` (v1.1.4)
- `libraries/ESPAsyncTCP`

To build on another computer:

1. Install Arduino IDE 2.x.
2. Install `esp32` board platform in Boards Manager.
3. Open `SolarStation.ino` from this project folder (do not copy only the `.ino` file).
4. Compile directly; do not install another conflicting `ESPAsyncWebServer` manually unless you intentionally want to replace the bundled one.

Recommended:

- If you already have other async web libraries globally installed in your Arduino libraries folder, keep versions aligned with this project to avoid API/link mismatches.

## Known Limitations / Work In Progress

- Some legacy/placeholder areas are still under cleanup.
- Arduino IntelliSense can report false compile errors in editor context even when firmware logic is valid.
- Full production solar tracking loop policy (beyond targeted move and preposition behavior) may still need refinement for final deployment scenarios.

## Quick Validation Checklist

- Open `/` and verify dashboard values update.
- Confirm Wi-Fi mode (STA or AP fallback) is shown correctly.
- In configuration tabs, set axis limits/ratio/speed/acceleration and save.
- Use `/diag`:
	- Apply manual override.
	- Cancel test and verify return to current calculated position.
- Verify sunset return preposition occurs after sunset + fixed delay.

## License

Project files contain their own headers where applicable.
