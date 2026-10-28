"""A stand-in for FL Studio's piano roll scripting module, for running generated scripts in tests.

It mirrors the documented API (flpianoroll.Note, flpianoroll.score, flpianoroll.ScriptDialog) and
rejects unknown note attributes so typos in generated scripts fail loudly.
"""

_NOTE_FIELDS = {
    "number": 60, "time": 0, "length": 0, "group": 0, "pan": 0.5, "velocity": 0.8,
    "release": 0.0, "color": 0, "fcut": 0.5, "fres": 0.5, "pitchofs": 0, "repeats": 0,
    "slide": False, "porta": False, "muted": False, "selected": False,
}


class Note:
    def __init__(self):
        for name, value in _NOTE_FIELDS.items():
            object.__setattr__(self, name, value)

    def __setattr__(self, name, value):
        if name not in _NOTE_FIELDS:
            raise AttributeError("flpianoroll.Note has no attribute '%s'" % name)
        object.__setattr__(self, name, value)

    def __repr__(self):
        return "Note(%s)" % ", ".join("%s=%r" % (k, getattr(self, k)) for k in _NOTE_FIELDS)


class _Score:
    PPQ = 96

    def __init__(self):
        self.notes = []
        self.markers = []
        self.length = 0

    @property
    def noteCount(self):
        return len(self.notes)

    def addNote(self, note):
        if not isinstance(note, Note):
            raise TypeError("addNote expects a flpianoroll.Note")
        self.notes.append(note)

    def clear(self, *args):
        self.notes = []


score = _Score()


class ScriptDialog:
    overrides = {}

    def __init__(self, title, description):
        self.title = title
        self.description = description
        self.values = {}
        self.options = {}

    def AddInputCombo(self, name, values, default):
        options = values.split(",")
        if not 0 <= default < len(options):
            raise ValueError("bad default for " + name)
        self.options[name] = options
        self.values[name] = ScriptDialog.overrides.get(name, default)

    def AddInputText(self, name, default):
        self.values[name] = ScriptDialog.overrides.get(name, default)

    def AddInputKnob(self, name, default, minimum, maximum):
        self.values[name] = ScriptDialog.overrides.get(name, default)

    def AddInputKnobInt(self, name, default, minimum, maximum):
        self.values[name] = ScriptDialog.overrides.get(name, default)

    def GetInputValue(self, name):
        return self.values[name]
