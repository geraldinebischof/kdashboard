#!/bin/sh

# Kindle-side launcher for the native planner dashboard.
# Copy this file to /mnt/us/documents/kindle-dashboard-launch.sh and make it executable.

CONFIG="${DASHBOARD_CONFIG:-/mnt/us/extensions/kindle-dashboard/config.sh}"
[ -f "$CONFIG" ] && . "$CONFIG"

DASHBOARD_DATA_URL="${DASHBOARD_DATA_URL:-}"
DASHBOARD_EVENTS_URL="${DASHBOARD_EVENTS_URL:-}"
DASHBOARD_TOGGLE_URL="${DASHBOARD_TOGGLE_URL:-}"
DASHBOARD_DELETE_URL="${DASHBOARD_DELETE_URL:-}"
DASHBOARD_READ_TOKEN="${DASHBOARD_READ_TOKEN:-}"
DASHBOARD_TOGGLE_TOKEN="${DASHBOARD_TOGGLE_TOKEN:-}"
NATIVE_APP="${NATIVE_APP:-/mnt/us/extensions/kindle-dashboard/bin/kindle-dashboard}"
RUN_APP="${RUN_APP:-/tmp/kindle-dashboard-native}"
CACHE="${CACHE:-/mnt/us/documents/kindle-dashboard-data.json}"
LOG="${LOG:-/mnt/us/documents/kindle-dashboard-native.log}"
SAVE_PGM="${SAVE_PGM:-}"
INTERVAL="${INTERVAL:-3600}"
DASHBOARD_KEEP_AWAKE="${DASHBOARD_KEEP_AWAKE:-1}"
DASHBOARD_SLEEP_WINDOW="${DASHBOARD_SLEEP_WINDOW:-off}"
DASHBOARD_TIMEZONE="${DASHBOARD_TIMEZONE:-}"
[ -n "$DASHBOARD_TIMEZONE" ] && export TZ="$DASHBOARD_TIMEZONE"
INVERT_IMAGES="${INVERT_IMAGES:-0}"
[ -n "$DASHBOARD_FORCE_INVERT_IMAGES" ] && INVERT_IMAGES="$DASHBOARD_FORCE_INVERT_IMAGES"

keep_awake() {
  lipc-set-prop com.lab126.powerd preventScreenSaver 1 >/dev/null 2>&1 || true
}

allow_sleep() {
  lipc-set-prop com.lab126.powerd preventScreenSaver 0 >/dev/null 2>&1 || true
}

enable_wifi() {
  lipc-set-prop com.lab126.cmd wirelessEnable 1 >/dev/null 2>&1 || true
}

wait_for_network() {
  attempts=0
  while [ "$attempts" -lt 24 ]; do
    if ping -c 1 -W 2 1.1.1.1 >/dev/null 2>&1; then
      return 0
    fi

    attempts=$((attempts + 1))
    sleep 5
  done

  return 0
}

if [ "$DASHBOARD_KEEP_AWAKE" = "1" ]; then
  keep_awake
else
  allow_sleep
fi
enable_wifi
wait_for_network
pkill -f "$RUN_APP" >/dev/null 2>&1 || true

if [ -z "$DASHBOARD_DATA_URL" ]; then
  echo "Dashboard config missing: create /mnt/us/extensions/kindle-dashboard/config.sh with DASHBOARD_DATA_URL." >> "$LOG"
  exit 1
fi

if [ -x "$NATIVE_APP" ]; then
  cp "$NATIVE_APP" "$RUN_APP" >> "$LOG" 2>&1
  chmod 755 "$RUN_APP" >> "$LOG" 2>&1
  image_args=""
  [ "$INVERT_IMAGES" = "1" ] && image_args="--invert-images"
  save_args=""
  [ -n "$SAVE_PGM" ] && save_args="--save-pgm $SAVE_PGM"
  exec "$RUN_APP" --url "$DASHBOARD_DATA_URL" --events-url "$DASHBOARD_EVENTS_URL" --toggle-url "$DASHBOARD_TOGGLE_URL" --delete-url "$DASHBOARD_DELETE_URL" --read-token "$DASHBOARD_READ_TOKEN" --toggle-token "$DASHBOARD_TOGGLE_TOKEN" --cache "$CACHE" --interval "$INTERVAL" --sleep-window "$DASHBOARD_SLEEP_WINDOW" $image_args $save_args >> "$LOG" 2>&1
fi

echo "Native dashboard binary not found at $NATIVE_APP." >&2
echo "Install the KUAL package before launching." >&2
exit 1
