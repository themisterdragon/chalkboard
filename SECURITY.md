# Chalkboard's Security Promise

Chalkboard is a lesson planner made by a teacher, for teachers. Your lesson plans,
your students' work, and your words belong to you. These promises hold for every
version of Chalkboard: the window version, the terminal version, and every download
on the [releases page](https://github.com/themisterdragon/chalkboard/releases).

## 1. It works completely offline

Chalkboard never connects to the internet. It has no accounts, no sign-in, no
analytics or "telemetry," no ads, no automatic updates, and no cloud sync. There's
no code in it that can open a network connection.

This gets checked, too. Every release is built by GitHub, and before it's published
the build runs [`scripts/check_offline.py`](scripts/check_offline.py) on Windows and
macOS. That script confirms no part of the code imports networking tools, then
builds a lesson and a quiz with every question type and exports them in every
format while a guard stops the program the instant it tries to reach the network
or start any unexpected program. If anything slips, the release isn't built.
Version 1.8.0 was also tested screen by screen on a computer with its network
switched off.

## 2. Your work stays on your computer

- Your lessons, assessments, and settings live in one file on your computer
  (`data.json`; the README says where on each system). Only your user account can
  read it, so other people who sign in to a shared school computer can't open it.
- Exports go to a folder you choose (`Documents/Chalkboard` unless you change it).
  An exported PDF or Word file carries the document's title, the word "Chalkboard,"
  and when it was made. It doesn't carry your computer's name or your username.
- Chalkboard never uploads, shares, or sends anything. Anything that leaves your
  computer leaves because you copied, printed, emailed, or uploaded it yourself.

## 3. Everything in it is in your voice

Chalkboard doesn't write lessons for you. It has no AI built in and never asks an
AI service for anything. There are no generated lesson plans, questions, or
feedback. It's a place to organize, format, and print what **you** write.

The only words Chalkboard supplies are the headings on its forms and handouts
(like "I Do / We Do / You Do") and an optional list of social-emotional warm-up
prompts it can drop into a bell ringer when you ask for one.

Being upfront about how it was made: Chalkboard's code was written with the help of
an AI coding assistant, guided and reviewed by a teacher. That help went into the
program's code, not into anything Chalkboard says to you or your students, and all
of that code is public here for anyone to read.

## 4. It only runs a few other programs, and only on your own files

When you ask it to, Chalkboard runs a few programs your computer already has:

- **Board slides:** a built-in image tool turns the slide into a PNG (`sips` on
  macOS, or `pdftoppm`, `mutool`, or Ghostscript on Linux and Windows).
- **Opening an export:** your computer's usual "open this file" command.
- **Setup's install step:** copying the app into your Programs or Applications
  folder, and on Windows, making Start-menu and desktop shortcuts. Nothing is
  installed for other users, and no admin password is needed.

## About the downloads

The Mac and Windows apps aren't signed by Apple or Microsoft (that costs money
every year), so your computer will ask before opening them the first time. The
release notes walk you through it. Every download is built by GitHub from the
public code in this repository, so what you install matches what you can read here.

## Reporting a security problem

If you find something that breaks one of these promises, or any other security
problem, please report it privately: open the **Security** tab of this repository
and click **Report a vulnerability**. Please don't open a public issue for it.
You'll get a reply as soon as possible, usually within a week.

Fixes go into the newest release, so please update to the latest version before
reporting.
