#!/usr/bin/env python3
"""Drive the test Aestra on a private X display (tools/ui/run.sh) with real pointer and key events (XTEST).

    tools/ui/drive.py [--display :78] COMMAND ARGS ... [COMMAND ARGS ...]

Commands run in order:
    click X Y            press and release the left button at X Y
    dblclick X Y         a real double-click (30 ms press, 60 ms gap)
    move X Y             move the pointer
    drag X0 Y0 X1 Y1     press at X0 Y0, move there in small steps, release (what drag and drop needs)
    hold X0 Y0 X1 Y1     like drag but keep the button down (finish with: release)
    release              let go of the left button
    key NAME             a key by X keysym name: Return, Escape, Left, a, F7 ...
    type TEXT            type plain text
    wait SECONDS         pause
    shot FILE.png        screenshot of the whole display
    raise TITLE          bring a window (a zenity dialog, say) to the front and focus it
    waitwindow SECONDS   wait until an Aestra window is mapped
    fit                  size the Aestra window to the whole display

Needs python-xlib (and Pillow for shot).
"""
import sys
import time

from Xlib import X, XK, display as xdisplay
from Xlib.ext import xtest


def main(argv):
    disp = ":78"
    if len(argv) >= 2 and argv[0] == "--display":
        disp, argv = argv[1], argv[2:]
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    d = xdisplay.Display(disp)
    root = d.screen().root

    def move(x, y):
        xtest.fake_input(d, X.MotionNotify, x=int(x), y=int(y))
        d.sync()

    def button(down):
        xtest.fake_input(d, X.ButtonPress if down else X.ButtonRelease, 1)
        d.sync()

    def glide(x0, y0, x1, y1, steps=30):
        for i in range(1, steps + 1):
            move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps)
            time.sleep(0.02)

    def key(name):
        sym = XK.string_to_keysym(name)
        if sym == 0 and len(name) == 1:
            sym = ord(name)
        code = d.keysym_to_keycode(sym)
        shifted = len(name) == 1 and (name.isupper() or name in '~!@#$%^&*()_+{}|:"<>?')
        shift = d.keysym_to_keycode(XK.string_to_keysym("Shift_L"))
        if shifted:
            xtest.fake_input(d, X.KeyPress, shift)
        xtest.fake_input(d, X.KeyPress, code)
        d.sync()
        time.sleep(0.02)
        xtest.fake_input(d, X.KeyRelease, code)
        if shifted:
            xtest.fake_input(d, X.KeyRelease, shift)
        d.sync()

    def windows():
        for w in root.query_tree().children:
            try:
                yield w, (w.get_wm_name() or ""), w.get_geometry(), w.get_attributes().map_state
            except Exception:
                continue

    def aestra_window():
        for w, name, geo, mapped in windows():
            if mapped == X.IsViewable and geo.width > 200 and "Aestra" in name:
                return w
        return None

    i = 0
    while i < len(argv):
        cmd = argv[i]
        a = argv[i + 1:]
        if cmd == "click":
            move(a[0], a[1]); time.sleep(0.1); button(True); time.sleep(0.05); button(False); i += 3
        elif cmd == "dblclick":
            move(a[0], a[1]); time.sleep(0.1)
            for _ in range(2):
                button(True); time.sleep(0.03); button(False); time.sleep(0.06)
            i += 3
        elif cmd == "move":
            move(a[0], a[1]); i += 3
        elif cmd in ("drag", "hold"):
            x0, y0, x1, y1 = map(int, a[:4])
            move(x0, y0); time.sleep(0.15); button(True); time.sleep(0.1)
            glide(x0, y0, x1, y1); time.sleep(0.3)
            if cmd == "drag":
                button(False); time.sleep(0.2)
            i += 5
        elif cmd == "release":
            button(False); i += 1
        elif cmd == "key":
            key(a[0]); i += 2
        elif cmd == "type":
            for ch in a[0]:
                key({" ": "space", ".": "period", "/": "slash", "-": "minus", "_": "underscore"}.get(ch, ch))
            i += 2
        elif cmd == "wait":
            time.sleep(float(a[0])); i += 2
        elif cmd == "shot":
            from PIL import Image
            g = root.get_geometry()
            raw = root.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xFFFFFFFF)
            Image.frombytes("RGB", (g.width, g.height), raw.data, "raw", "BGRX").save(a[0])
            i += 2
        elif cmd == "raise":
            for w, name, _, _ in windows():
                if name == a[0]:
                    w.configure(stack_mode=X.Above)
                    w.set_input_focus(X.RevertToParent, X.CurrentTime)
                    d.sync()
            i += 2
        elif cmd == "waitwindow":
            deadline = time.time() + float(a[0])
            while aestra_window() is None and time.time() < deadline:
                time.sleep(0.25)
            if aestra_window() is None:
                print("drive.py: no Aestra window yet", file=sys.stderr)
                return 1
            time.sleep(1.0)
            i += 2
        elif cmd == "fit":
            w = aestra_window()
            if w is not None:
                g = root.get_geometry()
                w.configure(x=0, y=0, width=g.width, height=g.height)
                d.sync()
            i += 1
        else:
            print(f"drive.py: unknown command {cmd!r}", file=sys.stderr)
            return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
