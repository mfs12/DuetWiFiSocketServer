#!/usr/bin/bash

#set -x

HOST="192.168.0.8"
PORT="80"
PASSWORD=""
IMAGE="build/dwss-image.bin"
#FIRMWARE="wifi" # rrf, pd, tb, boot, dev, dev2
FIRMWARE=$1

case "${FIRMWARE}" in
  "boot")
    esptool.py \
      --chip esp8266 \
      --port $2 \
      --baud 115200 \
      --before default_reset \
      --after hard_reset write_flash \
      -z \
      --flash_mode dio --flash_freq 26m --flash_size 2MB \
      0x0 build/bootloader/bootloader.bin \
      0x8000 build/partition_table/partition-table.bin \

    exit $?
    ;;
  "dev")
    esptool.py \
      --chip esp8266 \
      --port $2 \
      --baud 115200 \
      --before default_reset \
      --after hard_reset write_flash \
      -z \
      --flash_mode dio --flash_freq 26m --flash_size 2MB \
      0x10000 build/dwss.bin

    exit $?
    ;;
  "dev2")
    esptool.py \
      --chip esp8266 \
      --port $2 \
      --baud 115200 \
      --before default_reset \
      --after hard_reset write_flash \
      -z \
      --flash_mode dio --flash_freq 26m --flash_size 2MB \
      0x0 build/dwss-image.bin

    exit $?
    ;;
  "pd")
    FIRMWARE_ID=4
    ;;
  "rrf")
    FIRMWARE_ID=0
    ;;
  "tb")
    FIRMWARE_ID=3
    ;;
  "wifi")
    FIRMWARE_ID=1
    if test -n "$2"; then
      HOST=$2
    fi
    ;;
  *)
    echo "$0 wifi|dev|dev2 TTY|IP"
    exit 1
    ;;
esac

while ! ping -c 1 "${HOST}"; do
  echo "Waiting for host: ${HOST}"
  sleep 1
done

URL="http://${HOST}/rr_connect?password=\"$PASSWORD\""
while ! curl ${URL} | jq '.err == 0' | rg '^true$'; do
  echo "Waiting for initial connection to device"
  sleep 1
done

URL="http://${HOST}/rr_upload?name=0%3A%2Ffirmware%2FDuetWiFiServer-curl.bin"
curl \
  ${URL} \
  -X POST -H 'Accept: */*' \
  --compressed -H 'Content-Type: application/octet-stream' \
  -H 'DNT: 1' \
  --data-binary @${IMAGE} | jq '.err == 0' | rg '^true$'

if test $? -ne 0; then
  echo "Failed to upload file"
  exit 1
fi

sleep 3

URL="http://${HOST}/rr_gcode?gcode=M997%20S${FIRMWARE_ID}%20P%22DuetWiFiServer-curl.bin%22"
curl \
  ${URL} \
  -H 'Accept: */*' \
  --compressed \
  -H 'Content-Type: application/json' \
  -H 'DNT: 1' \
  -H 'Connection: keep-alive' | jq '.buff > 0' | rg '^true$'

echo "Uploaded and update started..."
exit 0
# wait for update to start
echo "Waiting for device to start updated"
sleep 10

while ! ping -c 1 "${HOST}"; do
  echo "Waiting for host: ${HOST}"
  sleep 1
done
