#!/bin/bash
# Amanita Ocean — macOS installer for Apple Silicon.
#   curl -fsSL https://ocean.amanita.music/install-macos.sh | bash
# Downloads the published package, checks its SHA-256 and runs the installer
# inside it. Installs for the current user only; no sudo, no system settings.
set -euo pipefail

work=''

# Everything runs from main, so a partly downloaded script does nothing.
main() {
    local name='Amanita-Ocean-0.24.0-macOS-arm64'
    local sha256='d3a787a2f5e6afd3900f0c5bc68fba91c5f13d3e1d4e95fd54fbb29dbeed14bd'
    local url="https://ocean.amanita.music/downloads/0.24.0/$name.zip"
    local protocols='=https' actual

    [[ "$(/usr/bin/uname -s)" == Darwin ]] || { printf 'This installer requires macOS.\n' >&2; exit 1; }
    [[ "$EUID" != 0 ]] || { printf 'Run this as your normal user, without sudo.\n' >&2; exit 1; }
    # For validation only: another source must still serve the exact published bytes.
    if [[ -n "${AMANITA_OCEAN_PACKAGE_URL:-}" ]]; then
        url="$AMANITA_OCEAN_PACKAGE_URL"
        protocols='=https,http,file'
    fi

    work="$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/amanita-ocean-install.XXXXXX")"
    trap '[[ -z "$work" ]] || /bin/rm -rf "$work"' EXIT
    printf 'Downloading %s.zip...\n' "$name"
    /usr/bin/curl -fsSL --proto "$protocols" --proto-redir "$protocols" -o "$work/package.zip" "$url"
    actual="$(/usr/bin/shasum -a 256 "$work/package.zip")"
    [[ "${actual%% *}" == "$sha256" ]] || { printf 'The download does not match the published SHA-256. Nothing was installed.\n' >&2; exit 1; }
    /usr/bin/ditto -xk "$work/package.zip" "$work/package"
    # The script itself arrives on standard input; keep the installer away from it.
    /bin/bash "$work/package/$name/Install Amanita Ocean.command" </dev/null
}

main "$@"
