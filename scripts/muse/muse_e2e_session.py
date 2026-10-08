#!/usr/bin/env python3
"""Generate the Muse end-to-end session corpus and its sample audio.

One corpus, two consumers:

  * ``muse_e2e_run.py``, which drives the real MuseRepl binary over it and
    diffs the answers, and
  * ``MuseE2ESessionTest`` in Tests/Commands, which asserts the same shapes
    in-process so a regression is caught by ctest rather than by a human
    remembering to run a script.

The session is a request a person would actually make — build a 142 BPM drum
loop, then bounce it — rather than a verb-by-verb sweep, because the failures
worth catching live in the transitions: a lane that must exist before a clip, a
batch that must undo as one step, a render that must not be silent. Calls that
are meant to fail are labelled, so a reader can tell an expected rejection from
a bug.

Addressing rules this corpus has to respect, all of them learned by getting them
wrong first:

  * Tracks are 0-based indexes. list_tracks also prints an ``id`` of 1..N, and
    that number addresses a DIFFERENT track, so nothing here echoes it back.
  * ``unit``, ``pattern`` and ``bar`` are 1-based; their schema minimum is 1 and
    a 0 is rejected.
  * ``add_lane`` creates the lane for the NEXT track index, so after one
    add_lane the clip belongs on track 0.
  * ``batch`` takes ``commands``, not ``ops``.
  * ``list_samples`` requires ``dir``.
  * An effect ``param`` is a case-insensitive name or the numeric id
    get_effects prints, and ``value`` is normalised 0..1, not musician units.
"""

import argparse
import json
import math
import os
import struct
import wave


def write_sample_wav(path, seconds=2.0, rate=48000, freq=110.0):
    """A deterministic mono tone. Not music, but real audio the sampler loads."""
    frames = bytearray()
    for n in range(int(seconds * rate)):
        t = n / rate
        env = min(1.0, t * 40.0) * math.exp(-2.0 * t)
        v = int(max(-1.0, min(1.0, env * math.sin(2.0 * math.pi * freq * t))) * 32767)
        frames += struct.pack("<h", v)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))
    return path


def build_session(sample, out):
    """(expectation, request) pairs. expectation is 'ok', 'err' or 'any'."""
    s = []
    seq = [0]

    def add(exp, verb, args=None):
        seq[0] += 1
        req = {"id": seq[0], "verb": verb}
        if args is not None:
            req["args"] = args
        s.append((exp, req))
        return req["id"]

    def mark(want, rid):
        for i, (_, r) in enumerate(s):
            if r["id"] == rid:
                s[i] = (want, r)
                return
        raise KeyError(rid)

    # --- orient ------------------------------------------------------------
    add("ok", "get_schema")
    add("ok", "get_capabilities")
    add("ok", "get_transport")
    add("ok", "get_session_state")

    # --- session lifecycle -------------------------------------------------
    # Headless Muse has no project file verbs: ProjectSerializer lives in
    # Source/, which AestraAudio cannot reach, so these are application host
    # verbs. Asserting they are UNKNOWN here is what keeps Slice B honest — when
    # project.save lands in the host it still will not appear in a headless
    # process, and this line is what says so.
    for v in ("project_save", "project_open", "project_new"):
        add("err", v)

    # --- transport ---------------------------------------------------------
    add("ok", "set_bpm", {"value": 142})
    add("err", "set_bpm", {"value": 0})
    add("err", "set_bpm", {"value": "fast"})
    add("ok", "get_transport")

    # --- tracks ------------------------------------------------------------
    add("ok", "add_track", {"name": "Drums"})
    add("ok", "add_track", {"name": "Bass"})
    add("ok", "add_track", {"name": "Keys"})
    add("ok", "list_tracks")
    add("ok", "rename_track", {"track": 2, "name": "Rhodes"})
    add("ok", "set_volume", {"track": 0, "value": 0.85})
    add("err", "set_volume", {"track": 0, "value": 1.5})
    add("ok", "set_pan", {"track": 1, "value": -0.25})
    add("ok", "mute_track", {"track": 1, "state": True})
    add("ok", "solo_track", {"track": 0, "state": True})
    add("ok", "list_tracks")

    # --- units and samples -------------------------------------------------
    add("ok", "add_unit", {"name": "Kick"})
    add("ok", "add_unit", {"name": "Hat"})
    add("ok", "list_units")
    add("ok", "load_sample", {"unit": 1, "file": sample})
    add("ok", "set_unit_gain", {"unit": 2, "value": 0.6})
    add("err", "load_sample", {"unit": 99, "file": sample})

    # --- patterns and notes ------------------------------------------------
    add("ok", "list_patterns")
    add("ok", "get_pattern", {"pattern": 1})
    add("ok", "add_note", {"pattern": 1, "unit": 1, "pitch": 36,
                           "start": 0.0, "duration": 0.25, "velocity": 1.0})
    add("ok", "add_note", {"pattern": 1, "unit": 1, "pitch": 36,
                           "start": 1.0, "duration": 0.25, "velocity": 0.9})
    add("ok", "set_steps", {"pattern": 1, "unit": 1, "pitch": 36,
                            "steps": "x...x...x...x..."})
    add("ok", "set_pattern_length", {"pattern": 1, "beats": 4})
    add("ok", "transpose_pattern", {"pattern": 1, "semitones": 12})
    add("ok", "quantize_pattern", {"pattern": 1, "grid": 0.25})
    add("ok", "get_pattern", {"pattern": 1})
    add("ok", "clone_pattern", {"pattern": 1})
    add("ok", "list_patterns")
    add("err", "transpose_pattern", {"pattern": 999, "semitones": 1})

    # --- audio clips -------------------------------------------------------
    add("ok", "add_lane", {"name": "Audio 1"})
    add("ok", "add_clip", {"track": 0, "file": sample, "bar": 1})
    add("ok", "list_clips")
    add("err", "add_clip", {"track": 0, "file": "/nonexistent/nope.wav", "bar": 1})
    add("err", "add_clip", {"track": 2, "file": sample, "bar": 1})  # no lane

    # --- effects -----------------------------------------------------------
    add("ok", "list_plugins")
    add("ok", "get_effects", {"track": 0})
    add("ok", "add_effect", {"track": 0, "effect": "com.Aestrastudios.eq"})
    add("ok", "get_effects", {"track": 0})
    add("ok", "set_effect_param", {"track": 0, "slot": 0,
                                   "param": "Low Shelf Gain", "value": 0.5})
    add("ok", "set_effect_param", {"track": 0, "slot": 0, "param": "5", "value": 0.4})
    add("err", "set_effect_param", {"track": 0, "slot": 0, "param": "gain", "value": 0.5})
    add("err", "set_effect_param", {"track": 0, "slot": 0,
                                    "param": "Low Shelf Gain", "value": 3.0})
    add("ok", "bypass_effect", {"track": 0, "slot": 0, "state": True})
    add("ok", "remove_effect", {"track": 0, "slot": 0})

    # --- diagnostics -------------------------------------------------------
    add("ok", "get_audio_health")
    add("ok", "get_routing_graph")
    add("ok", "get_latency_report")
    add("ok", "get_meters")
    add("ok", "list_samples", {"dir": out})
    add("ok", "get_project_load_report")

    # --- render and confirm ------------------------------------------------
    # Rendered while the clip is live, so peakDb is a real number. A golden that
    # only ever renders silence would not notice level measurement dying.
    add("ok", "render_song", {"file": os.path.join(out, "song.wav")})
    # Track 0 is soloed at this point, so the render is silent and the verdict is
    # "fail". Status is still ok: the verb ran, and it reported a failure. The
    # verdict semantics are pinned by scripts/muse/muse_verify_probe.py; this
    # line only proves the verb is reachable and does not break its neighbours.
    add("ok", "verify_render", {"file": os.path.join(out, "verified.wav")})
    add("err", "verify_render", {"file": os.path.join(out, "x.wav"), "nonsense": 1})
    add("err", "render_song", {"file": "/nonexistent/dir/x.wav"})

    # --- batch, history ----------------------------------------------------
    add("ok", "batch", {"commands": [
        {"verb": "set_volume", "args": {"track": 2, "value": 0.7}},
        {"verb": "set_pan", "args": {"track": 2, "value": 0.1}},
    ]})
    add("ok", "undo")
    add("ok", "list_tracks")
    add("ok", "redo")
    add("ok", "undo")

    # Silence now is the correct answer: the undo above removed the clip. The
    # corpus therefore holds a loud render AND a silent one.
    add("ok", "render_song", {"file": os.path.join(out, "song_after_undo.wav")})

    # Put it back and render loud again. Track 0 is still soloed, so this also
    # pins the soloed-render behaviour — currently digital silence. Recorded
    # rather than worked around: a golden that hid it would stop proving
    # anything the moment somebody fixed it.
    add("ok", "add_lane", {"name": "Audio 2"})
    add("ok", "add_clip", {"track": 0, "file": sample, "bar": 1})
    solo_off = add("ok", "solo_track", {"track": 0, "state": False})
    assert solo_off
    add("ok", "render_song", {"file": os.path.join(out, "song_restored.wav")})
    # Loud again, and byte-identical to song_restored.wav: the strongest
    # confirmation the surface offers, that the render path is deterministic.
    add("ok", "verify_render", {"file": os.path.join(out, "verified_again.wav"),
                                "against": os.path.join(out, "song_restored.wav")})

    # --- protocol honesty --------------------------------------------------
    add("err", "no_such_verb")
    add("err", "get_transport", {"unexpected": 1})
    add("err", "list_tracks", {"anything": True})

    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    args = ap.parse_args()
    out = os.path.abspath(args.outdir)
    os.makedirs(out, exist_ok=True)

    sample = write_sample_wav(os.path.join(out, "sample.wav"))
    session = build_session(sample, out)

    with open(os.path.join(out, "session.jsonl"), "w", encoding="utf-8") as f:
        for _, req in session:
            f.write(json.dumps(req) + "\n")
    with open(os.path.join(out, "session.expect"), "w", encoding="utf-8") as f:
        for exp, req in session:
            f.write(f"{req['id']}\t{exp}\t{req['verb']}\n")

    verbs = {r["verb"] for _, r in session}
    print(f"{len(session)} requests, {len(verbs)} distinct verbs -> {out}/session.jsonl")
    print(f"expect ok={sum(1 for e, _ in session if e == 'ok')} "
          f"err={sum(1 for e, _ in session if e == 'err')}")


if __name__ == "__main__":
    main()
