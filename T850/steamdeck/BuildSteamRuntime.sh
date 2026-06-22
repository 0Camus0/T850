#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
IMAGE="registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest"
BUILD_DIR="build/steamdeck-steamrt-libcpp"
CONFIG="Release"
BUILD_EDITOR="OFF"
CLEAN=0
CONFIGURE_ONLY=0

usage() {
  cat <<'USAGE'
Usage: BuildSteamRuntime.sh [options]

Options:
  --configuration NAME   CMake build type. Default: Release.
  --with-editor          Include T8ditor in the Steam Runtime build.
  --clean                Remove the SteamRT build/vcpkg x64-linux state first.
  --configure-only       Configure dependencies and CMake, but do not build.
  -h, --help             Show this help.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --configuration|--config)
      CONFIG="$2"
      shift 2
      ;;
    --with-editor)
      BUILD_EDITOR="ON"
      shift
      ;;
    --clean)
      CLEAN=1
      shift
      ;;
    --configure-only)
      CONFIGURE_ONLY=1
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

command -v podman >/dev/null 2>&1 || {
  echo "[T850] podman is required for the official Steam Runtime SDK build." >&2
  exit 1
}

if [[ "${CLEAN}" == "1" ]]; then
  rm -rf \
    "${REPO_ROOT}/T850/Librerias/vcpkg/installed" \
    "${REPO_ROOT}/T850/Librerias/vcpkg/packages" \
    "${REPO_ROOT}/T850/Librerias/vcpkg/buildtrees" \
    "${REPO_ROOT}/${BUILD_DIR}"
fi

podman run --rm \
  -v "${REPO_ROOT}:/workspace" \
  -w /workspace \
  "${IMAGE}" \
  bash -lc "
set -e
apt-get update >/dev/null
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  clang-16 libc++-16-dev libc++abi-16-dev \
  autoconf-archive libltdl-dev python3-venv \
  libx11-dev libxft-dev libxext-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev \
  libxi-dev libxrandr-dev libxcursor-dev libxfixes-dev \
  pkg-config zip unzip curl ca-certificates make m4 perl >/dev/null

if [ ! -x /tmp/autoconf-install/bin/autoconf ]; then
  rm -rf /tmp/autoconf-2.72 /tmp/autoconf-install
  cd /tmp
  curl -L -o autoconf-2.72.tar.gz https://ftp.gnu.org/gnu/autoconf/autoconf-2.72.tar.gz
  tar xf autoconf-2.72.tar.gz
  cd autoconf-2.72
  ./configure --prefix=/tmp/autoconf-install >/dev/null
  make -j2 >/dev/null
  make install >/dev/null
fi

export PATH=/tmp/autoconf-install/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export CC=clang-16
export CXX=clang++-16
export CFLAGS='-D__NR_mount_setattr=442'
export CPPFLAGS='-D__NR_mount_setattr=442'
export CXXFLAGS='-D__NR_mount_setattr=442 -stdlib=libc++'
export LDFLAGS='-stdlib=libc++ -lc++abi'

cd /workspace
cmake -S T850 -B ${BUILD_DIR} -G Ninja \
  -DCMAKE_BUILD_TYPE=${CONFIG} \
  -DT850_PLATFORM_STEAM_DECK=ON \
  -DT850_BUILD_EDITOR=${BUILD_EDITOR}

if [ '${CONFIGURE_ONLY}' != '1' ]; then
  cmake --build ${BUILD_DIR} --target DayScene --parallel \$(nproc)
  runtime_dir=/workspace/T850/bin/SteamDeck/${CONFIG}
  cp -L /usr/lib/x86_64-linux-gnu/libc++.so.1 \"\${runtime_dir}/\"
  cp -L /usr/lib/x86_64-linux-gnu/libc++abi.so.1 \"\${runtime_dir}/\"
  cp -L /usr/lib/x86_64-linux-gnu/libunwind.so.1 \"\${runtime_dir}/\"
fi
"
