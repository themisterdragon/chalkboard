# Porting and extending Chalkboard

Chalkboard is meant to be small and easy to take apart. You can rebuild it for another system
(a phone, a tablet, the web) or add to it for your own workflow. This page maps the pieces.

Two promises go with any port or plugin:

- **Offline.** Chalkboard never goes online. `chalkboard/offline.py` enforces this while the app
  runs, and `scripts/check_offline.py` tests it. A port should keep that promise.
- **The teacher's own voice.** Chalkboard lays out what the teacher writes. It doesn't generate
  lessons.

## The layers

```
 front ends     app.py + ui.py ............ the terminal app (curses)
                gui/ ...................... the window app (Tk); gui/term.py runs the terminal
                                            app inside the window
 ─────────────────────────────────────────────────────────────────────────────
 core           store.py .................. the data: load/save, settings, standards, backups
 (no UI code,   doc.py .................... lessons and assessments -> documents (blocks)
  standard      export_pdf / _docx / _txt   documents -> files
  library only) export_png / _pptx ........ board slides (PNG, editable .pptx)
                exporting.py .............. picks formats and folders; Export Everything
                markup.py ................. **bold** *italic* __underline__, web links
                organizers.py ............. graphic-organizer shapes
                images.py, fontmetrics.py . logo reading, text measuring
                plugins.py, offline.py .... plugins, and the offline guard
 data           data/*.json ............... standards, SEL prompts, mascot pixel art
```

None of the core modules imports a front end, curses, or Tk. A new front end (Kivy, BeeWare,
Pyodide in a browser, or native code that calls Python) needs only the core.

## The data file

Everything a teacher makes lives in one JSON file (`data.json` in the data folder; Settings shows
where). A port in another language can read and write it directly:

```
{ "version": 1,
  "settings":    { "teacher", "school", "course", "font", "page", "export_dir", ... },
  "lessons":     [ { "id", "title", "unit", "course", "date", "question", "targets", "success",
                     "vocab", "materials", "bell_ringer", ..., "standards": [codes],
                     "assessments": [ids], "created", "updated" } ],
  "assessments": [ { "id", "title", "kind", "unit", "course", "standards", "instructions",
                     "questions": [ { "type", "prompt", "points", "choices", "answer", ... } ] } ],
  "custom_standards": [ ... ] }
```

Text fields are plain text with Markdown-style marks (`**bold**`, `*italic*`, `__underline__`)
and `- ` bullets. Question types: `mc tf short essay fill match chart passage section`.
Backups (`chalkboard-backup-*.json`) wrap the same data with the teacher's imported standards.

## Documents

`doc.py` turns a lesson or assessment into a document: `{"title": ..., "blocks": [...]}`. Each
block is a dict with a type `t`: `title subtitle h1 p bullet kv q choice answer match passage
lines check fields ...`. Every exporter draws these same blocks, so a new output format, on any
platform, only has to draw blocks. `board_doc()` is the board-slide version: panels sorted into
columns.

## Things a port has to replace

- **Board PNGs.** `export_png.py` lays the slide out as a PDF, then turns it into a picture with a
  tool the system has: `sips` (Mac), Windows' built-in PDF renderer, or Poppler/Ghostscript
  (Linux). A phone port would use its own PDF renderer (Android `PdfRenderer`, iOS `PDFKit`).
  The editable `.pptx` needs no outside tool.
- **Opening files.** `exporting.open_path` uses `open` / `xdg-open` / `os.startfile`. On a
  phone, use the system share sheet.
- **Fonts.** PDFs use the standard Times and Helvetica fonts that PDF viewers include, so no
  font files are needed.

## Plugins

A plugin is one Python file in the `plugins` folder inside the data folder. It can add an export
format, add a mascot, or run something after each export. See `chalkboard/plugins.py` and
`examples/plugins/markdown_export.py`. A plugin that breaks gets switched off with a note, and
the offline guard covers plugins too. To get files into Google Drive, OneDrive, or Dropbox, a
plugin saves them into the folder that app syncs.

## Checks to run after a change

```
python3 scripts/check_offline.py     # offline promise, including the guard
python3 scripts/check_contrast.py    # every window-app look meets WCAG 2.1 AA
python3 scripts/check_py38.py        # nothing newer than the oldest supported Python
```
