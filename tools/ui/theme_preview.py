#!/usr/bin/env python3
"""Real-renderer design previews: launch Aestra on a private display with a theme (and optional token overrides),
drive it to named scenes, capture each as a PNG, and lay several runs side by side on a contact sheet.

A browser mock never matches the app (different font rasteriser, different blending, different layout), so a colour
or surface idea is judged from this, not from HTML. It reuses tools/ui: run.sh starts the app (silent, own HOME,
own display), drive.py sends the real pointer and key events and takes the screenshots.

  tools/ui/theme_preview.sh run   --label A [--theme ink-dark] [--set primary=#9f63fd ...] [--overrides FILE.json]
                                  [--scenes arsenal-4rows,mixer] [--bin PATH/Aestra] [--out DIR] [--display :77]
  tools/ui/theme_preview.sh sheet --out DIR --labels A,B,C,D [--scenes ...]
  tools/ui/theme_preview.sh list

run    writes DIR/LABEL/<scene>.png (1366x768 each) and DIR/LABEL/overrides.json.
sheet  writes DIR/sheet-<scene>.png: one scene, each label's screenshot at native size side by side, labelled.
       Nothing is scaled; a wide sheet is a wide PNG.

Token overrides ride on a theme JSON ({"colors": {...}}) that the app lays over the chosen theme when the
AESTRA_THEME_OVERRIDES environment variable names it (Source/Core/AestraWindowManager.cpp). Keys are the theme
property names (primary, selected, textOnPrimary, backgroundPrimary, backgroundSecondary, surfaceTertiary,
surfaceRaised, ...). Setting `primary` alone derives `selected` as a 18% wash, so Ink previews set both.

A binary built with a code change (not a token change) is previewed with --bin; the label is only a name.
Scenes assume the 1366x768 display run.sh makes, and a fresh project; each run starts from an empty state folder
under DIR/.state so nothing from the real app leaks in. Needs python-xlib, Pillow, Xvfb and bubblewrap.
"""
import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import time
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUN = HERE / "run.sh"
STOP = HERE / "stop.sh"
DRIVE = HERE / "drive.py"

SCENES = [
    "arsenal-4rows",    # Arsenal, four units, the last one selected
    "context-menu",     # a unit's context menu with a row hovered and its submenu open
    "arsenal-pianoroll",  # Arsenal with the piano roll docked, one note selected
    "file-browser",     # the browser with a folder row selected, then a sample row and the preview panel
    "sample-editor",    # the Sample Editor on a loaded unit
    "mixer",            # the mixer panel with four strips
    "timeline",         # the timeline with two clips on a track
    "clip-editor",      # the Audio Clip Editor on a clip
    "patterns",         # the Patterns list
    "audition",         # Audition, Files & queue
    "versions",         # the Versions panel
    "settings",         # Settings, Look page
]


def write_samples(home: Path):
    folder = home / "Documents" / "Aestra" / "Samples"
    folder.mkdir(parents=True, exist_ok=True)
    import random
    random.seed(1)
    sounds = {
        "Kick": lambda t: math.sin(2 * math.pi * (50 + 200 * math.exp(-t * 30)) * t) * math.exp(-t * 8),
        "Snare": lambda t: (random.uniform(-1, 1) * 0.7 + math.sin(2 * math.pi * 190 * t) * 0.3) * math.exp(-t * 14),
        "Hat": lambda t: random.uniform(-1, 1) * math.exp(-t * 40),
        "Bass": lambda t: math.sin(2 * math.pi * 55 * t) * math.exp(-t * 3),
    }
    for name, fn in sounds.items():
        sr = 44100
        with wave.open(str(folder / f"{name}.wav"), "w") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(sr)
            w.writeframes(b"".join(struct.pack("<h", int(32000 * fn(i / sr))) for i in range(sr // 2)))


class Session:
    def __init__(self, args, out: Path):
        self.args, self.out = args, out
        self.display = args.display
        self.env = dict(os.environ)
        self.state_root = out.parent / ".state"
        self.env["AESTRA_UI_STATE"] = str(self.state_root)
        self.flags = set()

    def drive(self, *cmd):
        subprocess.run([sys.executable, str(DRIVE), "--display", self.display, *map(str, cmd)], check=True)

    def shot(self, scene):
        self.drive("move", 700, 690, "wait", 0.6, "shot", self.out / f"{scene}.png")
        print(f"  {scene}")

    def start(self):
        num = self.display.lstrip(":")
        state = self.state_root / num
        shutil.rmtree(state, ignore_errors=True)
        (state / "data" / "Aestra").mkdir(parents=True)
        (state / "data" / "Aestra" / "preferences.json").write_text(json.dumps({"ui": {"theme": self.args.theme}}))
        write_samples(state / "home")
        colors = {}
        if self.args.overrides:
            colors.update(json.load(open(self.args.overrides)).get("colors", {}))
        for item in self.args.set or []:
            key, _, value = item.partition("=")
            colors[key.strip()] = value.strip()
        overrides = self.out / "overrides.json"
        overrides.write_text(json.dumps({"colors": colors}, indent=2))
        if colors:
            self.env["AESTRA_THEME_OVERRIDES"] = str(overrides)
        cmd = [str(RUN), "--display", self.display]
        if self.args.bin:
            cmd.append(self.args.bin)
        subprocess.run(cmd, check=True, env=self.env, stdout=subprocess.DEVNULL)
        time.sleep(1.5)

    def stop(self):
        subprocess.run([str(STOP), "--display", self.display], env=self.env, stdout=subprocess.DEVNULL)

    # -- the shared set-ups, each done once per session --------------------------------------------------------
    def arsenal(self):
        self.drive("click", 582, 16, "wait", 1)
        if "units" not in self.flags:
            for i in range(3):  # "+ Add a sound" sits under the last row; the tray's Sampler tile is 65 px below it
                y = 330 + 48 * i
                self.drive("click", 340, y, "wait", 1, "click", 353, y + 65, "wait", 1)
            self.flags.add("units")

    def browser_open(self):
        if "browser" in self.flags:
            return
        self.drive("click", 51, 110, "wait", 0.5, "click", 65, 179, "wait", 1.5, "click", 67, 665, "wait", 1.5)
        self.flags.add("browser")

    def browser_expand(self):
        if "expanded" in self.flags:
            return
        self.browser_open()
        self.drive("click", 17, 266, "wait", 1, "click", 35, 298, "wait", 1)
        self.flags.add("expanded")

    def load_sample(self):
        if "sample" in self.flags:
            return
        self.arsenal()
        self.browser_expand()
        self.drive("drag", 100, 394, 900, 280, "wait", 1.5)
        self.flags.add("sample")

    def timeline(self):
        self.drive("click", 683, 16, "wait", 1)

    def place_clips(self):
        if "clips" in self.flags:
            return
        self.browser_expand()
        self.timeline()
        self.drive("click", 1046, 102, "wait", 0.8)  # the mixer panel would cover the lanes
        self.drive("drag", 100, 394, 560, 208, "wait", 1, "drag", 100, 330, 800, 208, "wait", 1)
        self.flags.add("clips")

    # -- scenes -------------------------------------------------------------------------------------------------
    def scene(self, name):
        d = self.drive
        if name == "arsenal-4rows":
            self.arsenal()
            self.shot(name)
        elif name == "context-menu":
            self.arsenal()
            d("rclick", 400, 285, "wait", 1, "move", 480, 390, "wait", 1.2, "shot", self.out / f"{name}.png")
            print(f"  {name}")
            d("key", "Escape", "click", 800, 640, "wait", 0.5)
        elif name == "arsenal-pianoroll":
            self.arsenal()
            d("click", 1063, 68, "wait", 1.5, "click", 543, 424, "wait", 1)
            self.shot(name)
            d("click", 1337, 107, "wait", 1)
        elif name == "file-browser":
            self.arsenal()
            self.browser_open()
            self.shot(name)  # the Aestra folder row is selected
            self.browser_expand()
            d("click", 100, 394, "wait", 1)
            self.shot(name + "-sample")
        elif name == "sample-editor":
            self.load_sample()
            d("dblclick", 353, 292, "wait", 2)
            self.shot(name)
            d("click", 1148, 192, "wait", 0.8)
        elif name == "mixer":
            self.timeline()
            d("wait", 1)
            self.shot(name)
        elif name == "timeline":
            self.place_clips()
            d("click", 600, 208, "wait", 0.8)
            self.shot(name)
        elif name == "clip-editor":
            self.place_clips()
            d("dblclick", 495, 212, "wait", 2)
            self.shot(name)
            d("click", 1148, 192, "wait", 0.8)
        elif name == "patterns":
            self.arsenal()
            d("click", 130, 110, "wait", 1)
            self.shot(name)
            d("click", 51, 110, "wait", 0.5)
        elif name == "audition":
            d("click", 783, 16, "wait", 1.2, "click", 70, 229, "wait", 1.5)
            self.shot(name)
            d("click", 582, 16, "wait", 1)
        elif name == "versions":
            self.arsenal()
            d("click", 30, 16, "click", 58, 228, "wait", 1.5)
            self.shot(name)
            d("click", 1278, 102, "wait", 0.8)
        elif name == "settings":
            d("click", 30, 16, "click", 62, 296, "wait", 1.5, "click", 246, 240, "wait", 1)
            self.shot(name)
            d("click", 1133, 93, "wait", 0.8)
        else:
            raise SystemExit(f"unknown scene {name!r}; see: theme_preview.sh list")


def scene_files(out: Path, label: str, scene: str):
    """A scene may produce more than one PNG (file-browser also saves file-browser-sample)."""
    folder = out / label
    return sorted(p for p in folder.glob("*.png") if p.stem == scene or p.stem.startswith(scene + "-"))


def cmd_run(args):
    out = Path(args.out).resolve() / args.label
    out.mkdir(parents=True, exist_ok=True)
    wanted = args.scenes.split(",") if args.scenes else SCENES
    order = [s for s in SCENES if s in wanted] + [s for s in wanted if s not in SCENES]
    session = Session(args, out)
    session.start()
    try:
        print(f"{args.label}: theme {args.theme}, overrides {out / 'overrides.json'}")
        for name in order:
            session.scene(name)
    finally:
        session.stop()
    return 0


def cmd_sheet(args):
    from PIL import Image, ImageDraw, ImageFont
    base = Path(args.out).resolve()
    labels = args.labels.split(",")
    try:
        font = ImageFont.truetype("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", 20)
    except OSError:
        font = ImageFont.load_default()
    wanted = args.scenes.split(",") if args.scenes else SCENES
    made = []
    for scene in wanted:
        names = sorted({p.stem for lab in labels for p in scene_files(base, lab, scene)})
        for stem in names:
            shots = [(lab, base / lab / f"{stem}.png") for lab in labels]
            shots = [(lab, p) for lab, p in shots if p.exists()]
            if not shots:
                continue
            imgs = [(lab, Image.open(p).convert("RGB")) for lab, p in shots]
            bar = 32
            sheet = Image.new("RGB", (sum(i.width for _, i in imgs) + 4 * (len(imgs) - 1), max(i.height for _, i in imgs) + bar), (60, 60, 60))
            draw = ImageDraw.Draw(sheet)
            x = 0
            for lab, im in imgs:
                draw.rectangle([x, 0, x + im.width, bar - 1], fill=(20, 20, 20))
                draw.text((x + 10, 5), f"{lab}   {stem}", fill=(245, 245, 245), font=font)
                sheet.paste(im, (x, bar))
                x += im.width + 4
            target = base / f"sheet-{stem}.png"
            sheet.save(target)
            made.append(target)
            print(target)
    return 0 if made else 1


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--label", default="preview")
    r.add_argument("--theme", default="ink-dark")
    r.add_argument("--set", action="append", help="token=#rrggbb[aa], repeatable")
    r.add_argument("--overrides", help="a theme JSON {\"colors\": {...}}")
    r.add_argument("--scenes", help="comma list; default all (see: list)")
    r.add_argument("--bin", help="path to the Aestra binary (default: the first build*/bin/Aestra)")
    r.add_argument("--out", default="theme-preview")
    r.add_argument("--display", default=":77")
    s = sub.add_parser("sheet")
    s.add_argument("--out", default="theme-preview")
    s.add_argument("--labels", required=True)
    s.add_argument("--scenes")
    sub.add_parser("list")
    args = ap.parse_args(argv)
    if args.cmd == "list":
        print("\n".join(SCENES))
        return 0
    return cmd_run(args) if args.cmd == "run" else cmd_sheet(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
