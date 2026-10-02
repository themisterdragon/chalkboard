Chalkboard 1.8.1 fixes the Mac window version: **Random SEL Prompt** did nothing, because the app was built
without its list of prompts ([#1](https://github.com/themisterdragon/chalkboard/issues/1)). Nothing else changed.
If you're on Windows or Linux, or use the terminal app, you don't need to update.

New in 1.8 is the **window version** (`chalkboard-gui`): the whole planner with a mouse, menus, and buttons, in a
**Bevel** or **Pinstripe** look. It shares its data file with the terminal app, so you can switch any time.

**Which download?**

| You have | Download |
|----------|----------|
| A Mac | `Chalkboard-Window-1.8.1.dmg`: the window version. Open it, drag Chalkboard Window to Applications; nothing else to install. (`Chalkboard-1.8.1.dmg` is the terminal app; `Chalkboard-macOS.zip` is the same terminal app.) |
| Windows | `Chalkboard-Window-1.8.1-windows.exe`: the window version, nothing else to install. (`Chalkboard-1.8.1-windows.exe` is the terminal app; run it from Windows Terminal for the best look.) |
| Linux | `chalkboard-1.8.1-linux.tar.gz`: unpack it, then `sh install.sh`. Installs both `chalkboard` and the window version, with an app-menu entry (needs Python 3.8+). |
| The window version, from Python | `chalkboard-gui.pyz`: `python3 chalkboard-gui.pyz` (needs Python with Tk) |
| pipx/pip, any OS | the `.whl`: `pipx install chalkboard_planner-1.8.1-py3-none-any.whl` (adds both `chalkboard` and `chalkboard-gui`) |

**First launch:** the apps aren't signed by Apple or Microsoft, so your computer asks before
opening them the first time.
- Mac: System Settings > Privacy & Security > **Open Anyway**. `Read Me First.txt` walks through it.
- Windows: if "Windows protected your PC" appears, click **More info** > **Run anyway**.

**Standards:** Chalkboard doesn't include any standards. Import your state's or district's from a CSV or JSON file
(Standards Library > Import File in the window version, or `I` in the terminal). See
[Standards files](https://github.com/themisterdragon/chalkboard#standards-files).
