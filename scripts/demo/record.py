#!/usr/bin/env python3
"""
Screen recording for the demo, through OBS's own websocket.

    python scripts/demo/record.py setup     # launch OBS if needed and configure it (idempotent)
    python scripts/demo/record.py start NAME
    python scripts/demo/record.py stop

demo.py calls start/stop around every shot, so each test run leaves a clip.

WHY OBS AND WHY THE WEBSOCKET. OBS captures the game AND its audio - the voice
lines are the point of the demo. Driving it by websocket rather than by killing
the process is what keeps a recording intact; the format is MKV regardless,
because an MKV cut off mid-write is still playable where an MP4 is lost.

The websocket password lives in ~/.obs-websocket/password and is read here,
never printed. Setup builds everything through the API (a scene, a Game
Capture of Fallout4.exe, desktop audio, the output folder, MKV, NVENC) instead
of hand-writing OBS's scene JSON, which OBS owns and can change between versions.
"""
import pathlib
import subprocess
import sys
import time

import obsws_python as obs

OBS_DIR = pathlib.Path(r"C:\Program Files\obs-studio\bin\64bit")
PASSWORD = (pathlib.Path.home() / ".obs-websocket" / "password").read_text(encoding="utf-8").strip()
FOLDER = pathlib.Path.home() / "Videos" / "Rapport demo"
SCENE, CAPTURE = "Rapport Demo", "Fallout 4"


def running():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq obs64.exe"], capture_output=True, text=True).stdout
    return "obs64.exe" in out


def client(timeout=60.0):
    if not running():
        # The working directory matters: OBS resolves its data files relative to it.
        subprocess.Popen([str(OBS_DIR / "obs64.exe"), "--minimize-to-tray", "--disable-shutdown-check"],
                         cwd=str(OBS_DIR), creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
    end = time.time() + timeout
    while time.time() < end:
        try:
            c = obs.ReqClient(host="localhost", port=4455, password=PASSWORD, timeout=5)
            # Connected is not ready: while OBS is still starting (or a first-run
            # dialog is open) requests answer code 207, 'not ready'. Wait it out.
            c.get_scene_list()
            return c
        except Exception:
            time.sleep(1.0)
    sys.exit("OBS did not open its websocket - is it running, and was it started after the config was written?")


def setup():
    c = client()
    scenes = [s["sceneName"] for s in c.get_scene_list().scenes]
    if SCENE not in scenes:
        c.create_scene(SCENE)
    inputs = [i["inputName"] for i in c.get_input_list().inputs]
    if CAPTURE not in inputs:
        # Window mode, matched by executable: survives the title changing, and does
        # not capture anything else if the game is not running.
        c.create_input(SCENE, CAPTURE, "game_capture",
                       {"capture_mode": "window", "window": "Fallout4:Fallout4:Fallout4.exe", "priority": 2,
                        "capture_cursor": False}, True)
    c.set_current_program_scene(SCENE)
    FOLDER.mkdir(parents=True, exist_ok=True)
    c.set_record_directory(str(FOLDER))
    for category, name, value in (("Output", "Mode", "Simple"), ("SimpleOutput", "RecFormat2", "mkv"),
                                  ("SimpleOutput", "RecQuality", "HQ"), ("SimpleOutput", "RecEncoder", "nvenc"),
                                  ("Video", "FPSCommon", "60")):
        c.set_profile_parameter(category, name, value)
    # The game runs at 1920x1080; OBS defaults its canvas and output to 1280x720.
    # Record what the game draws, not a downscale of it.
    c.set_video_settings(numerator=60, denominator=1, base_width=1920, base_height=1080,
                         out_width=1920, out_height=1080)
    audio = [i["inputName"] for i in c.get_input_list("wasapi_output_capture").inputs]
    print(f"OBS ready: scene '{SCENE}', capture '{CAPTURE}', desktop audio {audio or 'NOT FOUND'}, "
          f"recording MKV to {FOLDER}")
    return c


def start(name):
    c = client()
    if c.get_record_status().output_active:
        c.stop_record()
        time.sleep(1.5)
    c.set_profile_parameter("Output", "FilenameFormatting", f"%CCYY-%MM-%DD %hh-%mm-%ss {name}")
    c.start_record()
    print(f"recording: {name}")


def stop():
    c = client()
    if not c.get_record_status().output_active:
        print("not recording")
        return None
    path = c.stop_record().output_path
    print(f"saved: {path}")
    return path


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "setup"
    {"setup": lambda: setup(), "start": lambda: start(sys.argv[2] if len(sys.argv) > 2 else "shot"),
     "stop": lambda: stop()}[what]()
