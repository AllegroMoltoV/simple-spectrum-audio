#!/usr/bin/env bash
# Fails when the release layout misses a plug-in or a license text. A published immutable release cannot be fixed.
set -euo pipefail

root="$1"
# The binary inside the VST 3 bundle: x86_64-win/Simple Spectrum.vst3 or x86_64-linux/Simple Spectrum.so.
vst3_binary="$2"
required=(
  "VST3/Simple Spectrum.vst3/Contents/${vst3_binary}"
  "CLAP/Simple Spectrum.clap"
  "LICENSE.txt"
  "README.txt"
  "THIRD_PARTY_LICENSES/vst3sdk.txt"
  "THIRD_PARTY_LICENSES/clap.txt"
  "THIRD_PARTY_LICENSES/clap-wrapper.txt"
  "THIRD_PARTY_LICENSES/fmt.txt"
  "THIRD_PARTY_LICENSES/visage.txt"
  "THIRD_PARTY_LICENSES/droid-sans-mono-apache-2.0.txt"
  "THIRD_PARTY_LICENSES/lato-OFL.txt"
  "THIRD_PARTY_LICENSES/stb.txt"
  "THIRD_PARTY_LICENSES/lucide-cursors.txt"
  "THIRD_PARTY_LICENSES/freetype-FTL.txt"
  "THIRD_PARTY_LICENSES/pffft.txt"
)
# Visage embeds the Twemoji font and uses the Vulkan headers only on Linux.
case "${vst3_binary}" in
  x86_64-linux/*)
    required+=("THIRD_PARTY_LICENSES/twemoji-mozilla.md" "THIRD_PARTY_LICENSES/vulkan-headers.md")
    ;;
esac
missing=0
for path in "${required[@]}"; do
  if [ ! -s "${root}/${path}" ]; then
    echo "missing: ${path}"
    missing=1
  fi
done
# Only the files above belong in the archive: no installers or scripts.
extra=$(cd "${root}" && find . -type f | sed 's#^\./##' | grep -v -x -F "$(printf '%s\n' "${required[@]}")" || true)
if [ -n "${extra}" ]; then
  echo "unexpected files:"
  echo "${extra}"
  missing=1
fi
exit "${missing}"
