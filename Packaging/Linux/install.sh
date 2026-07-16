#!/usr/bin/env bash
set -euo pipefail
package_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
artifact="$package_dir/Appeal_SQLite_Storage.so"
destination="${APPEAL_RESOURCES:-$HOME/Documents/Appeal/Resources}/Modules/Storage/Appeal_SQLite_Storage"
if [[ ! -f "$artifact" ]]; then echo "Missing release artifact: $artifact" >&2; exit 1; fi
if command -v apt-get >/dev/null 2>&1 && [[ -n "" ]]; then sudo apt-get update; sudo apt-get install -y ; fi
mkdir -p "$destination"
cp "$artifact" "$destination/"
echo "Installed Appeal_SQLite_Storage in $destination"