#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${ROOT_DIR}/final"
OUTPUT="${OUTPUT_DIR}/06-amanita-ocean-new-characters-story.png"
CHROME_BIN="${CHROME_BIN:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"

if [[ ! -x "${CHROME_BIN}" ]]; then
  printf 'Google Chrome was not found at: %s\n' "${CHROME_BIN}" >&2
  printf 'Set CHROME_BIN to a Chromium-compatible executable.\n' >&2
  exit 1
fi

mkdir -p "${OUTPUT_DIR}"
PROFILE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/amanita-ocean-new-story.XXXXXX")"
ACTIVE_PID=""

cleanup() {
  if [[ -n "${ACTIVE_PID}" ]]; then
    kill "${ACTIVE_PID}" 2>/dev/null || true
  fi
  pkill -f -- "--user-data-dir=${PROFILE_DIR}" 2>/dev/null || true
  rm -rf "${PROFILE_DIR}"
}

trap cleanup EXIT

rm -f "${OUTPUT}"
PROFILE="${PROFILE_DIR}/render"
mkdir -p "${PROFILE}"

"${CHROME_BIN}" \
  --headless=new \
  --disable-gpu \
  --disable-background-networking \
  --disable-default-apps \
  --disable-extensions \
  --disable-sync \
  --hide-scrollbars \
  --allow-file-access-from-files \
  --force-device-scale-factor=1 \
  --window-size=1080,1920 \
  --run-all-compositor-stages-before-draw \
  --virtual-time-budget=1800 \
  --user-data-dir="${PROFILE}" \
  --screenshot="${OUTPUT}" \
  "file://${ROOT_DIR}/new-characters.html" >/dev/null 2>&1 &

ACTIVE_PID="$!"
rendered="false"

# Chrome can keep headless alive after the local screenshot is flushed. Wait
# up to 20 seconds for the file, then terminate only this isolated profile.
for _ in {1..200}; do
  if [[ -s "${OUTPUT}" ]]; then
    sleep 0.4
    rendered="true"
    break
  fi
  sleep 0.1
done

kill "${ACTIVE_PID}" 2>/dev/null || true
wait "${ACTIVE_PID}" 2>/dev/null || true
ACTIVE_PID=""

if [[ "${rendered}" != "true" ]]; then
  printf 'Chrome did not produce: %s\n' "${OUTPUT}" >&2
  exit 1
fi

width="$(sips -g pixelWidth "${OUTPUT}" 2>/dev/null | awk '/pixelWidth/{print $2}')"
height="$(sips -g pixelHeight "${OUTPUT}" 2>/dev/null | awk '/pixelHeight/{print $2}')"

if [[ "${width}x${height}" != "1080x1920" ]]; then
  printf 'Unexpected size for %s: %sx%s\n' "${OUTPUT}" "${width}" "${height}" >&2
  exit 1
fi

printf 'Rendered %s (%sx%s)\n' "${OUTPUT}" "${width}" "${height}"
