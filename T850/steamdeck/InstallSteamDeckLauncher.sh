#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
T850_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
DESKTOP_DIR="${HOME}/.local/share/applications"
USER_DESKTOP="${HOME}/Desktop"
DESKTOP_FILE="${DESKTOP_DIR}/t850-steamdeck.desktop"
MINECRAFT_DESKTOP_FILE="${DESKTOP_DIR}/t850-minecraft-steamdeck.desktop"
LAUNCHER_DESKTOP_FILE="${DESKTOP_DIR}/t850-steamdeck-launcher.desktop"
DESKTOP_ICON_FILE="${USER_DESKTOP}/T850.desktop"
DESKTOP_MINECRAFT_ICON_FILE="${USER_DESKTOP}/T850 Minecraft.desktop"
DESKTOP_LAUNCHER_ICON_FILE="${USER_DESKTOP}/T850 Launcher.desktop"
ICON_PATH="${T850_ROOT}/Resources/logo.png"
LAUNCHER="${SCRIPT_DIR}/T850.sh"
GUI_LAUNCHER="${SCRIPT_DIR}/T850DeckLauncher.sh"

mkdir -p "${DESKTOP_DIR}" "${USER_DESKTOP}"
chmod +x "${LAUNCHER}"
chmod +x "${GUI_LAUNCHER}"

cat > "${DESKTOP_FILE}" <<EOF
[Desktop Entry]
Type=Application
Version=1.5
Name=T850
Comment=T850 Steam Deck Vulkan runtime
Exec=${LAUNCHER} --game-mode
Path=${T850_ROOT}
Icon=${ICON_PATH}
Terminal=false
Categories=Game;
StartupNotify=false
EOF

cat > "${LAUNCHER_DESKTOP_FILE}" <<EOF
[Desktop Entry]
Type=Application
Version=1.5
Name=T850 Launcher
Comment=T850 Steam Deck launcher
Exec=${GUI_LAUNCHER}
Path=${T850_ROOT}
Icon=${ICON_PATH}
Terminal=false
Categories=Game;
StartupNotify=false
EOF

cat > "${MINECRAFT_DESKTOP_FILE}" <<EOF
[Desktop Entry]
Type=Application
Version=1.5
Name=T850 Minecraft
Comment=T850 Minecraft voxel scene
Exec=${LAUNCHER} --game-mode --scene 6
Path=${T850_ROOT}
Icon=${ICON_PATH}
Terminal=false
Categories=Game;
StartupNotify=false
EOF

cp "${DESKTOP_FILE}" "${DESKTOP_ICON_FILE}"
cp "${MINECRAFT_DESKTOP_FILE}" "${DESKTOP_MINECRAFT_ICON_FILE}"
cp "${LAUNCHER_DESKTOP_FILE}" "${DESKTOP_LAUNCHER_ICON_FILE}"
chmod +x "${DESKTOP_FILE}" "${MINECRAFT_DESKTOP_FILE}" "${LAUNCHER_DESKTOP_FILE}" \
    "${DESKTOP_ICON_FILE}" "${DESKTOP_MINECRAFT_ICON_FILE}" "${DESKTOP_LAUNCHER_ICON_FILE}"

if command -v gio >/dev/null 2>&1; then
  gio set "${DESKTOP_ICON_FILE}" metadata::trusted true >/dev/null 2>&1 || true
    gio set "${DESKTOP_MINECRAFT_ICON_FILE}" metadata::trusted true >/dev/null 2>&1 || true
  gio set "${DESKTOP_LAUNCHER_ICON_FILE}" metadata::trusted true >/dev/null 2>&1 || true
fi

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "${DESKTOP_DIR}" >/dev/null 2>&1 || true
fi

python3 - "$DESKTOP_FILE" "$LAUNCHER_DESKTOP_FILE" "$ICON_PATH" "$LAUNCHER" "$T850_ROOT" <<'PY'
import binascii
import os
import struct
import sys
from collections import OrderedDict
from pathlib import Path

runtime_desktop = Path(sys.argv[1])
launcher_desktop = Path(sys.argv[2])
icon = Path(sys.argv[3])
runtime_launcher = Path(sys.argv[4])
t850_root = Path(sys.argv[5])
steam_userdata = Path.home() / ".local/share/Steam/userdata"
config_dirs = sorted(steam_userdata.glob("*/config"))
if config_dirs:
    shortcuts = config_dirs[0] / "shortcuts.vdf"
else:
    shortcuts = steam_userdata / "0/config/shortcuts.vdf"
shortcuts.parent.mkdir(parents=True, exist_ok=True)

TYPE_OBJECT = 0
TYPE_STRING = 1
TYPE_INT = 2
TYPE_UINT64 = 7
TYPE_END = 8

def read_cstr(data, idx):
    end = data.index(0, idx)
    return data[idx:end].decode("utf-8", errors="replace"), end + 1

def parse_object(data, idx):
    items = []
    while idx < len(data):
        t = data[idx]
        idx += 1
        if t == TYPE_END:
            return items, idx
        key, idx = read_cstr(data, idx)
        if t == TYPE_OBJECT:
            value, idx = parse_object(data, idx)
        elif t == TYPE_STRING:
            value, idx = read_cstr(data, idx)
        elif t == TYPE_INT:
            value = struct.unpack_from("<i", data, idx)[0]
            idx += 4
        elif t == TYPE_UINT64:
            value = struct.unpack_from("<Q", data, idx)[0]
            idx += 8
        else:
            raise ValueError(f"unsupported VDF type {t} for key {key}")
        items.append([t, key, value])
    return items, idx

def write_object(items):
    out = bytearray()
    for t, key, value in items:
        out.append(t)
        out.extend(key.encode("utf-8") + b"\0")
        if t == TYPE_OBJECT:
            out.extend(write_object(value))
        elif t == TYPE_STRING:
            out.extend(str(value).encode("utf-8") + b"\0")
        elif t == TYPE_INT:
            out.extend(struct.pack("<i", int(value)))
        elif t == TYPE_UINT64:
            out.extend(struct.pack("<Q", int(value)))
        else:
            raise ValueError(f"unsupported VDF type {t}")
    out.append(TYPE_END)
    return bytes(out)

def shortcut_appid(name, exe):
    crc = binascii.crc32((exe + name).encode("utf-8")) & 0xFFFFFFFF
    return struct.unpack("<i", struct.pack("<I", crc | 0x80000000))[0]

def make_shortcut(name, exe, start_dir, launch_options=""):
    appid = shortcut_appid(name, exe)
    return [
        [TYPE_INT, "appid", appid],
        [TYPE_STRING, "appname", name],
        [TYPE_STRING, "exe", f'"{exe}"'],
        [TYPE_STRING, "StartDir", f'"{start_dir}"'],
        [TYPE_STRING, "icon", str(icon)],
        [TYPE_STRING, "ShortcutPath", str(exe)],
        [TYPE_STRING, "LaunchOptions", launch_options],
        [TYPE_INT, "IsHidden", 0],
        [TYPE_INT, "AllowDesktopConfig", 1],
        [TYPE_INT, "AllowOverlay", 1],
        [TYPE_INT, "OpenVR", 0],
        [TYPE_INT, "Devkit", 0],
        [TYPE_STRING, "DevkitGameID", ""],
        [TYPE_INT, "DevkitOverrideAppID", 0],
        [TYPE_INT, "LastPlayTime", 0],
        [TYPE_OBJECT, "tags", [[TYPE_STRING, "0", "T850"]]],
    ]

def get_root(data):
    if not data:
        return [[TYPE_OBJECT, "shortcuts", []]]
    root, idx = parse_object(data, 0)
    return root

def find_object(items, key):
    for item in items:
        if item[0] == TYPE_OBJECT and item[1] == key:
            return item
    return None

def install_shortcuts():
    if shortcuts.exists():
        data = shortcuts.read_bytes()
        backup = shortcuts.with_suffix(".vdf.t850bak")
        backup.write_bytes(data)
    else:
        data = b""
    root = get_root(data)
    shortcuts_obj = find_object(root, "shortcuts")
    if shortcuts_obj is None:
        shortcuts_obj = [TYPE_OBJECT, "shortcuts", []]
        root.append(shortcuts_obj)

    entries = shortcuts_obj[2]
    wanted = {
        "T850": make_shortcut("T850", str(runtime_desktop), str(runtime_desktop.parent)),
        "T850 Minecraft": make_shortcut(
            "T850 Minecraft",
            str(runtime_launcher),
            str(t850_root),
            "--game-mode --scene 6",
        ),
        "T850 Launcher": make_shortcut("T850 Launcher", str(launcher_desktop), str(launcher_desktop.parent)),
    }
    existing_by_name = {}
    for entry in entries:
        if entry[0] != TYPE_OBJECT:
            continue
        name = None
        for child in entry[2]:
            if child[1] == "appname":
                name = child[2]
                break
        if name:
            existing_by_name[name] = entry

    for name, value in wanted.items():
        if name in existing_by_name:
            existing_by_name[name][2] = value
        else:
            numeric_keys = [int(e[1]) for e in entries if e[0] == TYPE_OBJECT and e[1].isdigit()]
            next_key = str((max(numeric_keys) + 1) if numeric_keys else 0)
            entries.append([TYPE_OBJECT, next_key, value])

    shortcuts.write_bytes(write_object(root))

try:
    install_shortcuts()
    print(f"[T850] Steam shortcuts updated: {shortcuts}")
except Exception as exc:
    print(f"[T850] Could not update Steam shortcuts automatically: {exc}", file=sys.stderr)
    sys.exit(0)
PY

cat <<EOF
[T850] Steam Deck launcher installed:
  ${DESKTOP_FILE}
    ${MINECRAFT_DESKTOP_FILE}
  ${LAUNCHER_DESKTOP_FILE}
  ${DESKTOP_ICON_FILE}
    ${DESKTOP_MINECRAFT_ICON_FILE}
  ${DESKTOP_LAUNCHER_ICON_FILE}

Desktop Mode:
    Double-click T850, T850 Minecraft, or T850 Launcher on the Desktop.
    Start Menu > Games > T850 / T850 Minecraft / T850 Launcher

Game Mode:
    T850, T850 Minecraft, and T850 Launcher were added to Steam shortcuts.
  Restart Steam or switch back to Game Mode if they do not appear immediately.
EOF
