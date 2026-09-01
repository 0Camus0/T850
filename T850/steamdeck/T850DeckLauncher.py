#!/usr/bin/env python3
import json
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path

import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Gdk, GLib


SCRIPT_DIR = Path(__file__).resolve().parent
T850_ROOT = SCRIPT_DIR.parent
ASSETS_DIR = T850_ROOT / "Assets"
RUNTIME_DIR = T850_ROOT / "bin" / "SteamDeck" / "Release"
DAYSCENE = RUNTIME_DIR / "DayScene"
EDITOR = RUNTIME_DIR / "T8ditor"
CONFIG_PATH = SCRIPT_DIR / "config_steamdeck.json"

SCENES = [
    ("Sandbox", 0),
    ("Day Scene", 1),
    ("Quake3 Mock", 2),
    ("Ragdoll Editor", 3),
    ("Scene Template", 4),
    ("Minecraft", 6),
]
SCENE_LABEL_BY_ID = {value: label for label, value in SCENES}
SCENE_ID_BY_LABEL = {label: value for label, value in SCENES}
LOG_LEVELS = ["error", "info", "debug", "verbose", "trace"]


def load_config():
    try:
        with CONFIG_PATH.open("r", encoding="utf-8-sig") as f:
            return json.load(f)
    except Exception:
        return {}


def list_asset_files(subdir, suffixes):
    root = ASSETS_DIR / subdir
    if not root.exists():
        return []
    out = []
    for path in root.rglob("*"):
        if path.is_file() and path.suffix.lower() in suffixes:
            try:
                out.append(str(path.relative_to(ASSETS_DIR)).replace("\\", "/"))
            except ValueError:
                out.append(str(path))
    return sorted(out)


def asset_path(value):
    value = value or ""
    path = Path(value)
    if path.is_absolute():
        try:
            return str(path.relative_to(ASSETS_DIR)).replace("\\", "/")
        except ValueError:
            return str(path)
    return value.replace("\\", "/")


def ensure_runtime_links():
    RUNTIME_DIR.mkdir(parents=True, exist_ok=True)
    for asset in ["Shaders", "Models", "Fonts", "Textures", "Scenes", "Layouts"]:
        target = ASSETS_DIR / asset
        link = RUNTIME_DIR / asset
        if not target.exists():
            continue
        if link.is_symlink() or not link.exists():
            if link.exists() or link.is_symlink():
                link.unlink()
            link.symlink_to(target)


def quote_cmd(args):
    def q(v):
        s = str(v)
        return f'"{s}"' if any(c.isspace() for c in s) else s
    return " ".join(q(x) for x in args)


class Launcher(Gtk.Window):
    def __init__(self):
        super().__init__(title="T850 Engine Launcher")
        screen = Gdk.Screen.get_default()
        monitor = screen.get_monitor_at_point(0, 0) if screen else 0
        geometry = screen.get_monitor_workarea(monitor) if screen else Gdk.Rectangle()
        max_width = geometry.width or 1280
        max_height = geometry.height or 800
        # SteamOS Desktop Mode can report the full monitor instead of the work
        # area, so reserve extra vertical room for the bottom taskbar.
        taskbar_reserve = 104
        self.window_width = min(1160, max_width - 32)
        self.window_height = min(660, max_height - taskbar_reserve)
        self.set_default_size(self.window_width, self.window_height)
        self.set_size_request(min(840, self.window_width), min(540, self.window_height))
        self.set_position(Gtk.WindowPosition.CENTER)
        self.connect("destroy", Gtk.main_quit)
        self.connect("key-press-event", self.on_key_press)
        self.config = load_config()
        self.models = list_asset_files("Models", {".glb", ".gltf"})
        self.scene_files = list_asset_files("Scenes", {".t8scene"})
        self.current_scene_id = None
        self._widgets = []
        self._build_style()
        self._build_ui()
        self._load_defaults()
        self.update_visibility()
        self.update_preview()
        GLib.idle_add(self.grab_initial_focus)

    def _build_style(self):
        css = b"""
        window { background: #1B1B2F; color: #E0E0E0; }
        label { color: #E0E0E0; font-size: 10px; }
        frame { background: #162447; border-radius: 7px; padding: 4px; }
        frame > label { color: #6C63FF; font-weight: bold; font-size: 10px; }
        entry, combobox, textview text { background: #1F4068; color: #E0E0E0; font-size: 10px; min-height: 24px; }
        checkbutton, radiobutton { font-size: 10px; }
        button { background: #1F4068; color: #E0E0E0; border-radius: 5px; padding: 4px; font-size: 10px; }
        button.run { background: #A6E3A1; color: #1E1E2E; font-weight: bold; }
        button.accent { background: #6C63FF; color: #E0E0E0; font-weight: bold; }
        .title { color: #6C63FF; font-size: 22px; font-weight: bold; }
        .subtitle { color: #A6ADC8; font-size: 10px; }
        .footer { background: #1B1B2F; }
        """
        provider = Gtk.CssProvider()
        provider.load_from_data(css)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(),
            provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION,
        )

    def _label(self, text):
        label = Gtk.Label(label=text)
        label.set_halign(Gtk.Align.START)
        return label

    def _combo(self, values, active=0, editable=False):
        combo = Gtk.ComboBoxText.new_with_entry() if editable else Gtk.ComboBoxText()
        for value in values:
            combo.append_text(value)
        if values:
            combo.set_active(active)
        combo.connect("changed", lambda _w: self.on_changed())
        self._widgets.append(combo)
        return combo

    def _entry(self, text=""):
        entry = Gtk.Entry()
        entry.set_text(str(text))
        entry.connect("changed", lambda _w: self.on_changed())
        self._widgets.append(entry)
        return entry

    def _check(self, text, active=False):
        check = Gtk.CheckButton(label=text)
        check.set_active(active)
        check.connect("toggled", lambda _w: self.on_changed())
        self._widgets.append(check)
        return check

    def grab_initial_focus(self):
        self.scene.grab_focus()
        return False

    def focus_next(self, direction):
        if not self.get_focus():
            self.grab_initial_focus()
            return True
        gtk_direction = Gtk.DirectionType.TAB_FORWARD if direction > 0 else Gtk.DirectionType.TAB_BACKWARD
        if not self.child_focus(gtk_direction):
            self.grab_initial_focus()
        return True

    def resolve_focus_widget(self):
        focus = self.get_focus()
        widget = focus
        while widget:
            parent = widget
            while parent:
                if isinstance(parent, Gtk.ComboBox):
                    return parent
                parent = parent.get_parent()
            if isinstance(widget, (Gtk.ComboBox, Gtk.CheckButton, Gtk.RadioButton, Gtk.Button)):
                return widget
            if isinstance(widget, Gtk.Entry):
                return widget
            widget = widget.get_parent()
        return focus

    def numeric_entry_step(self, entry):
        if entry in (self.width, self.height):
            return 10
        if entry in (self.dump_frame, self.telemetry_frequency, self.benchmark_seconds, self.profile_frames):
            return 1
        if entry is self.dump_seconds:
            return 1.0
        return None

    def adjust_numeric_entry(self, entry, direction):
        step = self.numeric_entry_step(entry)
        if step is None:
            return False
        text = entry.get_text().strip()
        try:
            if isinstance(step, float):
                value = max(0.0, float(text or 0.0) + (step * direction))
                entry.set_text(f"{value:g}")
            else:
                value = max(0, int(text or 0) + (step * direction))
                entry.set_text(str(value))
            entry.select_region(0, -1)
            return True
        except ValueError:
            return False

    def adjust_combo(self, combo, direction):
        model = combo.get_model()
        count = len(model) if model else 0
        if count <= 0:
            return False
        active = combo.get_active()
        if active < 0:
            text = self.combo_text(combo)
            for idx, row in enumerate(model):
                if row[0] == text:
                    active = idx
                    break
        if active < 0:
            active = 0
        next_index = max(0, min(count - 1, active + direction))
        combo.set_active(next_index)
        combo.grab_focus()
        return True

    def adjust_radio(self, radio, direction):
        group = list(radio.get_group())
        if len(group) <= 1:
            radio.set_active(True)
            return True
        group.reverse()
        try:
            active_index = next(i for i, item in enumerate(group) if item.get_active())
        except StopIteration:
            active_index = group.index(radio) if radio in group else 0
        next_index = max(0, min(len(group) - 1, active_index + direction))
        group[next_index].set_active(True)
        group[next_index].grab_focus()
        return True

    def adjust_focused_setting(self, direction):
        widget = self.resolve_focus_widget()
        if isinstance(widget, Gtk.ComboBox):
            return self.adjust_combo(widget, direction)
        if isinstance(widget, Gtk.RadioButton):
            return self.adjust_radio(widget, direction)
        if isinstance(widget, Gtk.CheckButton):
            widget.set_active(direction > 0)
            widget.grab_focus()
            return True
        if isinstance(widget, Gtk.Entry):
            return self.adjust_numeric_entry(widget, direction)
        return False

    def activate_focused_widget(self):
        widget = self.resolve_focus_widget()
        if isinstance(widget, Gtk.Button):
            widget.clicked()
            return True
        if isinstance(widget, Gtk.CheckButton):
            widget.set_active(not widget.get_active())
            return True
        if isinstance(widget, Gtk.RadioButton):
            widget.set_active(True)
            return True
        return False

    def on_key_press(self, _widget, event):
        key = event.keyval
        if key in (Gdk.KEY_Up, Gdk.KEY_KP_Up):
            return self.focus_next(-1)
        if key in (Gdk.KEY_Down, Gdk.KEY_KP_Down):
            return self.focus_next(1)
        if key in (Gdk.KEY_Left, Gdk.KEY_KP_Left):
            return self.adjust_focused_setting(-1)
        if key in (Gdk.KEY_Right, Gdk.KEY_KP_Right):
            return self.adjust_focused_setting(1)
        if key in (Gdk.KEY_Return, Gdk.KEY_KP_Enter, Gdk.KEY_space):
            return self.activate_focused_widget()
        return False

    def _section(self, title):
        frame = Gtk.Frame(label=title)
        grid = Gtk.Grid(column_spacing=8, row_spacing=4, margin=6)
        frame.add(grid)
        return frame, grid

    def _build_ui(self):
        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4, margin=8)
        self.add(root)

        header = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        title = Gtk.Label(label="T850 ENGINE")
        title.get_style_context().add_class("title")
        title.set_halign(Gtk.Align.START)
        subtitle = Gtk.Label(label="STEAM DECK")
        subtitle.get_style_context().add_class("subtitle")
        subtitle.set_halign(Gtk.Align.START)
        header.pack_start(title, False, False, 0)
        header.pack_start(subtitle, False, False, 0)
        root.pack_start(header, False, False, 0)

        scroller = Gtk.ScrolledWindow()
        scroller.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        scroller.set_min_content_height(max(280, self.window_height - 170))
        root.pack_start(scroller, True, True, 0)

        body = Gtk.Grid(column_spacing=8, row_spacing=8, margin=2)
        body.set_column_homogeneous(True)
        scroller.add(body)

        build_frame, build = self._section("BUILD CONFIGURATION")
        body.attach(build_frame, 0, 0, 1, 1)
        build.attach(self._label("Target"), 0, 0, 1, 1)
        build.attach(self._label("Architecture"), 1, 0, 1, 1)
        build.attach(self._label("Configuration"), 2, 0, 1, 1)
        self.target = self._combo(["Linux"], 0)
        self.arch = self._combo(["x64"], 0)
        self.config_combo = self._combo(["Release"], 0)
        build.attach(self.target, 0, 1, 1, 1)
        build.attach(self.arch, 1, 1, 1, 1)
        build.attach(self.config_combo, 2, 1, 1, 1)

        api_frame, api = self._section("GRAPHICS API")
        body.attach(api_frame, 1, 0, 1, 1)
        api.attach(self._label("API"), 0, 0, 1, 1)
        self.api = self._combo(["vulkan"], 0)
        api.attach(self.api, 0, 1, 1, 1)
        self.gui_on_start = self._check("Show runtime GUI on startup", False)
        api.attach(self.gui_on_start, 0, 2, 2, 1)

        content_frame, content = self._section("RUNTIME CONTENT")
        body.attach(content_frame, 0, 1, 1, 2)
        content.attach(self._label("Scene"), 0, 0, 1, 1)
        self.scene = self._combo([x[0] for x in SCENES], 1)
        content.attach(self.scene, 0, 1, 2, 1)
        content.attach(self._label("Sandbox Input"), 0, 2, 1, 1)
        self.sandbox_mode = self._combo(["Scene file (.t8scene)", "Model file"], 0)
        content.attach(self.sandbox_mode, 0, 3, 2, 1)
        content.attach(self._label("Model (Sandbox/Ragdoll)"), 0, 4, 2, 1)
        self.model = self._combo(self.models, 0, editable=True)
        content.attach(self.model, 0, 5, 1, 1)
        self.browse_model_button = Gtk.Button(label="Browse")
        self.browse_model_button.connect("clicked", lambda _w: self.browse_model())
        content.attach(self.browse_model_button, 1, 5, 1, 1)
        content.attach(self._label("Scene file (Sandbox/Quake3/Template)"), 0, 6, 2, 1)
        self.scene_file = self._combo(self.scene_files, 0, editable=True)
        content.attach(self.scene_file, 0, 7, 1, 1)
        self.browse_scene_button = Gtk.Button(label="Browse")
        self.browse_scene_button.connect("clicked", lambda _w: self.browse_scene_file())
        content.attach(self.browse_scene_button, 1, 7, 1, 1)

        display_frame, display = self._section("DISPLAY")
        body.attach(display_frame, 2, 0, 1, 1)
        display.attach(self._label("Width"), 0, 0, 1, 1)
        display.attach(self._label("Height"), 1, 0, 1, 1)
        self.width = self._entry("1280")
        self.height = self._entry("800")
        display.attach(self.width, 0, 1, 1, 1)
        display.attach(self.height, 1, 1, 1, 1)
        self.fullscreen = self._check("Fullscreen", True)
        display.attach(self.fullscreen, 0, 2, 2, 1)
        presets = [(1920, 1080), (2560, 1440), (3840, 2160), (1280, 800)]
        for i, (w, h) in enumerate(presets):
            btn = Gtk.Button(label=f"{w} x {h}" if (w, h) != (1280, 800) else "Max Device")
            btn.connect("clicked", lambda _b, ww=w, hh=h: self.set_resolution(ww, hh))
            display.attach(btn, i % 2, 3 + i // 2, 1, 1)

        snapshot_frame, snap = self._section("SNAPSHOT")
        body.attach(snapshot_frame, 1, 1, 1, 2)
        self.dump = self._check("Enable snapshot dump on run", False)
        self.debug_frames = self._check("Debug Frames (spacebar dumps + exits)", False)
        self.replay = self._check("Replay Snapshot (restore full scene state)", False)
        self.keep_running = self._check("Keep running after dump", False)
        snap.attach(self.dump, 0, 0, 2, 1)
        snap.attach(self.debug_frames, 0, 1, 2, 1)
        snap.attach(self.replay, 0, 2, 2, 1)
        snap.attach(self.keep_running, 0, 3, 2, 1)
        snap.attach(self._label("Replay path"), 0, 4, 2, 1)
        self.replay_path = self._entry("")
        snap.attach(self.replay_path, 0, 5, 1, 1)
        browse_replay = Gtk.Button(label="Browse")
        browse_replay.connect("clicked", lambda _w: self.browse_snapshot())
        snap.attach(browse_replay, 1, 5, 1, 1)
        self.dump_seconds_radio = Gtk.RadioButton.new_with_label_from_widget(None, "Dump at second")
        self.dump_frame_radio = Gtk.RadioButton.new_with_label_from_widget(self.dump_seconds_radio, "Dump at frame")
        self.dump_seconds_radio.set_active(True)
        self.dump_seconds_radio.connect("toggled", lambda _w: self.on_changed())
        self.dump_frame_radio.connect("toggled", lambda _w: self.on_changed())
        snap.attach(self.dump_seconds_radio, 0, 6, 1, 1)
        snap.attach(self.dump_frame_radio, 1, 6, 1, 1)
        snap.attach(self._label("Seconds"), 0, 7, 1, 1)
        snap.attach(self._label("Frame Number"), 1, 7, 1, 1)
        self.dump_seconds = self._entry("5")
        self.dump_frame = self._entry("300")
        snap.attach(self.dump_seconds, 0, 8, 1, 1)
        snap.attach(self.dump_frame, 1, 8, 1, 1)

        logging_frame, logging = self._section("LOGGING")
        body.attach(logging_frame, 0, 3, 1, 1)
        logging.attach(self._label("Log Level"), 0, 0, 2, 1)
        self.log_level = self._combo(["error", "info", "debug", "verbose", "trace"], 0)
        logging.attach(self.log_level, 0, 1, 2, 1)
        self.log_to_file = self._check("Save log to file", False)
        self.telemetry = self._check("Runtime telemetry JSON", False)
        logging.attach(self.log_to_file, 0, 2, 2, 1)
        logging.attach(self.telemetry, 0, 3, 2, 1)
        logging.attach(self._label("Telemetry frequency frames (0 = every frame)"), 0, 4, 2, 1)
        self.telemetry_frequency = self._entry("60")
        logging.attach(self.telemetry_frequency, 0, 5, 1, 1)

        options_frame, opts = self._section("RUNTIME OPTIONS")
        body.attach(options_frame, 1, 3, 2, 1)
        self.benchmark = self._check("Benchmark", False)
        opts.attach(self.benchmark, 0, 0, 2, 1)
        self.benchmark_single = Gtk.RadioButton.new_with_label_from_widget(None, "Single")
        self.benchmark_matrix = Gtk.RadioButton.new_with_label_from_widget(self.benchmark_single, "Matrix")
        self.benchmark_single.set_active(True)
        self.benchmark_single.connect("toggled", lambda _w: self.on_changed())
        self.benchmark_matrix.connect("toggled", lambda _w: self.on_changed())
        opts.attach(self.benchmark_single, 0, 1, 1, 1)
        opts.attach(self.benchmark_matrix, 1, 1, 1, 1)
        opts.attach(self._label("Benchmark seconds"), 0, 2, 1, 1)
        self.benchmark_seconds = self._entry("90")
        opts.attach(self.benchmark_seconds, 0, 3, 1, 1)
        opts.attach(self._label("Culling"), 1, 2, 1, 1)
        self.culling = self._combo(["full", "lazy", "disabled"], 0)
        opts.attach(self.culling, 1, 3, 1, 1)
        self.offscreen = self._check("Offscreen render", False)
        self.offscreen_debug = self._check("Offscreen debug dumps", False)
        self.profile = self._check("Profiler", False)
        opts.attach(self.offscreen, 0, 4, 1, 1)
        opts.attach(self.offscreen_debug, 1, 4, 1, 1)
        opts.attach(self.profile, 0, 5, 1, 1)
        opts.attach(self._label("Profile frames"), 1, 5, 1, 1)
        self.profile_frames = self._entry("300")
        opts.attach(self.profile_frames, 1, 6, 1, 1)

        preview_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=3)
        preview_box.get_style_context().add_class("footer")
        root.pack_start(preview_box, False, False, 0)
        self.status = Gtk.Label(label="")
        self.status.set_halign(Gtk.Align.START)
        preview_box.pack_start(self.status, False, False, 0)
        self.preview = Gtk.TextView()
        self.preview.set_editable(False)
        self.preview.set_can_focus(False)
        self.preview.set_wrap_mode(Gtk.WrapMode.WORD)
        self.preview.set_size_request(-1, 34)
        preview_box.pack_start(self.preview, False, False, 0)

        buttons = Gtk.Grid(column_spacing=8, column_homogeneous=True)
        buttons.get_style_context().add_class("footer")
        root.pack_start(buttons, False, False, 0)
        run = Gtk.Button(label="▶  RUN")
        run.get_style_context().add_class("run")
        run.connect("clicked", lambda _w: self.run_dayscene())
        editor = Gtk.Button(label="✎  EDITOR")
        editor.get_style_context().add_class("accent")
        editor.connect("clicked", lambda _w: self.run_editor())
        config = Gtk.Button(label="Open Config")
        config.connect("clicked", lambda _w: self.open_config())
        close = Gtk.Button(label="Close")
        close.connect("clicked", lambda _w: self.destroy())
        buttons.attach(run, 0, 0, 1, 1)
        buttons.attach(editor, 1, 0, 1, 1)
        buttons.attach(config, 2, 0, 1, 1)
        buttons.attach(close, 3, 0, 1, 1)

    def combo_text(self, combo):
        if combo.get_has_entry():
            return combo.get_child().get_text()
        return combo.get_active_text() or ""

    def set_combo_text(self, combo, text):
        model = combo.get_model()
        for idx, row in enumerate(model):
            if row[0] == text:
                combo.set_active(idx)
                return
        if combo.get_has_entry():
            combo.get_child().set_text(text)

    def preferred_scene_file_suffix(self, scene_id):
        if scene_id == 2:
            return "Q3/q3dm6_mod_3.t8scene"
        if scene_id == 4:
            return "Q3/q3dm6_mod_3_jolt.t8scene"
        return ""

    def select_preferred_scene_file_for_scene(self, scene_id):
        suffix = self.preferred_scene_file_suffix(scene_id)
        if not suffix:
            return
        normalized_suffix = suffix.lower().replace("\\", "/")
        for value in self.scene_files:
            if value.lower().replace("\\", "/").endswith(normalized_suffix):
                self.set_combo_text(self.scene_file, value)
                return

    def _load_defaults(self):
        cfg = self.config
        display = cfg.get("display", {}) if isinstance(cfg.get("display"), dict) else {}
        dev = cfg.get("devTools", {}) if isinstance(cfg.get("devTools"), dict) else {}
        dump = cfg.get("dump", {}) if isinstance(cfg.get("dump"), dict) else {}
        replay = cfg.get("replaySnapshot", {}) if isinstance(cfg.get("replaySnapshot"), dict) else {}
        telemetry = cfg.get("telemetry", {}) if isinstance(cfg.get("telemetry"), dict) else {}

        self.width.set_text(str(display.get("width", 1280)))
        self.height.set_text(str(display.get("height", 800)))
        self.fullscreen.set_active(bool(display.get("fullscreen", True)))
        self.set_combo_text(self.scene, SCENE_LABEL_BY_ID.get(int(display.get("scene", 1)), "Day Scene"))
        if display.get("model"):
            self.set_combo_text(self.model, display["model"])
            self.set_combo_text(self.sandbox_mode, "Model file")
        if display.get("sceneFile"):
            self.set_combo_text(self.scene_file, display["sceneFile"])
            self.set_combo_text(self.sandbox_mode, "Scene file (.t8scene)")
        self.debug_frames.set_active(bool(cfg.get("debugFrames", False)))
        self.keep_running.set_active(bool(cfg.get("keepRunning", False)))
        self.benchmark.set_active(bool(cfg.get("benchmark", dev.get("benchmark", False))))
        self.set_combo_text(self.culling, cfg.get("cullingMode", dev.get("cullingMode", "full")))
        self.set_combo_text(self.log_level, cfg.get("logLevel", dev.get("logLevel", "error")))
        self.log_to_file.set_active(bool(cfg.get("logToFile", dev.get("logToFile", False))))
        self.telemetry.set_active(bool(telemetry.get("enabled", False)))
        self.telemetry_frequency.set_text(str(telemetry.get("frequencyFrames", 60)))
        self.gui_on_start.set_active(bool(cfg.get("gui", dev.get("gui", False))))
        self.profile.set_active(bool(cfg.get("profile", dev.get("profile", False))))
        self.profile_frames.set_text(str(cfg.get("profileFrames", dev.get("profileFrames", 300))))
        self.offscreen.set_active(bool(cfg.get("offscreen", dev.get("offscreen", False))))
        self.offscreen_debug.set_active(bool(cfg.get("offscreenDebug", dev.get("offscreenDebug", False))))
        self.replay.set_active(bool(replay.get("enabled", False)))
        self.replay_path.set_text(str(replay.get("path", "")))
        self.dump.set_active(bool(dump.get("enabled", False)))
        if dump.get("trigger") == "frame":
            self.dump_frame_radio.set_active(True)
        else:
            self.dump_seconds_radio.set_active(True)
        self.dump_seconds.set_text(str(dump.get("seconds", 5)))
        self.dump_frame.set_text(str(dump.get("frame", 300)))
        self.update_visibility()

    def on_changed(self):
        self.update_visibility()

    def update_visibility(self):
        scene_label = self.combo_text(self.scene)
        scene_id = SCENE_ID_BY_LABEL.get(scene_label, 1)
        if self.current_scene_id != scene_id:
            self.current_scene_id = scene_id
            self.select_preferred_scene_file_for_scene(scene_id)
        sandbox = scene_id == 0
        sandbox_scene = sandbox and self.combo_text(self.sandbox_mode) == "Scene file (.t8scene)"
        sandbox_model = sandbox and self.combo_text(self.sandbox_mode) == "Model file"
        model_enabled = sandbox_model or scene_id == 3
        scene_file_enabled = sandbox_scene or scene_id in (2, 4)
        self.sandbox_mode.set_sensitive(sandbox)
        self.model.set_sensitive(model_enabled)
        self.browse_model_button.set_sensitive(model_enabled)
        self.scene_file.set_sensitive(scene_file_enabled)
        self.browse_scene_button.set_sensitive(scene_file_enabled)
        if scene_id in (2, 4):
            self.sandbox_mode.set_sensitive(False)
        if scene_id == 1:
            self.benchmark.set_sensitive(True)
            self.benchmark_single.set_sensitive(True)
            self.benchmark_matrix.set_sensitive(True)
        else:
            self.benchmark.set_active(False)
            self.benchmark.set_sensitive(False)
            self.benchmark_single.set_sensitive(False)
            self.benchmark_matrix.set_sensitive(False)
        self.status.set_text("Steam Deck Vulkan runtime")
        self.update_preview()

    def set_resolution(self, width, height):
        self.width.set_text(str(width))
        self.height.set_text(str(height))

    def browse_file(self, title, folder, patterns):
        dialog = Gtk.FileChooserDialog(title=title, parent=self, action=Gtk.FileChooserAction.OPEN)
        dialog.add_buttons(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL, Gtk.STOCK_OPEN, Gtk.ResponseType.OK)
        dialog.set_current_folder(str(folder))
        file_filter = Gtk.FileFilter()
        file_filter.set_name(title)
        for pattern in patterns:
            file_filter.add_pattern(pattern)
        dialog.add_filter(file_filter)
        response = dialog.run()
        filename = dialog.get_filename() if response == Gtk.ResponseType.OK else None
        dialog.destroy()
        return asset_path(filename) if filename else None

    def browse_model(self):
        selected = self.browse_file("Select model", ASSETS_DIR / "Models", ["*.glb", "*.gltf"])
        if selected:
            self.set_combo_text(self.model, selected)

    def browse_scene_file(self):
        selected = self.browse_file("Select .t8scene", ASSETS_DIR / "Scenes", ["*.t8scene"])
        if selected:
            self.set_combo_text(self.scene_file, selected)

    def browse_snapshot(self):
        selected = self.browse_file("Select snapshot.json", T850_ROOT, ["*.json"])
        if selected:
            self.replay_path.set_text(selected)
            self.replay.set_active(True)

    def build_args(self):
        args = ["--config", str(CONFIG_PATH), "--api", "vulkan"]
        scene_id = SCENE_ID_BY_LABEL.get(self.combo_text(self.scene), 1)
        args += ["--scene", str(scene_id)]
        if scene_id == 0:
            if self.combo_text(self.sandbox_mode) == "Model file":
                if self.combo_text(self.model):
                    args += ["--model", asset_path(self.combo_text(self.model))]
            elif self.combo_text(self.scene_file):
                args += ["--sceneFile", asset_path(self.combo_text(self.scene_file))]
        elif scene_id in (2, 4) and self.combo_text(self.scene_file):
            args += ["--sceneFile", asset_path(self.combo_text(self.scene_file))]
            args += ["--sceneProfile", "pc/windows"]
        elif scene_id == 3 and self.combo_text(self.model):
            args += ["--model", asset_path(self.combo_text(self.model))]
        if self.fullscreen.get_active():
            args.append("--fullscreen")
        args += ["--width", self.width.get_text(), "--height", self.height.get_text()]
        args += ["--logLevel", self.combo_text(self.log_level)]
        if self.debug_frames.get_active():
            args.append("--debugFrames")
        if self.replay.get_active() and self.replay_path.get_text():
            args += ["--replaySnapshot", self.replay_path.get_text()]
        if self.keep_running.get_active():
            args.append("--keepRunning")
        if self.dump.get_active():
            if self.dump_frame_radio.get_active():
                args += ["--dumpSnapshot-frame", self.dump_frame.get_text()]
            else:
                args += ["--dumpSnapshot-seconds", self.dump_seconds.get_text()]
        if self.benchmark.get_active() and scene_id == 1:
            if self.benchmark_matrix.get_active():
                ts = datetime.now().strftime("%Y%m%d_%H%M%S")
                args += ["--benchmarkMatrix", "--benchmarkSeconds", self.benchmark_seconds.get_text(), "--benchmarkReport", f"benchmark_reports/dayscene_matrix_{ts}/DayScene_Benchmark_Report.md"]
            else:
                args += ["--benchmark", "--benchmarkSeconds", self.benchmark_seconds.get_text()]
        args += ["--culling", self.combo_text(self.culling)]
        if self.log_to_file.get_active():
            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
            args += ["--logFile", f"logs/T850_{ts}_vulkan.log"]
        if self.telemetry.get_active():
            args += ["--telemetry", "--telemetryFrequencyFrames", self.telemetry_frequency.get_text(), "--telemetryOutput", "logs/perf_telemetry_steamdeck.json"]
        if self.gui_on_start.get_active():
            args.append("--gui")
        if self.profile.get_active():
            args += ["--profile", "--profileFrames", self.profile_frames.get_text()]
        if self.offscreen.get_active():
            args.append("--offscreen")
        if self.offscreen_debug.get_active():
            args.append("--offscreenDebug")
        return args

    def save_config(self):
        scene_id = SCENE_ID_BY_LABEL.get(self.combo_text(self.scene), 1)
        display = {
            "width": int(self.width.get_text()),
            "height": int(self.height.get_text()),
            "fullscreen": bool(self.fullscreen.get_active()),
            "scene": scene_id,
            "title": "T850 Steam Deck",
        }
        if scene_id == 0:
            if self.combo_text(self.sandbox_mode) == "Model file":
                display["model"] = asset_path(self.combo_text(self.model))
            else:
                display["sceneFile"] = asset_path(self.combo_text(self.scene_file))
        elif scene_id in (2, 4):
            display["sceneFile"] = asset_path(self.combo_text(self.scene_file))
            display["sceneProfile"] = "pc/windows"
        elif scene_id == 3:
            display["model"] = asset_path(self.combo_text(self.model))
        cfg = {
            "api": "vulkan",
            "targetPlatform": "linux",
            "architecture": "x64",
            "configuration": "Release",
            "display": display,
            "debugFrames": self.debug_frames.get_active(),
            "benchmark": self.benchmark.get_active() and scene_id == 1,
            "cullingMode": self.combo_text(self.culling),
            "cullDisabled": self.combo_text(self.culling) == "disabled",
            "keepRunning": self.keep_running.get_active(),
            "replaySnapshot": {"enabled": self.replay.get_active(), "path": self.replay_path.get_text()},
            "dump": {
                "enabled": self.dump.get_active(),
                "trigger": "frame" if self.dump_frame_radio.get_active() else "seconds",
                "seconds": float(self.dump_seconds.get_text() or 0),
                "frame": int(self.dump_frame.get_text() or 0),
            },
            "devTools": {
                "gui": self.gui_on_start.get_active(),
                "logLevel": self.combo_text(self.log_level),
                "logToFile": self.log_to_file.get_active(),
                "profile": self.profile.get_active(),
                "profileFrames": int(self.profile_frames.get_text() or 300),
                "benchmark": self.benchmark.get_active(),
                "cullingMode": self.combo_text(self.culling),
                "offscreen": self.offscreen.get_active(),
                "offscreenDebug": self.offscreen_debug.get_active(),
            },
            "telemetry": {
                "enabled": self.telemetry.get_active(),
                "frequencyFrames": int(self.telemetry_frequency.get_text() or 0),
                "outputPath": "logs/perf_telemetry_steamdeck.json",
            },
        }
        CONFIG_PATH.write_text(json.dumps(cfg, indent=2), encoding="utf-8")

    def update_preview(self):
        try:
            cmd = quote_cmd([str(DAYSCENE)] + self.build_args())
        except Exception as exc:
            cmd = f"Invalid options: {exc}"
        buffer = self.preview.get_buffer()
        buffer.set_text(cmd)

    def validate_common(self):
        if not DAYSCENE.exists():
            self.error(f"DayScene not found:\n{DAYSCENE}")
            return False
        if not self.width.get_text().isdigit() or not self.height.get_text().isdigit():
            self.error("Width and height must be positive integers.")
            return False
        return True

    def error(self, text):
        dialog = Gtk.MessageDialog(parent=self, flags=0, message_type=Gtk.MessageType.ERROR, buttons=Gtk.ButtonsType.OK, text="T850 Launcher")
        dialog.format_secondary_text(text)
        dialog.run()
        dialog.destroy()

    def launch_process(self, executable, args):
        ensure_runtime_links()
        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = f"{RUNTIME_DIR}:{env.get('LD_LIBRARY_PATH', '')}"
        env["SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS"] = "0"
        env["SDL_GAMECONTROLLER_USE_BUTTON_LABELS"] = "1"
        subprocess.Popen([str(executable)] + args, cwd=str(T850_ROOT), env=env, start_new_session=True)
        self.destroy()

    def run_dayscene(self):
        if not self.validate_common():
            return
        self.save_config()
        self.launch_process(DAYSCENE, self.build_args())

    def run_editor(self):
        if not EDITOR.exists():
            self.error("T8ditor is not built for Steam Deck yet. Rebuild with --with-editor once editor support is enabled.")
            return
        args = ["--api", "vulkan", "--width", self.width.get_text(), "--height", self.height.get_text(), "--logLevel", self.combo_text(self.log_level)]
        self.launch_process(EDITOR, args)

    def open_config(self):
        try:
            self.save_config()
        except Exception:
            pass
        for opener in ("kwrite", "xdg-open"):
            if subprocess.call(["which", opener], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
                subprocess.Popen([opener, str(CONFIG_PATH)], start_new_session=True)
                return
        self.error(f"Config path:\n{CONFIG_PATH}")


if __name__ == "__main__":
    launcher = Launcher()
    launcher.show_all()
    Gtk.main()
