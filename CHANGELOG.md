# Changelog

All notable changes to filament-dryer-esp32 are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/).

## [0.0.1-b01] — 2026-07-05

### Added
- **Dynamic Dual-Sensor Monitoring** — Implemented automatic querying and run-time validation of dual BME280/BMP280 sensor layouts (ambient core at `0x77` and heater exhaust guard at `0x76` on a shared I2C bus) with seamless single-sensor mirroring and fallback to prevent unwanted lockouts.
- **Exhaust Safety Guard** — Added a strict safety cutoff against `MAX_HEATER_EXHAUST_TEMP` (default `70.0°C`). If exceeded, physical heater power is cut and the fan is forced to 100% to cool the exhaust and protect filament spools from warping or melting.
- **Home Assistant Auto-Discovery for Exhaust Sensor** — Integrates dynamic auto-registration of the secondary "Heater Exhaust Temp" entity, which cleanly unregisters/removes itself from the Home Assistant dashboard if a secondary sensor is physically absent.
- **NVS Magic Key & Passive Migration** — Added standard NVS configuration validation and passive version-check key generation matching the moisture sensor design.
- **Initial Captive Setup Portal** — Visual setup hotspot (`DRYER-XXXXXX`, password `dryer123`) redirecting users to a secure dark-mode responsive setup panel to provision WiFi, MQTT, and Unit Segment properties.
- **Drying Control Loop & Presets** — Embedded material library presets (PLA, PETG, CoPE, ASA, ABS, TPU) with automated holding thresholds, countdown timers, thermal runaway protections, and periodic 30-second ventilation purges.
