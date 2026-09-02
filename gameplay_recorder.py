#!/usr/bin/env python3
"""
Lightweight Gameplay Recorder for Windows
=========================================

Architecture (why it stays extremely light on RAM/CPU):
- Screen + system audio captured and encoded entirely by an external ffmpeg process.
  Python only starts/stops that process and never touches frames or audio buffers.
- ffmpeg is spawned ONLY while recording is active. When paused or idle the process
  is terminated → CPU/RAM usage drops to near zero.
- UI is pure tkinter (ships with Python on Windows) – no Qt, no Electron, no web view.
- Global hotkeys via pynput (tiny native hooks).
- Config is a plain JSON file. No database, no heavy serializers.

Target: feel invisible while a game is running.
Primary platform: Windows 10/11 (gdigrab + wasapi).

Requirements:
  - Python 3.8+
  - pynput  (pip install pynput)
  - ffmpeg  in PATH (https://ffmpeg.org/download.html) – recent build recommended
    for wasapi support.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog

try:
    from pynput import keyboard
except ImportError:
    print("Missing dependency: pynput")
    print("Install with:  pip install pynput")
    sys.exit(1)


# ---------------------------------------------------------------------------
# Constants & defaults
# ---------------------------------------------------------------------------

APP_NAME = "Lightweight Gameplay Recorder"
CONFIG_NAME = "config.json"
DEFAULT_HOTKEY = "f9"
DEFAULT_FPS = 30
DEFAULT_CRF = 23
DEFAULT_PRESET = "ultrafast"
DEFAULT_FOLDER = str(Path.home() / "Videos" / "Gameplays")

# States
IDLE = "idle"
RECORDING = "recording"
PAUSED = "paused"


# ---------------------------------------------------------------------------
# Config helpers
# ---------------------------------------------------------------------------

def get_script_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).parent
    return Path(__file__).resolve().parent


def load_config() -> dict:
    cfg_path = get_script_dir() / CONFIG_NAME
    defaults = {
        "save_folder": DEFAULT_FOLDER,
        "hotkey": DEFAULT_HOTKEY,
        "fps": DEFAULT_FPS,
        "crf": DEFAULT_CRF,
        "preset": DEFAULT_PRESET,
        "use_hardware_encoder": True,
        "audio_enabled": True,
        "draw_mouse": True,
    }
    if cfg_path.exists():
        try:
            with open(cfg_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            defaults.update(data)
        except Exception:
            pass
    return defaults


def save_config(cfg: dict) -> None:
    cfg_path = get_script_dir() / CONFIG_NAME
    try:
        with open(cfg_path, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)
    except Exception as e:
        print(f"Warning: could not save config: {e}")


# ---------------------------------------------------------------------------
# FFmpeg helpers
# ---------------------------------------------------------------------------

def find_ffmpeg() -> Optional[str]:
    """Return path to ffmpeg executable or None."""
    return shutil.which("ffmpeg")


def detect_hw_encoder() -> Optional[str]:
    """
    Probe ffmpeg for the best available hardware H.264 encoder.
    Returns encoder name or None (caller should fall back to libx264).
    """
    ffmpeg = find_ffmpeg()
    if not ffmpeg:
        return None
    try:
        # Quick probe – just ask for encoders
        out = subprocess.check_output(
            [ffmpeg, "-hide_banner", "-encoders"],
            stderr=subprocess.STDOUT,
            text=True,
            timeout=8,
        )
        # Preference order: NVIDIA > Intel > AMD
        if "h264_nvenc" in out:
            return "h264_nvenc"
        if "h264_qsv" in out:
            return "h264_qsv"
        if "h264_amf" in out:
            return "h264_amf"
    except Exception:
        pass
    return None


def build_ffmpeg_cmd(
    output_path: Path,
    fps: int = 30,
    crf: int = 23,
    preset: str = "ultrafast",
    use_hw: bool = True,
    audio: bool = True,
    draw_mouse: bool = True,
) -> list[str]:
    """
    Build a low-overhead ffmpeg command for Windows desktop + system audio.
    """
    ffmpeg = find_ffmpeg()
    if not ffmpeg:
        raise RuntimeError("ffmpeg not found in PATH")

    cmd: list[str] = [
        ffmpeg,
        "-y",                    # overwrite
        "-hide_banner",
        "-loglevel", "error",
        # ---- Video (gdigrab is the most compatible Windows desktop grabber)
        "-f", "gdigrab",
        "-framerate", str(fps),
        "-draw_mouse", "1" if draw_mouse else "0",
        "-i", "desktop",
    ]

    # ---- Audio (WASAPI loopback = system sound). Requires a recent ffmpeg build.
    if audio:
        cmd += [
            "-f", "wasapi",
            "-i", "default",     # default playback device loopback
        ]

    # ---- Encoder
    hw = detect_hw_encoder() if use_hw else None
    if hw == "h264_nvenc":
        cmd += [
            "-c:v", "h264_nvenc",
            "-preset", "p4",         # balanced speed/quality for nvenc
            "-cq", str(crf),
            "-pix_fmt", "yuv420p",
        ]
    elif hw == "h264_qsv":
        cmd += [
            "-c:v", "h264_qsv",
            "-preset", "veryfast",
            "-global_quality", str(crf),
            "-pix_fmt", "nv12",
        ]
    elif hw == "h264_amf":
        cmd += [
            "-c:v", "h264_amf",
            "-quality", "speed",
            "-rc", "cqp",
            "-qp_i", str(crf),
            "-qp_p", str(crf),
            "-pix_fmt", "yuv420p",
        ]
    else:
        # Software – ultrafast + zerolatency keeps CPU impact minimal
        cmd += [
            "-c:v", "libx264",
            "-preset", preset,
            "-tune", "zerolatency",
            "-crf", str(crf),
            "-pix_fmt", "yuv420p",
        ]

    if audio:
        cmd += [
            "-c:a", "aac",
            "-b:a", "128k",
            "-ac", "2",
        ]

    cmd += [
        "-movflags", "+faststart",
        str(output_path),
    ]
    return cmd


# ---------------------------------------------------------------------------
# Recorder core
# ---------------------------------------------------------------------------

class Recorder:
    """Manages the ffmpeg process and state machine."""

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.state = IDLE
        self.proc: Optional[subprocess.Popen] = None
        self.current_file: Optional[Path] = None
        self.start_time: Optional[float] = None
        self.elapsed_before_pause: float = 0.0
        self._lock = threading.Lock()

    @property
    def is_active(self) -> bool:
        return self.state in (RECORDING, PAUSED)

    def _make_output_path(self) -> Path:
        folder = Path(self.cfg["save_folder"])
        folder.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        return folder / f"gameplay_{ts}.mp4"

    def start(self) -> tuple[bool, str]:
        """Start a new recording. Returns (success, message)."""
        with self._lock:
            if self.state == RECORDING:
                return False, "Already recording"
            if self.state == PAUSED:
                # Resume = start a new file (clean & reliable)
                return self._start_new()

            return self._start_new()

    def _start_new(self) -> tuple[bool, str]:
        if not find_ffmpeg():
            return False, "ffmpeg not found. Install it and add to PATH."

        out = self._make_output_path()
        try:
            cmd = build_ffmpeg_cmd(
                out,
                fps=int(self.cfg.get("fps", DEFAULT_FPS)),
                crf=int(self.cfg.get("crf", DEFAULT_CRF)),
                preset=self.cfg.get("preset", DEFAULT_PRESET),
                use_hw=bool(self.cfg.get("use_hardware_encoder", True)),
                audio=bool(self.cfg.get("audio_enabled", True)),
                draw_mouse=bool(self.cfg.get("draw_mouse", True)),
            )
        except Exception as e:
            return False, str(e)

        try:
            # CREATE_NO_WINDOW keeps the console clean on Windows
            creationflags = 0
            if sys.platform == "win32":
                creationflags = subprocess.CREATE_NO_WINDOW

            self.proc = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                creationflags=creationflags,
            )
            self.current_file = out
            self.state = RECORDING
            self.start_time = time.time()
            self.elapsed_before_pause = 0.0
            return True, f"Recording → {out.name}"
        except Exception as e:
            self.proc = None
            return False, f"Failed to start ffmpeg: {e}"

    def pause(self) -> tuple[bool, str]:
        """Stop the current ffmpeg process (finalize file) and mark paused."""
        with self._lock:
            if self.state != RECORDING:
                return False, "Not recording"
            self._stop_process()
            if self.start_time is not None:
                self.elapsed_before_pause += time.time() - self.start_time
            self.start_time = None
            self.state = PAUSED
            name = self.current_file.name if self.current_file else "?"
            return True, f"Paused (saved {name})"

    def resume(self) -> tuple[bool, str]:
        """Start a new file after pause."""
        with self._lock:
            if self.state != PAUSED:
                return False, "Not paused"
            return self._start_new()

    def stop(self) -> tuple[bool, str]:
        """Fully stop and go to idle."""
        with self._lock:
            if self.state == IDLE:
                return False, "Already idle"
            was_recording = self.state == RECORDING
            self._stop_process()
            self.state = IDLE
            self.start_time = None
            self.elapsed_before_pause = 0.0
            name = self.current_file.name if self.current_file else "file"
            self.current_file = None
            msg = f"Stopped – saved {name}" if was_recording or name != "file" else "Stopped"
            return True, msg

    def _stop_process(self) -> None:
        if self.proc is None:
            return
        try:
            # Graceful quit
            if self.proc.stdin:
                try:
                    self.proc.stdin.write(b"q\n")
                    self.proc.stdin.flush()
                except Exception:
                    pass
            self.proc.wait(timeout=4)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            try:
                self.proc.wait(timeout=2)
            except Exception:
                pass
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass
        finally:
            self.proc = None

    def get_elapsed(self) -> float:
        """Total recorded seconds (excluding paused time)."""
        extra = 0.0
        if self.state == RECORDING and self.start_time is not None:
            extra = time.time() - self.start_time
        return self.elapsed_before_pause + extra

    def cleanup(self) -> None:
        with self._lock:
            self._stop_process()
            self.state = IDLE


# ---------------------------------------------------------------------------
# Hotkey listener
# ---------------------------------------------------------------------------

class HotkeyManager:
    def __init__(self, hotkey_str: str, callback):
        self.hotkey_str = hotkey_str.lower().strip()
        self.callback = callback
        self.listener: Optional[keyboard.Listener] = None
        self._pressed = set()

    def _parse(self) -> set:
        """Very simple parser: 'f9', 'ctrl+shift+r', etc."""
        parts = [p.strip() for p in self.hotkey_str.replace(" ", "").split("+")]
        return set(parts)

    def start(self) -> None:
        target = self._parse()

        def on_press(key):
            try:
                k = key.char.lower() if hasattr(key, "char") and key.char else str(key).replace("Key.", "").lower()
            except Exception:
                return
            self._pressed.add(k)
            # Check if all required keys are currently pressed
            if target.issubset(self._pressed):
                # Fire once per chord
                self._pressed.clear()
                self.callback()

        def on_release(key):
            try:
                k = key.char.lower() if hasattr(key, "char") and key.char else str(key).replace("Key.", "").lower()
                self._pressed.discard(k)
            except Exception:
                pass

        self.listener = keyboard.Listener(on_press=on_press, on_release=on_release)
        self.listener.daemon = True
        self.listener.start()

    def stop(self) -> None:
        if self.listener:
            self.listener.stop()
            self.listener = None

    def update_hotkey(self, new_hotkey: str) -> None:
        self.stop()
        self.hotkey_str = new_hotkey.lower().strip()
        self.start()


# ---------------------------------------------------------------------------
# UI
# ---------------------------------------------------------------------------

class App:
    def __init__(self):
        self.cfg = load_config()
        self.recorder = Recorder(self.cfg)
        self.root = tk.Tk()
        self.root.title(APP_NAME)
        self.root.geometry("420x280")
        self.root.resizable(False, False)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

        # Status
        self.status_var = tk.StringVar(value="Idle")
        self.file_var = tk.StringVar(value="—")
        self.time_var = tk.StringVar(value="00:00:00")
        self.folder_var = tk.StringVar(value=self.cfg["save_folder"])
        self.hotkey_var = tk.StringVar(value=self.cfg["hotkey"].upper())

        self._build_ui()
        self._update_status_color()

        # Hotkey
        self.hotkey_mgr = HotkeyManager(self.cfg["hotkey"], self._on_hotkey)
        self.hotkey_mgr.start()

        # Timer
        self._tick()

    def _build_ui(self) -> None:
        pad = {"padx": 8, "pady": 4}

        # Status row
        frm_status = tk.Frame(self.root)
        frm_status.pack(fill="x", **pad)
        tk.Label(frm_status, text="Status:", font=("Segoe UI", 10)).pack(side="left")
        self.lbl_status = tk.Label(
            frm_status, textvariable=self.status_var,
            font=("Segoe UI", 11, "bold"), width=12, anchor="w"
        )
        self.lbl_status.pack(side="left", padx=6)
        tk.Label(frm_status, textvariable=self.time_var, font=("Consolas", 11)).pack(side="right")

        # Current file
        frm_file = tk.Frame(self.root)
        frm_file.pack(fill="x", **pad)
        tk.Label(frm_file, text="File:", font=("Segoe UI", 9)).pack(side="left")
        tk.Label(frm_file, textvariable=self.file_var, font=("Segoe UI", 9),
                 fg="#555", anchor="w").pack(side="left", fill="x", expand=True)

        # Folder
        frm_folder = tk.Frame(self.root)
        frm_folder.pack(fill="x", **pad)
        tk.Label(frm_folder, text="Save folder:", font=("Segoe UI", 9)).pack(anchor="w")
        row = tk.Frame(frm_folder)
        row.pack(fill="x")
        tk.Entry(row, textvariable=self.folder_var, state="readonly",
                 font=("Segoe UI", 9)).pack(side="left", fill="x", expand=True, padx=(0, 4))
        tk.Button(row, text="Change…", command=self.change_folder,
                  font=("Segoe UI", 9)).pack(side="right")

        # Hotkey
        frm_hk = tk.Frame(self.root)
        frm_hk.pack(fill="x", **pad)
        tk.Label(frm_hk, text="Hotkey (start / pause / resume):",
                 font=("Segoe UI", 9)).pack(side="left")
        tk.Label(frm_hk, textvariable=self.hotkey_var,
                 font=("Segoe UI", 10, "bold"), fg="#0066cc").pack(side="left", padx=6)
        tk.Button(frm_hk, text="Change…", command=self.change_hotkey,
                  font=("Segoe UI", 9)).pack(side="right")

        # Buttons
        frm_btn = tk.Frame(self.root)
        frm_btn.pack(fill="x", pady=12, padx=8)
        self.btn_start = tk.Button(
            frm_btn, text="Start", width=10, command=self.do_start,
            font=("Segoe UI", 10), bg="#28a745", fg="white"
        )
        self.btn_start.pack(side="left", padx=4)
        self.btn_pause = tk.Button(
            frm_btn, text="Pause", width=10, command=self.do_pause,
            font=("Segoe UI", 10), state="disabled"
        )
        self.btn_pause.pack(side="left", padx=4)
        self.btn_stop = tk.Button(
            frm_btn, text="Stop", width=10, command=self.do_stop,
            font=("Segoe UI", 10), bg="#dc3545", fg="white", state="disabled"
        )
        self.btn_stop.pack(side="left", padx=4)

        # Hint
        tk.Label(
            self.root,
            text="Tip: Hotkey works even when the game is focused.\n"
                 "Pause finalizes the current file; Resume starts a new one.",
            font=("Segoe UI", 8), fg="#666", justify="left"
        ).pack(pady=(4, 0))

    def _update_status_color(self) -> None:
        colors = {
            IDLE: ("#333333", "Idle"),
            RECORDING: ("#c0392b", "● RECORDING"),
            PAUSED: ("#e67e22", "❚❚ PAUSED"),
        }
        fg, text = colors.get(self.recorder.state, ("#333", "?"))
        self.status_var.set(text)
        self.lbl_status.configure(fg=fg)

        # Button states
        st = self.recorder.state
        self.btn_start.configure(state="normal" if st in (IDLE, PAUSED) else "disabled")
        self.btn_pause.configure(state="normal" if st == RECORDING else "disabled")
        self.btn_stop.configure(state="normal" if st != IDLE else "disabled")

        if self.recorder.current_file:
            self.file_var.set(self.recorder.current_file.name)
        elif st == IDLE:
            self.file_var.set("—")

    def _tick(self) -> None:
        # Update duration
        secs = int(self.recorder.get_elapsed())
        h, rem = divmod(secs, 3600)
        m, s = divmod(rem, 60)
        self.time_var.set(f"{h:02d}:{m:02d}:{s:02d}")
        self.root.after(250, self._tick)

    def _on_hotkey(self) -> None:
        # Toggle logic: Idle→Start, Recording→Pause, Paused→Resume
        st = self.recorder.state
        if st == IDLE:
            self.do_start()
        elif st == RECORDING:
            self.do_pause()
        elif st == PAUSED:
            self.do_start()  # resume

    def do_start(self) -> None:
        ok, msg = self.recorder.start()
        self._update_status_color()
        if not ok:
            messagebox.showerror(APP_NAME, msg)
        # else silent – status already shows

    def do_pause(self) -> None:
        ok, msg = self.recorder.pause()
        self._update_status_color()
        if not ok:
            messagebox.showwarning(APP_NAME, msg)

    def do_stop(self) -> None:
        ok, msg = self.recorder.stop()
        self._update_status_color()
        if ok:
            # Brief confirmation is nice
            pass

    def change_folder(self) -> None:
        path = filedialog.askdirectory(
            title="Select save folder",
            initialdir=self.cfg["save_folder"]
        )
        if path:
            self.cfg["save_folder"] = path
            self.folder_var.set(path)
            save_config(self.cfg)

    def change_hotkey(self) -> None:
        new = simpledialog.askstring(
            "Change Hotkey",
            "Enter new hotkey (examples: f9   or   ctrl+shift+r):",
            initialvalue=self.cfg["hotkey"],
            parent=self.root,
        )
        if new and new.strip():
            self.cfg["hotkey"] = new.strip().lower()
            self.hotkey_var.set(self.cfg["hotkey"].upper())
            self.hotkey_mgr.update_hotkey(self.cfg["hotkey"])
            save_config(self.cfg)

    def on_close(self) -> None:
        if self.recorder.is_active:
            if not messagebox.askyesno(
                APP_NAME,
                "Recording is still active. Stop and quit?"
            ):
                return
            self.recorder.stop()
        self.hotkey_mgr.stop()
        self.recorder.cleanup()
        self.root.destroy()

    def run(self) -> None:
        # Pre-flight checks
        if not find_ffmpeg():
            messagebox.showerror(
                APP_NAME,
                "ffmpeg was not found in your PATH.\n\n"
                "1. Download a recent Windows build from https://ffmpeg.org\n"
                "2. Extract it and add the 'bin' folder to your system PATH\n"
                "3. Restart this program.\n\n"
                "Without ffmpeg the recorder cannot run."
            )
            # Still show UI so user can see the message, but disable buttons
            self.btn_start.configure(state="disabled")
            self.btn_pause.configure(state="disabled")
            self.btn_stop.configure(state="disabled")
        self.root.mainloop()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    # Ensure default folder exists
    cfg = load_config()
    Path(cfg["save_folder"]).mkdir(parents=True, exist_ok=True)
    save_config(cfg)  # write defaults on first run

    app = App()
    app.run()


if __name__ == "__main__":
    main()
