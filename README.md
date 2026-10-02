# Chalkboard

A retro green-screen lesson planner and assessment builder for the terminal,
plus a windowed version (`chalkboard-gui`) with an old-desktop look. Works
completely offline: no accounts, no API calls, no internet needed.

- **Lesson plans**: title, unit, dates, standards, learning targets, success
  criteria, materials, bell ringer, I Do / We Do / You Do, closure,
  differentiation, checks for understanding, homework, notes
- **SEL bell ringers**: no warm-up planned? Press `G` for a random
  social-emotional learning prompt (137 built in). Chalkboard avoids prompts your
  other lessons already use.
- **Assessments & assignments**: quizzes, tests, worksheets, exit tickets, homework
  - multiple choice, true/false, short answer, extended response (lined,
    blank, or boxed space), fill in the blank, matching, reading passages
    with line numbers, section headers
  - per-question points and standards, automatic totals
  - answer keys with circled answers and a quick key
  - up to 4 shuffled versions (A = original order). "All of the above" style
    choices stay put, and passages/sections act as anchors.
- **Annotation sheets**: Name / Date, your heading (e.g. "Hamlet 4.1 Annotation"),
  then a Line / Symbol / Reason for Annotating chart (10 rows) filling one
  page. The .docx is the digital copy; its rows grow as students type.
- **Bell ringer sheets**: two pages to print double-sided, Monday-Friday boxes
  for one week per side. Leave the prompts blank or fill any day (SEL prompts
  work here too).
- **Worksheets inside a lesson**: the lesson's "Assessments & Worksheets"
  field builds new quizzes and sheets (they pick up the lesson's unit, course,
  and standards) or links ones you already made. Export All puts them in the
  lesson's folder, and the make-up sheet lists them under What You'll Need.
- **Default materials**: set what you always need under Settings > Default
  Materials Needed, and every new lesson starts with it in Materials & Texts.
- **Finding things**: filter lesson and assessment lists by unit (and
  assessments by type), search, and sort by date modified, date created,
  title, or unit.
- **Standards**: import your state's, district's, or school's standards from
  a CSV or JSON file (see [Standards files](#standards-files)), or type your
  own in. Filter by subject and grade, search, browse, and attach them to
  lessons, assessments, and individual questions.
- **Export**: PDF, Word .docx (also opens in Google Docs, Pages, LibreOffice),
  and plain text for pasting into Google Classroom or an LMS
- **Make-up sheets**: a student-facing PDF (plus an editable .docx) for anyone who missed class, built
  from the lesson: goals, success criteria, materials, then a checklist of
  steps (bell ringer and exit ticket get writing lines) with Name / Date
  Missed / Due and a Turned In / Teacher Initials line. Teacher-only fields
  (differentiation, checks, notes) are left off.
- **Board slides**: a 1920x1080 PNG of a lesson's standards, "I can"
  statements, success criteria, bell ringer, materials, and homework for an
  interactive display, projector, or TV. Text sizes itself to fit; empty sections are
  left off. Chalkboard (dark green), whiteboard (white), or your school colors
  (HEX codes under Settings for background, headings, and text).

## Install

Download from the
[latest release](https://github.com/themisterdragon/chalkboard/releases/latest).

Everything except the Windows `.exe` needs Python 3.8+ (Macs offer to install it
the first time).

**Mac (double-click app):** open `Chalkboard-<version>.dmg` and drag
Chalkboard to Applications (or unzip `Chalkboard-macOS.zip` and do the same).
It runs in a Terminal window. `Read Me First.txt` covers the first-launch
prompts (Gatekeeper, Python install).

**Windows:** download `Chalkboard-Window-<version>-windows.exe` for the window
version, or `Chalkboard-<version>-windows.exe` for the terminal app (it looks best
in Windows Terminal). Python is built in, so there's nothing else to install.
If "Windows protected your PC" appears, click More info > Run anyway.

**Linux (or macOS from the terminal):** unpack `chalkboard-<version>-linux.tar.gz`
(it holds `chalkboard.pyz`, `chalkboard-gui.pyz`, and `install.sh`) and run `sh install.sh`
(installs `chalkboard` and `chalkboard-gui` to `~/.local/bin`, plus an app-menu entry for the
window version), or run either directly with `python3 chalkboard.pyz`.

**Python package (any OS, including Windows):**

    pipx install chalkboard_planner-1.8.0-py3-none-any.whl

On Windows this pulls in `windows-curses` automatically.

## Window version

`chalkboard-gui` is the whole planner in a window, with a mouse, menus, and
buttons, in one of two old-desktop looks (View menu or Settings):

- **Bevel**: gray 3-D buttons, dark blue title bars, a teal desktop
- **Pinstripe**: black-and-white, striped title bars, a dotted gray desktop

Everything the terminal app does is here: lesson plans (with standards,
SEL bell ringers, and linked worksheets), quizzes and tests with
every question type, annotation and bell ringer sheets, the standards
library with import, settings, preview, and every export. It reads and writes
the same data file as the terminal app, and picks up changes the other one
saves. Close a window with its close box (top left) or Esc to go back.
Text size and overall size are in Settings, and sharp screens get 2× on
their own.

    chalkboard-gui                    # or: python3 chalkboard-gui.pyz

It needs Tk, which Python from python.org includes on Windows and macOS. On
Linux install it first: `sudo pacman -S tk` (Arch) or
`sudo apt install python3-tk` (Debian/Ubuntu). `install.sh` installs it next
to `chalkboard` and, on Linux, adds a "Chalkboard (Window)" app-menu entry.

## Use

    chalkboard            # with boot sequence
    chalkboard --no-boot

Everything autosaves. Number keys or arrows + Return pick items. Esc goes back.

| Where | Keys |
|-------|------|
| Lists | `N` new, `R` rename, `C` copy, `D` delete, `X` export, `P` preview, `/` search, `U` unit filter, `T` type filter (assessments), `O` sort order |
| Lesson | `B` board slide (PNG) and `M` make-up sheet (PDF + DOCX) in one keystroke, `G` random SEL bell ringer (press again for another), `H` annotation sheet as homework |
| Lesson > Assessments & Worksheets | `N` build new, `L` link existing, `E` rename, `R` remove from lesson, `+`/`-` reorder |
| Assessment | `A` add question, `S` settings (title, directions, standards), `+`/`-` move, `M` move to position, `K` preview answer key |
| Standards library | `I` import a CSV/JSON file, `X` remove an imported subject, `A` add one by hand, `D` delete one you added |
| Standards picker | number/Space toggle, `V` view full text, `F` subject, `G` grade, `/` search, `S` show selected (the library has `F`, `G`, and `/` too) |
| Yes/no questions | Return or `Y` means yes; `N` or Esc means no |
| Text editor | type freely, `- ` starts a bullet, Esc saves, Ctrl-X cancels; in Bell Ringer, Ctrl-G adds a random SEL prompt |

Fill-in-the-blank: type `___` (three or more underscores) wherever a blank goes.

## Where things live

| What | Where |
|------|-------|
| Your data | `~/.local/share/chalkboard/data.json` (macOS: `~/Library/Application Support/chalkboard`, Windows: `%APPDATA%\chalkboard`). Copy this file to move your work to another computer. A `.bak` copy is kept automatically. |
| Imported standards | a `standards` folder next to `data.json`, one JSON file per subject |
| Exports | `~/Documents/Chalkboard` (changeable in Settings). Every export is filed by class, then unit, then lesson, like `English 10/Unit 3/The Raven/`; assessments go in their unit folder. A blank course or unit is skipped. Export All also puts a lesson's linked worksheets in its folder. |

Board slides are drawn with the same engine as the PDF, then turned into a PNG
by a tool the computer already has: `sips` on macOS (built in), or poppler's
`pdftoppm`, `mutool`, or Ghostscript on Linux/Windows. Pick which sections
appear under Settings > Board Slide (or from the export screen).

## Standards files

Chalkboard doesn't come with any standards. Standards documents are usually
copyrighted by the state or organization that wrote them, so each teacher
brings their own. In the Standards Library, press `I` and type (or drag in) the
path to a `.csv` or `.json` file. Importing a subject again replaces it, and
`X` removes one. Lessons keep the codes they already use.

**CSV** (save from Excel, Numbers, or Google Sheets): the first row holds column
names, in any order. `code` and `text` are required.

| Column | Meaning |
|--------|---------|
| `subject` | Groups standards for the `F` filter. Blank means the file's name. |
| `code` | Unique code, like `RL.9-10.1` |
| `text` | The standard's full text |
| `grades` | `K`, `5`, `9-10`, `K-12`... (used by the `G` filter; anything else always shows) |
| `strand`, `cluster` | Optional headings shown when you view a standard |
| `part_of` | For a lettered or numbered part: its parent's code. The part's code must be the parent's plus a letter (`RL.9-10.1a`) or `.` and a label (`MTH.A.1.1`). |

[`examples/sample-standards.csv`](examples/sample-standards.csv) is a small
made-up example to try.

**JSON**: one subject, or a list of them:

```json
{
  "subject": "English",
  "source": "Where these came from (shown under each standard)",
  "standards": [
    {"code": "ENG.R.1", "grades": "9-10", "strand": "Reading", "cluster": "Evidence",
     "text": "Use details from a text to support what it says.",
     "subs": [["a", "Quote the details that matter most."]]}
  ]
}
```

Make sure you're allowed to use the standards you import. Many states let
their own teachers copy their standards for classroom use. Chalkboard keeps
them on your computer and never uploads them anywhere.

## License

Chalkboard is free software under the [GNU General Public License v3.0](LICENSE).
You can use, study, share, and change it. If you distribute a changed version,
you have to share its source under the same license.

Chalkboard isn't affiliated with or endorsed by any state department of
education, standards organization, or hardware maker. Product names mentioned
here belong to their owners.

## Build

    python3 scripts/build_pyz.py       # -> dist/chalkboard.pyz, dist/chalkboard-gui.pyz, dist/install.sh
    python3 scripts/build_mac.py       # -> dist/Chalkboard-macOS.zip (also rebuilds the .pyz)
    sh scripts/build_dmg.sh            # -> dist/Chalkboard-<version>.dmg (macOS only, after build_mac.py)
    pip wheel --no-deps -w dist .      # -> dist/*.whl
    python3 scripts/make_icon.py       # -> macos/Chalkboard.icns (only to change the icon)

Pushing a version tag (`git tag v1.8.0 && git push --tags`) builds all of these on
GitHub's Mac and Windows runners (the `.exe` with PyInstaller), tests them, and
attaches them to a draft release.
