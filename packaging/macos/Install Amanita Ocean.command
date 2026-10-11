#!/bin/bash
# Preview installer for the three bundles shipped beside this file.
set -euo pipefail

formats=(VST3 Components CLAP)
extensions=(vst3 component clap)
transaction_dir=''
lock_dir=''
owns_lock=0
ready_to_commit=0
completed=0

fail() {
    printf '\nInstallation failed: %s\n' "$*" >&2
    exit 1
}

# Refuse redirects outside the explicitly selected installation directory.
check_path() {
    local path="$1"
    [[ "$path" == /* && "$path" != / ]] || fail "Expected an absolute directory: $path"
    case "$path/" in
        *'/../'*|*'/./'*|*'//'*) fail "Use a path without . or .. components: $path" ;;
    esac
    while [[ "$path" != / ]]; do
        [[ ! -L "$path" ]] || fail "Symbolic links are not supported: $path"
        path="$(/usr/bin/dirname "$path")"
    done
}

finish() {
    local status=$? i destination staged previous rollback_failed=0
    trap - EXIT INT TERM HUP
    set +e

    if [[ "$completed" == 0 && -n "$transaction_dir" ]]; then
        # All three staged bundles exist before commit begins. A missing staged
        # bundle means its rename succeeded, even if a signal arrived just after it.
        if [[ "$ready_to_commit" == 1 ]]; then
            printf '\nRestoring the previous installation...\n' >&2
            for i in 2 1 0; do
                destination="$install_root/${formats[$i]}/Amanita Ocean.${extensions[$i]}"
                staged="$transaction_dir/new/${formats[$i]}/Amanita Ocean.${extensions[$i]}"
                previous="$transaction_dir/previous/${formats[$i]}.backup"
                if [[ ! -e "$staged" && ( -e "$destination" || -L "$destination" ) ]]; then
                    /bin/mv "$destination" "$staged" || rollback_failed=1
                fi
                if [[ -e "$previous" ]]; then
                    if [[ -e "$destination" || -L "$destination" ]]; then
                        rollback_failed=1
                    else
                        /bin/mv "$previous" "$destination" || rollback_failed=1
                    fi
                fi
            done
        fi
        if [[ "$rollback_failed" == 0 ]]; then
            /bin/rm -rf "$transaction_dir"
        else
            printf 'Automatic restore was incomplete. Keep these recovery files:\n%s\n' "$transaction_dir" >&2
        fi
    fi

    if [[ "$owns_lock" == 1 ]]; then
        /bin/rmdir "$lock_dir" || status=1
    fi
    if [[ "$status" != 0 ]]; then
        printf '\nInstallation did not complete. Review the error above.\n' >&2
    fi
    if [[ -t 0 && -t 1 ]]; then
        printf '\nPress Return to close this window. '
        read -r _ || true
    fi
    exit "$status"
}

trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

[[ "$(/usr/bin/uname -s)" == Darwin ]] || fail 'This installer requires macOS.'
[[ "$EUID" != 0 ]] || fail 'Run this installer as your normal user, without sudo.'
[[ "$(/usr/bin/uname -m)" == arm64 || "$(/usr/sbin/sysctl -n hw.optional.arm64 2>/dev/null || true)" == 1 ]] \
    || fail 'This preview requires an Apple Silicon Mac.'
[[ $# == 0 ]] || fail 'This installer does not take command-line arguments.'

source_dir="$(cd -- "$(/usr/bin/dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# The override is for isolated installer validation; never override HOME.
install_root="${AMANITA_OCEAN_INSTALL_ROOT:-$HOME/Library/Audio/Plug-Ins}"
install_root="${install_root%/}"
check_path "$install_root"

printf '\nAmanita Ocean — macOS preview installer\n'
printf 'Quit your DAW before installing.\n'
printf 'Destination: %s\n' "$install_root"
printf 'Installing VST3, Audio Unit and CLAP for this user.\n'
printf 'Only the quarantine attribute on these three plug-ins will be removed.\n\n'

# Validate every source and destination before replacing any existing bundle.
package_version=''
for i in 0 1 2; do
    bundle="$source_dir/Amanita Ocean.${extensions[$i]}"
    destination="$install_root/${formats[$i]}/Amanita Ocean.${extensions[$i]}"
    [[ -d "$bundle" && ! -L "$bundle" ]] || fail "Missing plug-in: $bundle"
    links="$(/usr/bin/find "$bundle" -type l -print)"
    [[ -z "$links" ]] || fail "A plug-in contains symbolic links: $bundle"
    identifier="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$bundle/Contents/Info.plist")"
    [[ "$identifier" == audio.amanitaocean.reverb ]] || fail "Unexpected plug-in identity: $bundle"
    version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$bundle/Contents/Info.plist")"
    [[ -n "$version" ]] || fail "Missing plug-in version: $bundle"
    [[ -z "$package_version" || "$version" == "$package_version" ]] || fail 'The three plug-ins have different versions.'
    package_version="$version"
    # file ships with macOS; lipo can require Xcode Command Line Tools.
    case "$(/usr/bin/file -b "$bundle/Contents/MacOS/Amanita Ocean")" in
        *Mach-O*arm64*) ;;
        *) fail "The plug-in does not support Apple Silicon: $bundle" ;;
    esac
    /usr/bin/codesign --verify --deep --strict "$bundle" || fail "Invalid code signature: $bundle"
    check_path "$destination"
    [[ ! -e "$destination" || -d "$destination" ]] || fail "A file is blocking the installation: $destination"
    [[ "$destination" != "$bundle" ]] || fail 'Unpack the installer outside your plug-in folders.'
done

/bin/mkdir -p "$install_root"
lock_dir="$install_root/.Amanita-Ocean-install.lock"
unfinished=0
for pending in "$install_root"/.Amanita-Ocean-install.*/pending; do
    if [[ -f "$pending" ]]; then
        unfinished=1
        printf 'Unfinished installation; keep these recovery files:\n%s\n' "${pending%/pending}" >&2
        printf 'Previous plug-ins, if moved, are in its previous/ folder as FORMAT.backup.\n' >&2
    fi
done
[[ "$unfinished" == 0 ]] || fail "An installation is running or was interrupted. If interrupted, recover any previous plug-ins from the reported folders, then remove their pending files and the empty lock folder: $lock_dir"
/bin/mkdir "$lock_dir" 2>/dev/null \
    || fail "Another installation may be running. If it was interrupted before copying any plug-ins, remove the empty lock folder and retry: $lock_dir"
owns_lock=1
transaction_dir="$(/usr/bin/mktemp -d "$install_root/.Amanita-Ocean-install.XXXXXX")"
/bin/mkdir "$transaction_dir/new" "$transaction_dir/previous"
/usr/bin/touch "$transaction_dir/pending"

for i in 0 1 2; do
    bundle="$source_dir/Amanita Ocean.${extensions[$i]}"
    staged="$transaction_dir/new/${formats[$i]}/Amanita Ocean.${extensions[$i]}"
    /bin/mkdir -p "$install_root/${formats[$i]}" "$transaction_dir/new/${formats[$i]}"
    /usr/bin/ditto --rsrc --extattr --qtn "$bundle" "$staged"
    /usr/bin/xattr -dr com.apple.quarantine "$staged"
    /usr/bin/codesign --verify --deep --strict "$staged" || fail "Copy verification failed: $bundle"
done

ready_to_commit=1
has_backups=0
for i in 0 1 2; do
    destination="$install_root/${formats[$i]}/Amanita Ocean.${extensions[$i]}"
    staged="$transaction_dir/new/${formats[$i]}/Amanita Ocean.${extensions[$i]}"
    check_path "$destination"
    if [[ -e "$destination" ]]; then
        /bin/mv "$destination" "$transaction_dir/previous/${formats[$i]}.backup"
        has_backups=1
    fi
    /bin/mv "$staged" "$destination"
done

# Check final paths as well as staged copies before declaring success.
for i in 0 1 2; do
    /usr/bin/codesign --verify --deep --strict \
        "$install_root/${formats[$i]}/Amanita Ocean.${extensions[$i]}"
done
completed=1
/bin/rm "$transaction_dir/pending"
/bin/rm -rf "$transaction_dir/new"
if [[ "$has_backups" == 0 ]]; then
    /bin/rmdir "$transaction_dir/previous" "$transaction_dir"
fi

printf '\nInstalled Amanita Ocean %s:\n' "$package_version"
for i in 0 1 2; do
    printf '  %s/%s/Amanita Ocean.%s\n' "$install_root" "${formats[$i]}" "${extensions[$i]}"
done
if [[ "$has_backups" == 1 ]]; then
    printf '\nPrevious versions saved in:\n%s/previous\n' "$transaction_dir"
    printf 'Before restoring a backup, quit your DAW and move the currently installed bundle elsewhere.\n'
    printf 'Do not merge the old and new bundle folders.\n'
    printf 'To restore one, move its FORMAT.backup folder to the corresponding plug-in folder\n'
    printf 'and rename it to Amanita Ocean.vst3, Amanita Ocean.component or Amanita Ocean.clap.\n'
fi
printf '\nOpen your DAW and rescan plug-ins if necessary.\n'
printf 'If the Audio Unit is still missing, restart your Mac and scan again.\n'
