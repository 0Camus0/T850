#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
CONFIG="Release"
OUTPUT=""
SKIP_BUILD=0

usage() {
  cat <<'USAGE'
Usage: PackageSteamDeckRelease.sh [options]

Options:
  --configuration NAME   CMake build type. Default: Release.
  --output PATH          Output .tar.gz path. Default: T850/steamdeck/package/T850-SteamDeck-<config>.tar.gz
  --skip-build           Package an already-built Steam Deck runtime.
  -h, --help             Show this help.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --configuration|--config)
      CONFIG="$2"
      shift 2
      ;;
    --output)
      OUTPUT="$2"
      shift 2
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "[T850] Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "${OUTPUT}" ]]; then
  OUTPUT="${REPO_ROOT}/T850/steamdeck/package/T850-SteamDeck-${CONFIG}.tar.gz"
fi

if [[ "${SKIP_BUILD}" != "1" ]]; then
  "${SCRIPT_DIR}/BuildSteamRuntime.sh" --configuration "${CONFIG}"
fi

RUNTIME_DIR="${REPO_ROOT}/T850/bin/SteamDeck/${CONFIG}"
if [[ ! -x "${RUNTIME_DIR}/DayScene" ]]; then
  echo "[T850] Missing Steam Deck runtime: ${RUNTIME_DIR}/DayScene" >&2
  exit 1
fi

STAGE_ROOT="${REPO_ROOT}/T850/steamdeck/package/stage/T850-SteamDeck-${CONFIG}"
rm -rf "${STAGE_ROOT}"
mkdir -p "${STAGE_ROOT}/bin/SteamDeck/${CONFIG}" \
         "${STAGE_ROOT}/steamdeck" \
         "${STAGE_ROOT}/Resources" \
         "$(dirname -- "${OUTPUT}")"

cp -a "${RUNTIME_DIR}/." "${STAGE_ROOT}/bin/SteamDeck/${CONFIG}/"
for asset in Shaders Models Fonts Textures Scenes Layouts; do
  rm -rf "${STAGE_ROOT}/bin/SteamDeck/${CONFIG}/${asset}"
done
cp -a "${SCRIPT_DIR}/T850.sh" \
      "${SCRIPT_DIR}/T850DeckLauncher.sh" \
      "${SCRIPT_DIR}/T850DeckLauncher.py" \
      "${SCRIPT_DIR}/DownloadCloudAssets.py" \
      "${SCRIPT_DIR}/InstallSteamDeckLauncher.sh" \
      "${SCRIPT_DIR}/config_steamdeck.json" \
      "${STAGE_ROOT}/steamdeck/"
cp -a "${REPO_ROOT}/T850/Resources/logo.png" "${STAGE_ROOT}/Resources/" 2>/dev/null || true

ASSETS_ROOT="${REPO_ROOT}/T850/Assets"
for asset in Shaders Models Fonts Textures Scenes Layouts; do
  if [[ -d "${ASSETS_ROOT}/${asset}" ]]; then
    mkdir -p "${STAGE_ROOT}/Assets"
    cp -a "${ASSETS_ROOT}/${asset}" "${STAGE_ROOT}/Assets/"
  fi
done
if [[ -f "${ASSETS_ROOT}/model-cloud-manifest.json" ]]; then
  mkdir -p "${STAGE_ROOT}/Assets"
  cp -a "${ASSETS_ROOT}/model-cloud-manifest.json" "${STAGE_ROOT}/Assets/"
fi

chmod +x "${STAGE_ROOT}/steamdeck/"*.sh || true

tar -C "${STAGE_ROOT}/.." -czf "${OUTPUT}" "$(basename -- "${STAGE_ROOT}")"
echo "[T850] Steam Deck package created: ${OUTPUT}"
