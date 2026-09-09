#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
for script in BuildSteamRuntime.sh PackageSteamDeckRelease.sh T850.sh; do
  bash -n "${SCRIPT_DIR}/${script}"
done

podman() { printf '%s\n' "$@"; }
export -f podman
editor_build="$(bash "${SCRIPT_DIR}/BuildSteamRuntime.sh" --with-editor)"
runtime_build="$(bash "${SCRIPT_DIR}/BuildSteamRuntime.sh")"
[[ "${editor_build}" == *'--target DayScene T8ditor --parallel'* ]]
[[ "${editor_build}" == *'-DT850_BUILD_EDITOR=ON'* ]]
[[ "${runtime_build}" == *'--target DayScene --parallel'* ]]
[[ "${runtime_build}" == *'-DT850_BUILD_EDITOR=OFF'* ]]

fixture="$(mktemp -d)"
trap 'rm -rf -- "${fixture}"' EXIT
source_root="${fixture}/T850"
runtime="${source_root}/bin/SteamDeck/Release"
mkdir -p "${source_root}/steamdeck" "${source_root}/Assets/Scenes" "${runtime}"
for file in BuildSteamRuntime.sh PackageSteamDeckRelease.sh T850.sh T850DeckLauncher.sh T850DeckLauncher.py DownloadCloudAssets.py InstallSteamDeckLauncher.sh config_steamdeck.json; do
  cp "${SCRIPT_DIR}/${file}" "${source_root}/steamdeck/"
done
printf '#!/usr/bin/env bash\nprintf "%%s\\n" "$(basename "$0")" "$@"\n' > "${runtime}/DayScene"
chmod +x "${runtime}/DayScene"
if bash "${source_root}/steamdeck/PackageSteamDeckRelease.sh" --with-editor --skip-build > "${fixture}/missing.log" 2>&1; then
  echo 'Missing editor was accepted by packaging' >&2
  exit 1
fi
grep -q 'Missing Steam Deck editor' "${fixture}/missing.log"
cp "${runtime}/DayScene" "${runtime}/T8ditor"
bash "${source_root}/steamdeck/PackageSteamDeckRelease.sh" --with-editor --skip-build --output "${fixture}/package.tar.gz"
tar -tzf "${fixture}/package.tar.gz" > "${fixture}/contents.txt"
grep -Fx 'T850-SteamDeck-Release/bin/SteamDeck/Release/DayScene' "${fixture}/contents.txt"
grep -Fx 'T850-SteamDeck-Release/bin/SteamDeck/Release/T8ditor' "${fixture}/contents.txt"
editor_run="$(T850_SKIP_ASSET_DOWNLOAD=1 bash "${source_root}/steamdeck/T850.sh" --editor --scene-file Scenes/Test.t8scene)"
[[ "${editor_run}" == T8ditor$'\n'* ]]
[[ "${editor_run}" == *$'--api\nvulkan'* ]]
[[ "${editor_run}" == *$'--sceneFile\nScenes/Test.t8scene'* ]]
[[ "${editor_run}" != *'--fullscreen'* ]]
runtime_run="$(T850_SKIP_ASSET_DOWNLOAD=1 bash "${source_root}/steamdeck/T850.sh" --game-mode)"
[[ "${runtime_run}" == DayScene$'\n'* ]]
[[ "${runtime_run}" == *'--fullscreen'* ]]
echo 'PASS SteamRT targets, required editor packaging, and separate editor/runtime launch'