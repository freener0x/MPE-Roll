"""Runs a generated FL Studio piano roll script against the mock flpianoroll module and checks the result.

Usage: run_script.py path/to/script.pyscript   (expects path/to/script.expect.json next to it)
"""

import json
import os
import runpy
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import flpianoroll  # noqa: E402  (the mock)


def run(path, overrides, existing=()):
    flpianoroll.score.clear()
    for note in existing:
        flpianoroll.score.addNote(note)
    flpianoroll.ScriptDialog.overrides = overrides
    namespace = runpy.run_path(path)
    form = namespace["createDialog"]()
    namespace["apply"](form)
    return list(flpianoroll.score.notes), namespace


def check_ranges(note):
    assert 0 <= note.number <= 127, note
    assert note.time >= 0, note
    assert note.length >= 1, note
    assert 0 <= note.color <= 15, note
    assert -120 <= note.pitchofs <= 120, note
    for field in ("velocity", "release", "pan", "fcut", "fres"):
        value = getattr(note, field)
        assert 0.0 <= value <= 1.0, (field, note)
    assert isinstance(note.slide, bool), note


def pitch_of(note, namespace):
    return note.number + note.pitchofs / float(namespace["PITCHOFS_PER_SEMITONE"])


def main():
    path = sys.argv[1]
    with open(os.path.splitext(path)[0] + ".expect.json") as f:
        expected = json.load(f)

    # Defaults: replace, slide notes, pressure -> Mod X, timbre -> Mod Y, ramps on, grouping on.
    notes, ns = run(path, {})
    plain = [n for n in notes if not n.slide]
    slides = [n for n in notes if n.slide]
    for n in notes:
        check_ranges(n)
    assert len(plain) == expected["notes"], (len(plain), expected)
    assert len(slides) == expected["slides"], (len(slides), expected)

    groups = {n.group for n in plain if n.group}
    for s in slides:
        assert s.group in groups, s
        owner = [n for n in plain if n.group == s.group][0]
        assert s.color == owner.color, (s, owner)
        assert s.time >= owner.time and s.time < owner.time + owner.length, (s, owner)

    # The glide on key 60 ends three semitones up.
    glide = [n for n in plain if n.number == 60][0]
    glide_slides = sorted((s for s in slides if s.group == glide.group), key=lambda s: s.time)
    assert abs(pitch_of(glide_slides[-1], ns) - 63.0) < 0.11, glide_slides[-1]

    # The microtonal note keeps its offset as fine pitch.
    micro = [n for n in plain if abs(pitch_of(n, ns) - 66.7) < 0.06]
    assert len(micro) == 1, [pitch_of(n, ns) for n in plain]

    # The scoop starts beyond fine pitch range, so the note itself moves down to start there.
    scoop = [n for n in plain if n.time == 5 * flpianoroll.score.PPQ][0]
    assert abs(pitch_of(scoop, ns) - 34.0) < 0.11, scoop

    # Without Mod ramps only the pitch-moving slides remain.
    notes, ns = run(path, {"Mod ramps on slide notes": 1})
    assert len([n for n in notes if n.slide]) == expected["moving_slides"]

    # Glides off: no slides, no fine pitch.
    notes, ns = run(path, {"Pitch glides": 2})
    assert not any(n.slide for n in notes)
    assert all(n.pitchofs == 0 for n in notes)

    # Keep existing notes.
    existing = flpianoroll.Note()
    existing.number = 30
    existing.length = 96
    notes, ns = run(path, {"Existing notes": 1}, existing=[existing])
    assert notes[0] is existing

    # Pressure as velocity.
    notes, ns = run(path, {"Pressure": 2})
    assert abs([n for n in notes if n.number == 60 and not n.slide][0].velocity - 1.0) < 1e-6

    print("FL script OK: %d notes, %d slides" % (expected["notes"], expected["slides"]))


if __name__ == "__main__":
    main()
