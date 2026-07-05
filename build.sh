#!/usr/bin/env bash
set -e

SKETCH="chamber_controller.ino"
FQBN="esp32:esp32:esp32c6"

# Create a temporary compilation directory matching the sketch name
mkdir -p build_temp/chamber_controller
cp "$SKETCH" config.h sensor_helper.h build_temp/chamber_controller/

"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" compile \
  --fqbn "$FQBN" \
  --output-dir build/ \
  build_temp/chamber_controller/chamber_controller.ino

rm -rf build_temp
cp version.txt build/version.txt

echo "Build complete. Binary: build/chamber_controller.ino.bin"
