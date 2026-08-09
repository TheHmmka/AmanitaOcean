#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${ROOT_DIR}/final"
CHROME_BIN="${CHROME_BIN:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"

if [[ ! -x "${CHROME_BIN}" ]]; then
  printf 'Google Chrome was not found at: %s\n' "${CHROME_BIN}" >&2
  printf 'Set CHROME_BIN to a Chromium-compatible executable.\n' >&2
  exit 1
fi

mkdir -p "${OUTPUT_DIR}"
PROFILE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/amanita-ocean-stories.XXXXXX")"
ACTIVE_PID=""

cleanup() {
  if [[ -n "${ACTIVE_PID}" ]]; then
    kill "${ACTIVE_PID}" 2>/dev/null || true
  fi
  pkill -f -- "--user-data-dir=${PROFILE_DIR}" 2>/dev/null || true
  rm -rf "${PROFILE_DIR}"
}

trap cleanup EXIT

modes=(default bloom drift veil current)

for index in "${!modes[@]}"; do
  mode="${modes[$index]}"
  number="$(printf '%02d' "$((index + 1))")"
  output="${OUTPUT_DIR}/${number}-amanita-ocean-${mode}-story.png"
  url="file://${ROOT_DIR}/story.html?mode=${mode}"
  profile="${PROFILE_DIR}/${mode}"
  mkdir -p "${profile}"
  rm -f "${output}"

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
    --user-data-dir="${profile}" \
    --screenshot="${output}" \
    "${url}" >/dev/null 2>&1 &

  ACTIVE_PID="$!"
  rendered="false"

  # Chrome 151 on macOS can keep its headless process alive after a local-file
  # screenshot has been flushed. Wait for the PNG, then terminate only this
  # isolated render profile instead of letting the campaign export hang.
  for _ in {1..120}; do
    if [[ -s "${output}" ]]; then
      sleep 0.4
      rendered="true"
      break
    fi
    sleep 0.1
  done

  kill "${ACTIVE_PID}" 2>/dev/null || true
  wait "${ACTIVE_PID}" 2>/dev/null || true
  ACTIVE_PID=""
  pkill -f -- "--user-data-dir=${profile}" 2>/dev/null || true

  if [[ "${rendered}" != "true" ]]; then
    printf 'Chrome did not produce: %s\n' "${output}" >&2
    exit 1
  fi

  width="$(sips -g pixelWidth "${output}" 2>/dev/null | awk '/pixelWidth/{print $2}')"
  height="$(sips -g pixelHeight "${output}" 2>/dev/null | awk '/pixelHeight/{print $2}')"

  if [[ "${width}x${height}" != "1080x1920" ]]; then
    printf 'Unexpected size for %s: %sx%s\n' "${output}" "${width}" "${height}" >&2
    exit 1
  fi

  printf 'Rendered %s (%sx%s)\n' "${output}" "${width}" "${height}"
done
