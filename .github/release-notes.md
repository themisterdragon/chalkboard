Chalkboard 1.8.0 adds a **window version** and a new icon, and makes the Windows terminal app snappier.

**New: the window version (`chalkboard-gui`).** The whole planner with a mouse, menus, and buttons, in one of two
old-desktop looks: **Bevel** (gray 3-D buttons, blue title bars) or **Pinstripe** (black-and-white, striped title
bars). Lessons, quizzes and tests with every question type, annotation and bell ringer sheets, the standards
library with import, settings, preview, and every export are all there. It shares its data file with the terminal
app, so you can switch any time. The first time it opens, a short setup wizard asks for your name, school,
class, school colors, and everyday materials, and installs it on your computer. On Windows and Mac it's a standalone app. From the `.pyz` it needs Python 3.8+ with Tk (python.org's installers include it; on
Linux, `sudo pacman -S tk` or `sudo apt install python3-tk`).

**Also new**
- A pixel-art chalkboard icon
- Windows: the terminal app no longer draws each screen line by line, which made it stutter

**Which download?**

| You have | Download |
|----------|----------|
| A Mac | `Chalkboard-Window-1.8.0.dmg`: the window version. Open it, drag Chalkboard Window to Applications; nothing else to install. (`Chalkboard-1.8.0.dmg` is the terminal app; `Chalkboard-macOS.zip` is the same terminal app.) |
| Windows | `Chalkboard-Window-1.8.0-windows.exe`: the window version, nothing else to install. (`Chalkboard-1.8.0-windows.exe` is the terminal app; run it from Windows Terminal for the best look.) |
| Linux | `chalkboard-1.8.0-linux.tar.gz`: unpack it, then `sh install.sh`. Installs both `chalkboard` and the window version, with an app-menu entry (needs Python 3.8+). |
| The window version, from Python | `chalkboard-gui.pyz`: `python3 chalkboard-gui.pyz` (needs Python with Tk) |
| pipx/pip, any OS | the `.whl`: `pipx install chalkboard_planner-1.8.0-py3-none-any.whl` (adds both `chalkboard` and `chalkboard-gui`) |

**First launch:** the apps aren't signed by Apple or Microsoft, so your computer asks before
opening them the first time.
- Mac: System Settings > Privacy & Security > **Open Anyway**. `Read Me First.txt` walks through it.
- Windows: if "Windows protected your PC" appears, click **More info** > **Run anyway**.

**Standards:** Chalkboard doesn't include any standards. Import your state's or district's from a CSV or JSON file
(Standards Library > Import File in the window version, or `I` in the terminal). See
[Standards files](https://github.com/themisterdragon/chalkboard#standards-files).
