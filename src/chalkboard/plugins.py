"""Plugins: small Python files that add to Chalkboard without changing it.

Put a .py file in the plugins folder inside Chalkboard's data folder (Settings shows where the
data file is). When Chalkboard starts, it runs each file's setup(chalkboard) once:

    def setup(chalkboard):
        chalkboard.add_export("MD", "Markdown (.md)", ".md", write_markdown)
        chalkboard.add_mascot("owls2", "NIGHT OWLS", rows)       # 16 rows of 16 pixels: . d n h
        chalkboard.after_export(lambda paths: print(paths))     # e.g. copy them to a sync folder

A plugin that fails is switched off with a note, and Chalkboard carries on; an export format whose
plugin fails says so instead of crashing. `--no-plugins` (or CHALKBOARD_NO_PLUGINS=1) starts
without any. Rename a file to end in .off to turn it off.

Plugins are your own code, run with your permissions: Chalkboard itself never goes online, but a
plugin can do anything Python can, so only add ones you trust. examples/plugins has one to copy.
"""

import importlib.util
import os

from . import __version__, exporting, mascots

API_VERSION = 1
problems = []        # notes for the teacher: plugins that didn't load, hooks that failed
_after = []          # after_export functions
_loaded = False


class Chalkboard:
    """What setup() gets. Registrations only take effect if setup() finishes without an error."""
    version = __version__
    api = API_VERSION

    def __init__(self, data_dir, name):
        self.data_dir = data_dir
        self.name = name
        self._todo = []

    def add_export(self, key, label, ext, render, kinds=("lesson", "assessment")):
        """A new export format. render(doc, path, item) writes one file: doc is the same document the
        PDF and Word exports draw (see doc.py), item the lesson or assessment itself."""
        key = str(key).upper()
        if key in exporting.FORMATS or not callable(render) or not str(ext).startswith("."):
            raise ValueError(f"add_export: {key} is taken, or render/ext isn't right")
        self._todo.append(lambda: _add_export(key, str(label), str(ext), render, kinds, self.name))

    def add_mascot(self, key, name, rows):
        if not mascots.valid(rows):
            raise ValueError(f"add_mascot: {key} needs 16 rows of 16 (or 8) pixels in . d n h, row 0 blank")
        self._todo.append(lambda: mascots.MASCOTS.__setitem__(str(key), (str(name).upper(), list(rows))))

    def after_export(self, fn):
        """fn(paths) runs after every export with the files just written."""
        if not callable(fn):
            raise ValueError("after_export needs a function")
        self._todo.append(lambda: _after.append((self.name, fn)))


def _add_export(key, label, ext, render, kinds, plugin):
    def write(doc, path, item=None, **_):
        try:
            render(doc, path, item)
        except Exception as e:  # noqa: BLE001 - a plugin's bug is the plugin's, not a crash
            raise exporting.ExportError(f"THE {label.upper()} PLUGIN ({plugin}) FAILED: {e}") from e
        return path
    exporting.FORMATS[key] = (ext, write)
    exporting.PLUGIN_FORMATS[key] = label
    for kind in kinds:
        order = exporting.FORMAT_ORDER.get(kind)
        if order is not None and key not in order:
            order.insert(len(order) - 1, key)  # before ALL (which stays the built-in formats)


def load(data_dir):
    """Run every plugin in <data_dir>/plugins once per process. Returns the problems so far."""
    global _loaded
    if _loaded or os.environ.get("CHALKBOARD_NO_PLUGINS"):
        return problems
    _loaded = True
    folder = os.path.join(data_dir, "plugins")
    try:
        names = sorted(n for n in os.listdir(folder) if n.endswith(".py") and not n.startswith(("_", ".")))
    except OSError:
        return problems  # no plugins folder: nothing to do
    for n in names:
        api = Chalkboard(data_dir, n)
        try:
            spec = importlib.util.spec_from_file_location(f"chalkboard_plugin_{n[:-3]}", os.path.join(folder, n))
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            module.setup(api)
            for step in api._todo:
                step()
        except Exception as e:  # noqa: BLE001 - one broken plugin never stops Chalkboard
            problems.append(f"PLUGIN {n} WAS SKIPPED: {e}")
    return problems


def after_export(paths):
    for name, fn in _after:
        try:
            fn(list(paths))
        except Exception as e:  # noqa: BLE001
            problems.append(f"PLUGIN {name} (AFTER EXPORT) FAILED: {e}")


def take_problems():
    out = problems[:]
    problems.clear()
    return out
