#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
T850_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
CONFIG_PATH="${T850_ROOT}/steamdeck/config_steamdeck.json"
EXECUTABLE="${T850_ROOT}/bin/SteamDeck/Release/DayScene"

MODE="game"
EXTRA_ARGS=()

usage() {
  cat <<'USAGE'
Usage: T850.sh [options] [-- extra DayScene args]

Options:
  --game-mode      Fullscreen 1280x800 defaults for Steam Game Mode.
  --desktop        Windowed 1280x800 defaults for Desktop Mode.
  --scene N        Override starting scene index.
  --scene-file P   Launch Sandbox with a .t8scene file.
  --model P        Launch Sandbox with a model file.
  --log-level L    error|info|debug|verbose|trace|0..4.
  --profile        Enable runtime profiler.
  --benchmark      Enable benchmark mode.
  -h, --help       Show this help.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --game-mode)
      MODE="game"
      shift
      ;;
    --desktop)
      MODE="desktop"
      shift
      ;;
    --scene)
      EXTRA_ARGS+=(--scene "$2")
      shift 2
      ;;
    --scene-file|--t8scene)
      EXTRA_ARGS+=(--sceneFile "$2")
      shift 2
      ;;
    --model)
      EXTRA_ARGS+=(--model "$2")
      shift 2
      ;;
    --log-level|--logLevel)
      EXTRA_ARGS+=(--logLevel "$2")
      shift 2
      ;;
    --profile)
      EXTRA_ARGS+=(--profile)
      shift
      ;;
    --benchmark)
      EXTRA_ARGS+=(--benchmark)
      shift
      ;;
    --)
      shift
      EXTRA_ARGS+=("$@")
      break
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      EXTRA_ARGS+=("$1")
      shift
      ;;
  esac
done

if [[ ! -x "${EXECUTABLE}" ]]; then
  echo "[T850] Missing executable: ${EXECUTABLE}" >&2
  echo "[T850] Build it with LaunchSteamDeckSolution.bat --build or cmake --build build/steamdeck --target DayScene." >&2
  exit 1
fi

mkdir -p "${T850_ROOT}/logs"
RUNTIME_DIR="$(dirname -- "${EXECUTABLE}")"
export LD_LIBRARY_PATH="${RUNTIME_DIR}:${LD_LIBRARY_PATH:-}"

ASSET_DOWNLOADER="${SCRIPT_DIR}/DownloadCloudAssets.py"
if [[ "${T850_SKIP_ASSET_DOWNLOAD:-0}" != "1" && -f "${ASSET_DOWNLOADER}" ]]; then
  if ! python3 "${ASSET_DOWNLOADER}" --root "${T850_ROOT}" --jobs "${T850_CLOUD_DOWNLOAD_THREADS:-7}" >> "${T850_ROOT}/logs/steamdeck_asset_download.log" 2>&1; then
    echo "[T850] Warning: cloud asset download failed. See ${T850_ROOT}/logs/steamdeck_asset_download.log" >&2
  fi
fi

for asset in Shaders Models Fonts Textures Scenes Layouts; do
  target="${T850_ROOT}/Assets/${asset}"
  link="${RUNTIME_DIR}/${asset}"
  if [[ -e "${target}" ]]; then
    if [[ -L "${link}" || ! -e "${link}" ]]; then
      rm -f "${link}"
      ln -s "${target}" "${link}"
    fi
  fi
done

cd "${T850_ROOT}"

if [[ "${MODE}" == "desktop" ]]; then
  HAS_WIDTH=0
  HAS_HEIGHT=0
  for arg in "${EXTRA_ARGS[@]}"; do
    [[ "$arg" == "--width" ]] && HAS_WIDTH=1
    [[ "$arg" == "--height" ]] && HAS_HEIGHT=1
  done
  [[ "$HAS_WIDTH" == "0" ]] && EXTRA_ARGS+=(--width 1280)
  [[ "$HAS_HEIGHT" == "0" ]] && EXTRA_ARGS+=(--height 800)
else
  HAS_FULLSCREEN=0
  HAS_WIDTH=0
  HAS_HEIGHT=0
  for arg in "${EXTRA_ARGS[@]}"; do
    [[ "$arg" == "--fullscreen" ]] && HAS_FULLSCREEN=1
    [[ "$arg" == "--width" ]] && HAS_WIDTH=1
    [[ "$arg" == "--height" ]] && HAS_HEIGHT=1
  done
  [[ "$HAS_FULLSCREEN" == "0" ]] && EXTRA_ARGS+=(--fullscreen)
  [[ "$HAS_WIDTH" == "0" ]] && EXTRA_ARGS+=(--width 1280)
  [[ "$HAS_HEIGHT" == "0" ]] && EXTRA_ARGS+=(--height 800)
fi

export SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0
export SDL_GAMECONTROLLER_USE_BUTTON_LABELS=1

exec "${EXECUTABLE}" --config "${CONFIG_PATH}" --api vulkan "${EXTRA_ARGS[@]}"
