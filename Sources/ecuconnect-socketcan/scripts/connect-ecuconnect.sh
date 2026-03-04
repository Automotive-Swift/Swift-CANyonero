#!/usr/bin/env bash
set -euo pipefail

PREFIX="ECUconnect"
IFNAME=""
PASSWORD=""
RESCAN="yes"
DRY_RUN=0

usage() {
  cat <<USAGE
Usage: $(basename "$0") [options]

Find the first available Wi-Fi network whose SSID starts with a prefix
(default: ECUconnect) and connect using NetworkManager (nmcli).

Options:
  --prefix PREFIX      SSID prefix to match (default: ECUconnect)
  --ifname IFACE       Wi-Fi interface to use (default: auto-detect)
  --password PASS      Wi-Fi password (if required)
  --no-rescan          Use cached scan results (default: rescan)
  --dry-run            Print chosen network and command, do not connect
  -h, --help           Show this help

Examples:
  sudo $(basename "$0")
  sudo $(basename "$0") --ifname wlan0
  sudo $(basename "$0") --prefix ECUconnect --password 'secret123'
USAGE
}

die() {
  echo "Error: $*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Required command '$1' not found"
}

maybe_enable_wifi() {
  local wifi_state
  wifi_state="$(nmcli -t -f WIFI radio | tr -d '[:space:]' || true)"
  if [[ "$wifi_state" == "enabled" ]]; then
    return 0
  fi

  echo "Wi-Fi radio is disabled, trying to enable it..." >&2

  if command -v rfkill >/dev/null 2>&1; then
    rfkill unblock wifi >/dev/null 2>&1 || true
    rfkill unblock all >/dev/null 2>&1 || true
  fi

  nmcli radio wifi on >/dev/null 2>&1 || true
}

current_wifi_ssid() {
  local ssid
  ssid="$(
    nmcli -t --escape yes -f ACTIVE,SSID device wifi list ifname "$IFNAME" --rescan no \
      | awk -F: '$1 == "yes" { print $2; exit }' \
      | sed 's/\\:/:/g'
  )"
  if [[ -n "$ssid" ]]; then
    printf '%s\n' "$ssid"
    return 0
  fi

  # Fallback for older/limited nmcli output: use active connection label.
  ssid="$(
    nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device status \
      | awk -F: -v dev="$IFNAME" '$1 == dev && $2 == "wifi" && $3 == "connected" { print $4; exit }'
  )"
  printf '%s\n' "$ssid"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      [[ $# -ge 2 ]] || die "--prefix requires a value"
      PREFIX="$2"
      shift 2
      ;;
    --ifname)
      [[ $# -ge 2 ]] || die "--ifname requires a value"
      IFNAME="$2"
      shift 2
      ;;
    --password)
      [[ $# -ge 2 ]] || die "--password requires a value"
      PASSWORD="$2"
      shift 2
      ;;
    --no-rescan)
      RESCAN="no"
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "Unknown option: $1"
      ;;
  esac
done

[[ -n "$PREFIX" ]] || die "Prefix must not be empty"

require_cmd nmcli
require_cmd awk
require_cmd sort

if ! nmcli -t -f RUNNING general status | grep -qx "running"; then
  die "NetworkManager is not running"
fi

maybe_enable_wifi

if [[ -z "$IFNAME" ]]; then
  IFNAME="$(
    nmcli -t -f DEVICE,TYPE,STATE device status \
      | awk -F: '$2 == "wifi" { print $1; exit }'
  )"
fi
if [[ -z "$IFNAME" ]]; then
  die "No Wi-Fi interface found (nmcli device status has no type=wifi)"
fi

IF_STATE="$(
  nmcli -t -f DEVICE,TYPE,STATE device status \
    | awk -F: -v dev="$IFNAME" '$1 == dev && $2 == "wifi" { print $3; exit }'
)"
if [[ -z "$IF_STATE" ]]; then
  die "Interface '$IFNAME' is not a Wi-Fi interface according to nmcli"
fi
if [[ "$IF_STATE" == "unavailable" ]]; then
  WIFI_STATE="$(nmcli -t -f WIFI radio | tr -d '[:space:]' || true)"
  RFKILL_STATE="$(rfkill list 2>/dev/null | awk '/Wireless LAN/{f=1} f && /Soft blocked:/{print $3; exit}' || true)"
  die "Wi-Fi interface '$IFNAME' is unavailable (radio=${WIFI_STATE:-unknown}, rfkill_soft_blocked=${RFKILL_STATE:-unknown}). Try: sudo rfkill unblock wifi && sudo nmcli radio wifi on"
fi

CURRENT_SSID="$(current_wifi_ssid || true)"
if [[ -n "$CURRENT_SSID" && "$CURRENT_SSID" == "$PREFIX"* ]]; then
  echo "Interface: $IFNAME"
  echo "Already connected to matching SSID: $CURRENT_SSID"
  exit 0
fi

# Scan available networks.
# We avoid newer nmcli-only flags (like --separator) for compatibility.
SCAN_OUTPUT="$(
  nmcli --terse --escape yes \
    -f SSID,SIGNAL,SECURITY device wifi list ifname "$IFNAME" --rescan "$RESCAN"
)"

[[ -n "$SCAN_OUTPUT" ]] || die "No Wi-Fi networks found on interface '$IFNAME'"

TARGET_LINE="$(
  printf '%s\n' "$SCAN_OUTPUT" \
    | sed -nE 's/^(.*):([0-9]+):(.*)$/\1|\2|\3/p' \
    | sed 's/\\:/:/g' \
    | awk -F'|' -v p="$PREFIX" '$1 != "" && index($1, p) == 1 { print }' \
    | sort -t'|' -k2,2nr \
    | head -n1
)"

[[ -n "$TARGET_LINE" ]] || die "No SSID with prefix '$PREFIX' found"

TARGET_SSID="${TARGET_LINE%%|*}"
REST="${TARGET_LINE#*|}"
TARGET_SIGNAL="${REST%%|*}"
TARGET_SECURITY="${TARGET_LINE##*|}"

echo "Interface: $IFNAME"
echo "Selected SSID: $TARGET_SSID (signal=${TARGET_SIGNAL:-unknown}, security=${TARGET_SECURITY:-unknown})"

if [[ $DRY_RUN -eq 1 ]]; then
  if [[ -n "$PASSWORD" ]]; then
    echo "Dry run: nmcli device wifi connect '<SSID>' ifname '$IFNAME' password '***'"
  else
    echo "Dry run: nmcli device wifi connect '<SSID>' ifname '$IFNAME'"
  fi
  exit 0
fi

if [[ -n "$PASSWORD" ]]; then
  nmcli device wifi connect "$TARGET_SSID" ifname "$IFNAME" password "$PASSWORD"
else
  nmcli device wifi connect "$TARGET_SSID" ifname "$IFNAME"
fi

echo "Connected to $TARGET_SSID"
