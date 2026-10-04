**Chalkboard 2.1** adds the Board Designer, a Day Slideshow for passing time, a Curriculum Map, and
word-processor formatting in the window app. Everything new stays out of the way until you want it.

### Board slides

- **Board Designer** (Settings > Advanced Mode): pick the fonts for the text and headings (the standard ones
  or any font on your computer), balanced or fixed columns, card / outlined / plain-chalk sections, where the
  title and class codes go, and extra-big text, with a live preview. Show, hide, and reorder each section,
  and switch its side. Reset to Default puts it all back.
- **Day Slideshow** (Lesson Plans): pick a day on the calendar, and every class period's board slide goes
  into one slideshow (and a PDF), in period order. At passing time, just go to the next slide.
- **School mascots in the window app:** your mascot, in your school colors, waits on the home screen and
  cheers when an export finishes.

### Planning

- **Advanced Mode** (Settings) for the nitpicky options, off by default. **Page Layouts** set the name line,
  titles, headings, and footers for lesson plans, make-up work, quizzes and tests, annotation sheets, and
  bell ringer sheets, with a live preview and Reset to Default.
- **Curriculum Map** (Lesson Plans): each class's units in order with dates, essential questions,
  standards, lessons, and assessments, plus where each standard is taught. Save it as PDF, Word, or a
  spreadsheet, for any school year.
- **Calendars** for picking a lesson's date(s). Dates now carry their year, so next year's copy of a lesson
  never mixes with this year's.
- **Sort by class** (Lesson Plans and Assessments): pick a class, then see and add its lessons.
- **Make-up sheets carry the classwork:** a lesson's linked worksheets, assignments, and exit tickets print
  right after its make-up sheet (student copies). Quizzes and tests show up as a "See Me" step instead.

### Formatting

- **B / I / U buttons** in every editor, and text boxes show bold, italic, and underline as they'll print
  instead of the `**` and `__` marks. Ctrl+B / Ctrl+I / Ctrl+U (Cmd on a Mac), and Undo takes back
  formatting too. The terminal view still shows the marks; both read the same files.
- Bold and italic on the same word now prints right everywhere.

### Fixes

- Settings > **Sections & School Colors** opens again (the button did nothing in 2.0).
- The window opens sized to the screen, so on a small laptop it no longer runs under the Windows taskbar
  and hides a dialog's buttons.
- Drop-down and right-click menus close when you click elsewhere (Linux).
- **Tidier export folders:** the PDFs you print stay on top of each lesson's folder; answer keys, board
  slides, Word files, and text files get folders of their own. Exporting an older lesson again tidies it.

**Upgrading:** your lessons and settings carry over on their own. They live in your data file, not in the
app. Tried a 2.1 beta? The beta kept its own copy of your lessons. To bring that work over: in the beta,
File > Back Up Everything; then in Chalkboard 2.1, File > Import Backup and choose "Add what I don't have."
After that you can delete the beta.

**Which download?**

| You have | Download |
|----------|----------|
| A Mac | `Chalkboard-2.1.0.dmg`. Open it and drag Chalkboard to Applications; nothing else to install. (`Chalkboard-Terminal-2.1.0.dmg` is the terminal-only app.) |
| Windows | `Chalkboard-2.1.0-windows.exe`, nothing else to install. (`Chalkboard-Terminal-2.1.0-windows.exe` is the terminal-only app; run it from Windows Terminal for the best look.) |
| Linux | `chalkboard-2.1.0-linux.tar.gz`: unpack it, then `sh install.sh`. It offers to install anything missing (Python, Tk, and the tool that makes board pictures) and adds Chalkboard to your app menu. |
| Python, any OS | `chalkboard-gui.pyz` (`python3 chalkboard-gui.pyz`, needs Python with Tk), or the `.whl` with pipx: `pipx install chalkboard_planner-2.1.0-py3-none-any.whl` |

**First launch:** the apps aren't signed by Apple or Microsoft, so your computer asks before
opening them the first time.
- Mac: System Settings > Privacy & Security > **Open Anyway**. `Read Me First.txt` walks through it.
- Windows: if "Windows protected your PC" appears, click **More info** > **Run anyway**.

**Standards:** Chalkboard doesn't include any standards. Import your state's or district's from a CSV or JSON file
(Standards Library > Import File, or `I` in the terminal view). See
[Standards files](https://github.com/themisterdragon/chalkboard#standards-files).
