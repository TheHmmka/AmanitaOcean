#!/bin/bash
# Package either a flat release folder or CMake's Release artefact directory.
set -euo pipefail

if [[ $# != 2 ]]; then
    printf 'Usage: %s SOURCE_DIRECTORY OUTPUT.zip\n' "$0" >&2
    exit 1
fi
[[ "$(/usr/bin/uname -s)" == Darwin ]] || { printf 'Packaging requires macOS.\n' >&2; exit 1; }
script_dir="$(cd -- "$(/usr/bin/dirname "${BASH_SOURCE[0]}")" && pwd -P)"
source_dir="$(cd -- "$1" && pwd -P)"
output_name="$(/usr/bin/basename "$2")"
[[ "$output_name" == *.zip && "$output_name" != .zip ]] || { printf 'Output must end with .zip.\n' >&2; exit 1; }
/bin/mkdir -p "$(/usr/bin/dirname "$2")"
output_dir="$(cd -- "$(/usr/bin/dirname "$2")" && pwd -P)"
output_zip="$output_dir/$output_name"
[[ ! -e "$output_zip" && ! -L "$output_zip" ]] || { printf 'Refusing to replace existing archive: %s\n' "$output_zip" >&2; exit 1; }
stage_dir="$(/usr/bin/mktemp -d "$output_dir/.macos-package.XXXXXX")"
trap '/bin/rm -rf "$stage_dir"' EXIT
package_dir="$stage_dir/${output_name%.zip}"
/bin/mkdir "$package_dir"

formats=(VST3 AU CLAP)
extensions=(vst3 component clap)
for i in 0 1 2; do
    name="Amanita Ocean.${extensions[$i]}"
    bundle="$source_dir/$name"
    if [[ ! -d "$bundle" ]]; then
        bundle="$source_dir/${formats[$i]}/$name"
    fi
    [[ -d "$bundle" && ! -L "$bundle" ]] || { printf 'Missing bundle: %s\n' "$bundle" >&2; exit 1; }
    /usr/bin/codesign --verify --deep --strict "$bundle"
    /usr/bin/ditto "$bundle" "$package_dir/$name"
done
for document in README.txt BUILD-REPORT.txt; do
    if [[ -f "$source_dir/$document" ]]; then
        /bin/cp "$source_dir/$document" "$package_dir/$document"
    fi
done
/bin/cp "$script_dir/../packaging/macos/Install Amanita Ocean.command" "$package_dir/Install Amanita Ocean.command"
/bin/cp "$script_dir/../packaging/macos/INSTALL.txt" "$package_dir/INSTALL.txt"
/bin/chmod 755 "$package_dir/Install Amanita Ocean.command"
/usr/bin/ditto -c -k --norsrc --noextattr --keepParent "$package_dir" "$stage_dir/package.zip"
# An exclusive hard-link creates the final archive without an overwrite race.
/bin/ln "$stage_dir/package.zip" "$output_zip"
(cd -- "$output_dir" && /usr/bin/shasum -a 256 "$output_name")
