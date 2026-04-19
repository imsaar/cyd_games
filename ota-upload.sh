#!/bin/bash
# OTA firmware upload for CYD Games
# Usage: ./ota-upload.sh <IP> [firmware.bin]

set -e

IP="${1:?Usage: $0 <IP> [firmware.bin]}"
ENV="esp32-2432S028"
BIN="${2:-.pio/build/${ENV}/firmware.bin}"

echo "Building firmware..."
pio run -e "$ENV" || exit 1

if [ ! -f "$BIN" ]; then
    echo "Error: build succeeded but $BIN missing"
    exit 1
fi

echo "Starting OTA to $IP..."
curl -sf "http://$IP/ota/start?mode=firmware" > /dev/null || {
    echo "Error: Cannot reach $IP — is it on WiFi?"
    exit 1
}

SIZE=$(wc -c < "$BIN" | tr -d ' ')
echo "Uploading $(basename "$BIN") (${SIZE} bytes)..."
HTTP_CODE=$(curl -X POST "http://$IP/ota/upload" \
    -F "firmware=@$BIN" \
    --progress-bar \
    -o /dev/null -w "%{http_code}")

if [ "$HTTP_CODE" = "200" ]; then
    echo "Done — device will reboot automatically."
else
    echo "Error: Upload failed (HTTP $HTTP_CODE)"
    exit 1
fi
