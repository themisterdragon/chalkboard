**Chalkboard 3** is the same Chalkboard, rebuilt from the ground up in C++: one small program for
each computer, nothing else to install (no Python, no extra tools), and still completely offline.
Your lessons, assessments, standards, and settings carry over on their own.

### A fresh new look

- **A sidebar that takes you anywhere:** Today, Lessons, Assessments, Calendar, Standards, Exports,
  Settings, and the Terminal view, always one click away.
- **Today:** your week at a glance, quick actions, recent work, and your school mascot. Plan any day
  from here.
- **Calendar:** a month of your lessons, color-coded by class.
- **Modern look** in light or dark, in Chalkboard green, your school's colors, or your computer's
  accent color. Prefer something fun? Pick **Glossy** (shiny 2000s glass), **Bevel**, or **Pinstripe**
  from the View menu. Every look passes the WCAG AA contrast check.
- With a menu open, slide across the menu bar to see the others.

### Faster and simpler

- **Board slides are drawn by Chalkboard itself.** No image tool to install on Linux or Windows, and
  slides look the same on every computer.
- Opens fast and stays small, on Windows, Mac (Apple silicon and Intel), and Linux (Intel/AMD and ARM,
  like a Raspberry Pi 4 or 5).
- The terminal app is in the same program: run `chalkboard` in a terminal, or use the Terminal view in
  the window. Over SSH with no desktop, it starts as the terminal app.

### Fixes

- The lesson and assessment editors now scroll when your work runs past the bottom of the window.
- A dialog over another dialog no longer darkens the window twice.

### Changes

- **Plugins are gone.** Chalkboard 3 runs no add-on code at all. Exports (PDF, Word, text, board slides,
  slideshows, spreadsheets) are all built in.
- Using the Bevel look? Chalkboard 3 opens in the new Modern look once, with a note to switch back
  (View > Bevel, or the **Go Back to Bevel** button).

**Upgrading from 2.x:** install Chalkboard 3 and your work is already there. It lives in your data file,
not in the app. Back up first if you like (File > Back Up Everything in 2.x).
Tried a 3.0 beta? The beta kept its own copy of your lessons. To bring that work over: in the beta,
File > Back Up Everything; then in Chalkboard 3, File > Import Backup and choose "Add what I don't
have." After that you can delete the beta.

## Download

| Your computer | Download | Then |
|---|---|---|
| Mac (macOS 11+, Apple silicon or Intel) | `Chalkboard-@VERSION@-macOS.dmg` | Open it, drag Chalkboard to Applications. First time: right-click Chalkboard > Open, then Open (on macOS 15+: System Settings > Privacy & Security > Open Anyway) |
| Windows 10 / 11 | `Chalkboard-@VERSION@-windows.exe` | Double-click. If Windows says "Windows protected your PC": More info > Run anyway |
| Linux (Intel/AMD) | `Chalkboard-@VERSION@-linux-x86_64.tar.gz` | Unpack, double-click `chalkboard` (or run it in a terminal) |
| Linux (ARM, e.g. Raspberry Pi 4/5) | `Chalkboard-@VERSION@-linux-arm64.tar.gz` | The same |

The first time, a short setup offers to install Chalkboard (Applications, the Start menu, or your app
menu). The Mac and Windows programs aren't signed with a paid developer certificate, so each asks once
as above.

Need the old Python version? [Chalkboard 2.1.0](https://github.com/themisterdragon/chalkboard/releases/tag/v2.1.0)
is still available.

**Standards:** Chalkboard doesn't include any standards. Import your state's or district's from a CSV or
JSON file (Standards Library > Import File, or `I` in the terminal view). See
[Standards files](https://github.com/themisterdragon/chalkboard#standards-files).
