#!/usr/bin/env bash
# Downloads the validators at fixed versions and checks their SHA-256 before anything runs them.
# Usage: fetch-validators.sh <destination> <windows-x64 | linux-x86_64>
set -euo pipefail

dest="$1"
platform="$2"
mkdir -p "${dest}"

fetch() {
  local name="$1" url="$2" sha256="$3"
  local zip="${dest}/${name}.zip"
  curl -fsSL -o "${zip}" "${url}"
  echo "${sha256}  ${zip}" | sha256sum --check --strict
  mkdir -p "${dest}/${name}"
  unzip -q "${zip}" -d "${dest}/${name}"
}

pluginval_base=https://github.com/Tracktion/pluginval/releases/download/v2.0.0-beta3
validator_base=https://github.com/free-audio/clap-validator/releases/download/0.4.1

case "${platform}" in
  windows-x64)
    fetch pluginval "${pluginval_base}/pluginval_Windows.zip" \
      da158e918b60430c6a5223cd99cc2e746bda604e583258c400898caf4e95c523
    fetch clap-validator "${validator_base}/clap-validator-0.4.1-127-g152b982-windows.zip" \
      d935c3af0a45c3911ea2e900f4aa5d6709dac82bb485f0c4ce28648ab2cd0c10
    ;;
  linux-x86_64)
    fetch pluginval "${pluginval_base}/pluginval_Linux.zip" \
      2e23d311c8c2664457b11be8f6cf4ed29cfdc08765c71c43934a1ed3360f118f
    fetch clap-validator "${validator_base}/clap-validator-0.4.1-127-g152b982-ubuntu-22.04.zip" \
      49edadcfb407ea0dd946ce418300e853fbd2660fa4b0d00e4f19ff8eef24ad90
    # The Linux archive of clap-validator wraps the binary in a nested tarball.
    for inner in "${dest}/clap-validator"/*.tar.gz; do
      if [ -e "${inner}" ]; then tar -xzf "${inner}" -C "${dest}/clap-validator"; fi
    done
    chmod +x "${dest}/pluginval/pluginval"
    ;;
  *)
    echo "Unknown platform: ${platform}" >&2
    exit 1
    ;;
esac
