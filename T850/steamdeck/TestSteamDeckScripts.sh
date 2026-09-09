#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
for script in BuildSteamRuntime.sh PackageSteamDeckRelease.sh T850.sh PrepareSteamRuntimeApt.sh; do
  bash -n "${SCRIPT_DIR}/${script}"
done

podman() { printf '%s\n' "$@"; }
export -f podman
editor_build="$(bash "${SCRIPT_DIR}/BuildSteamRuntime.sh" --with-editor)"
runtime_build="$(bash "${SCRIPT_DIR}/BuildSteamRuntime.sh")"
[[ "${editor_build}" == *'--target DayScene T8ditor --parallel'* ]]
[[ "${editor_build}" == *'-DT850_BUILD_EDITOR=ON'* ]]
[[ "${editor_build}" == *'799b8115b5eb26a98cfda1d8f2b8ada39a0918c3c5f1114cab541a2fb2243981'* ]]
[[ "${editor_build}" == *'sha256sum --check --strict'* ]]
[[ "${editor_build}" == *'python3 -m venv /tmp/t850-python-check'* ]]
[[ "${runtime_build}" == *'--target DayScene --parallel'* ]]
[[ "${runtime_build}" == *'-DT850_BUILD_EDITOR=OFF'* ]]

fixture="$(mktemp -d)"
trap 'rm -rf -- "${fixture}"' EXIT
apt_root="${fixture}/apt"
mkdir -p "${apt_root}/sources.list.d"
printf '%s\n' 'deb http://deb.debian.org/debian bullseye main' \
  'deb http://deb.debian.org/debian-security bullseye-security main' \
  'deb-src [signed-by=/usr/share/keyrings/debian.gpg] https://deb.debian.org/debian-security-debug bullseye-security-debug main' \
  'deb https://repo.steampowered.com/steamrt3 sniper main' \
  '# deb https://example.invalid bullseye-security main' > "${apt_root}/sources.list"
bash "${SCRIPT_DIR}/PrepareSteamRuntimeApt.sh" "${apt_root}"
[[ "$(grep -c '^# T850: Bullseye LTS ended' "${apt_root}/sources.list")" == 2 ]]
[[ "$(grep -c '^deb ' "${apt_root}/sources.list")" == 2 ]]
grep -Fx 'deb http://deb.debian.org/debian bullseye main' "${apt_root}/sources.list"
grep -Fx 'deb https://repo.steampowered.com/steamrt3 sniper main' "${apt_root}/sources.list"
cp "${apt_root}/sources.list" "${fixture}/prepared.list"
bash "${SCRIPT_DIR}/PrepareSteamRuntimeApt.sh" "${apt_root}"
cmp "${apt_root}/sources.list" "${fixture}/prepared.list"
printf '%s\n' 'Types: deb' 'URIs: https://deb.debian.org/debian-security' 'Suites: bullseye-security' > "${apt_root}/sources.list.d/unsupported.sources"
if bash "${SCRIPT_DIR}/PrepareSteamRuntimeApt.sh" "${apt_root}" > "${fixture}/unsupported.log" 2>&1; then
  echo 'Unexpected security source format was accepted' >&2
  exit 1
fi
grep -q 'unsupported deb822' "${fixture}/unsupported.log"
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