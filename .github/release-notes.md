**2.0.1** fixes a crash on Macs: Chalkboard Terminal wouldn't open, and switching to the terminal view or
running setup again froze the app. If you're on 2.0.0 on a Mac, replace it with this one; your lessons are kept.

**Chalkboard 2.0** is one app with two views. Open **Chalkboard** and plan with windows, buttons, and menus,
or switch to the retro **terminal view** (green screen, keyboard-driven) any time with View > Switch to
Terminal View or **Ctrl+Shift+W** (Cmd+Shift+W on a Mac). Both views work on the same lessons, so you never
lose your place. Setup asks which one you'd like to start with. If you'd rather have only the terminal,
**Chalkboard Terminal** is still its own download.

Chalkboard is still fully offline: it never goes online, and now it holds itself to that while it runs.

### New for your classroom display

- **Board slides you can edit.** Every board export now includes a slideshow for PowerPoint, Keynote, or
  Google Slides. The title, each panel, the date, and your class codes are real text boxes, so you can fix a
  typo, change a code, or reuse it next school year.
- **Bigger, easier-to-read slides.** Text sizes itself to fill the slide. When a lesson has too much to read
  from the back row, standards shrink to just their codes, then the board continues on a second slide.
- **Your school logo** on board slides (PNG or JPEG, from Settings).
- **Class codes per period:** add each class period under Settings > Class Periods & Codes, and each period
  gets its own slide with its own codes in big type.

### New for planning

- **Essential Question** and **Words to Know** sections for lessons, with a one-click **vocab quiz** from
  your word list. Hide any section you don't use (Sections…), so the editor stays short.
- **Charts and graphic organizers** as a question type: tables, T-charts, KWL, Venn diagrams, webs,
  sequences, Frayer models, and plot diagrams.
- **Bold, italic, and underline:** Ctrl+B, Ctrl+I, Ctrl+U on a word or a selection (Ctrl+T for italic in the
  terminal view).
- **Web addresses become links** you can click in the PDF, Word, and slideshow files.

### New for keeping and sharing your work

- **Back Up Everything** saves all your lessons, assessments, standards, and settings into one file in any
  folder. **Import Backup** brings it back on this computer or another one, either adding what you don't
  have or replacing everything. Chalkboard keeps a copy of what you had first.
- **Export Everything** (File menu) makes one folder with every lesson and assessment as PDF and Word, sorted
  by class and unit, plus a backup file. Keep it, or drag the whole folder into Google Drive.
- **Share your standards:** Standards Library > **Export to Share** saves the standards you imported or wrote
  yourself as a file a colleague can import.

### Looks and comfort

- A new **Modern** look that matches your computer, next to Bevel and Pinstripe, and **dark mode** for every
  look (it can follow your computer's setting).
- Every look meets WCAG 2.1 AA contrast. Known gap: screen readers can't read the window version or its
  terminal view yet; the standalone terminal app may work better with one.
- In the terminal view, a **school mascot** of your choice (from Settings) joins the startup screen and runs
  along the export progress bar. Nineteen to pick from, Dragons included.

### For tinkerers

- **Plugins:** a Python file in the `plugins` folder can add an export format, a mascot, or a step after each
  export. A broken plugin is switched off with a note, and Chalkboard keeps working. The offline guard covers
  plugins too. See `examples/plugins/markdown_export.py`.
- **PORTING.md** maps the code for anyone bringing Chalkboard to another system.

**Upgrading:** your lessons and settings carry over on their own. They live in your data file, not in the
app. If you have **Chalkboard Window** from 1.8, you can delete it once Chalkboard 2.0 is installed. The
window app is now just called **Chalkboard**.

**Which download?**

| You have | Download |
|----------|----------|
| A Mac | `Chalkboard-2.0.1.dmg`. Open it and drag Chalkboard to Applications; nothing else to install. (`Chalkboard-Terminal-2.0.1.dmg` is the terminal-only app.) |
| Windows | `Chalkboard-2.0.1-windows.exe`, nothing else to install. (`Chalkboard-Terminal-2.0.1-windows.exe` is the terminal-only app; run it from Windows Terminal for the best look.) |
| Linux | `chalkboard-2.0.1-linux.tar.gz`: unpack it, then `sh install.sh`. It offers to install anything missing (Python, Tk, and the tool that makes board pictures) and adds Chalkboard to your app menu. |
| Python, any OS | `chalkboard-gui.pyz` (`python3 chalkboard-gui.pyz`, needs Python with Tk), or the `.whl` with pipx: `pipx install chalkboard_planner-2.0.1-py3-none-any.whl` |

**First launch:** the apps aren't signed by Apple or Microsoft, so your computer asks before
opening them the first time.
- Mac: System Settings > Privacy & Security > **Open Anyway**. `Read Me First.txt` walks through it.
- Windows: if "Windows protected your PC" appears, click **More info** > **Run anyway**.

**Standards:** Chalkboard doesn't include any standards. Import your state's or district's from a CSV or JSON file
(Standards Library > Import File, or `I` in the terminal view). See
[Standards files](https://github.com/themisterdragon/chalkboard#standards-files).
