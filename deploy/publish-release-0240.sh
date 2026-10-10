#!/usr/bin/env bash
# Publish the Amanita Ocean 0.24.0 site on the existing Caddy server.
# Runs ON THE SERVER, after the site archive and its manifest were uploaded to
# the transfer folder. It checks everything before it touches what is served,
# then switches the `current` symlink; the previous release stays for rollback.
#   bash publish-release-0240.sh <site archive SHA-256> <manifest SHA-256>
set -Eeuo pipefail
readonly archive_hash=${1:?site archive SHA-256 required}
readonly manifest_hash=${2:?public manifest SHA-256 required}
[[ "${archive_hash}" =~ ^[a-f0-9]{64}$ && "${manifest_hash}" =~ ^[a-f0-9]{64}$ ]]
readonly site=/var/www/ocean.amanita.music
readonly release=20261010-001-v0240
readonly prior=releases/20260813-182501-headings
readonly transfer=/var/tmp/ocean-v0240-transfer
readonly target="${site}/releases/${release}"
readonly next="${site}/current-v0240-next"

test "$(readlink "${site}/current")" = "${prior}"
test ! -e "${target}" && test ! -L "${target}"
test ! -e "${next}" && test ! -L "${next}"
printf '%s  %s\n' \
  "${archive_hash}" "${transfer}/public-site.tar.gz" \
  "${manifest_hash}" "${transfer}/public-site.sha256" | sha256sum --check --strict

mkdir "${target}"
tar --no-same-owner -xzf "${transfer}/public-site.tar.gz" -C "${target}"
test -z "$(find "${target}" ! -type f ! -type d -print)"
find "${target}" -type d -exec chmod 755 {} +
find "${target}" -type f -exec chmod 644 {} +
cd "${target}"
sha256sum --check --strict --quiet "${transfer}/public-site.sha256"
test "$(find . -type f | wc -l)" -eq "$(wc -l <"${transfer}/public-site.sha256")"

# Every version that is already public stays byte for byte what it is.
for directory in "${site}/current"/downloads/*; do
  test -d "${directory}"
  diff -qr "${directory}" "${target}/downloads/$(basename "${directory}")"
done
cmp "${site}/current/robots.txt" "${target}/robots.txt"

# The new version: three archives that match their manifest, linked from the page.
(cd downloads/0.24.0 && sha256sum --check --strict SHA256SUMS.txt)
test "$(wc -l <downloads/0.24.0/SHA256SUMS.txt)" -eq 3
for archive in \
  Amanita-Ocean-0.24.0-macOS-arm64.zip \
  Amanita-Ocean-0.24.0-Windows-x64.zip \
  Amanita-Ocean-0.24.0-Linux-x64.tar.gz; do
  test -s "downloads/0.24.0/${archive}"
  grep -Fq "href=\"downloads/0.24.0/${archive}\"" index.html
done
grep -Fq 'href="downloads/0.24.0/SHA256SUMS.txt"' index.html
grep -Fq '"softwareVersion": "0.24.0"' index.html

# The one-line installer must pin exactly the macOS archive published beside it.
bash -n install-macos.sh
grep -Fq 'curl -fsSL https://ocean.amanita.music/install-macos.sh | bash' index.html
grep -Fq "local sha256='$(grep -F 'macOS-arm64.zip' downloads/0.24.0/SHA256SUMS.txt | cut -d' ' -f1)'" install-macos.sh
test -s assets/amanita-ocean-plugin.png

ln -s "releases/${release}" "${next}"
test "$(readlink "${site}/current")" = "${prior}"
mv -Tf "${next}" "${site}/current"
readlink "${site}/current"
