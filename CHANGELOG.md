# Changelog

All notable changes for players are listed here, newest first. Each GitHub release copies its section.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [1.1.2] - 2026-11-19

### Added

- One-click setup in [RecompLauncher](https://recomplauncher.com/game/f1-06-pc): the release now includes a
  `recomplauncher.json`. Pick your ISO once and the launcher checks it against the known good dump of the PAL disc
  (SCES-53950), copies the game data with a progress bar, as the installer does, and then starts the game with
  **Play**. The graphics and display options (window mode, scaling filter, supersampling, double resolution, V-Sync,
  aspect ratio, car detail, track draw distance and FPS counter) can be changed on the game's page in the launcher,
  and `F1-06.log` is also written when the launcher starts the game.
- Crash reports with the log: when the game closes because of a failure it now ends with an error code, so
  RecompLauncher's crash report attaches `F1-06.log`. The codes are 10 (an error in the game's code), 11 (the
  original game stopped itself after an error) and 12 (the game was still frozen, or its picture still stuck, when you
  closed it); crashes keep Windows' own error code. A normal close, or a freeze the game recovered from, ends
  normally. How to send a problem is in the README.
- `F1-06.log` now starts with the game version and build, your Windows version, CPU, cores, RAM and power plan, and
  the settings the game is really using.
- Crashes, errors in the game's code and aborts are written to `F1-06.log` with the part of the game that failed and
  where in the code it happened, in a form that can be traced back to the exact line (`scripts\simbolizar.ps1` with
  the symbols kept for each release). Errors in the game's own Lua scripts also list the script and position.
- When the game closes, `F1-06.log` ends with a short summary of the session (menu or race, track, duration,
  average fps and, in a race, the lowest fps over 10 seconds) and the reason it closed.
- For troubleshooting only: the environment variable `PS2X_CAPTURA_LANZAMIENTOS=0` turns off the recording of
  where errors in the game's code started. It makes no measurable difference to speed.

### Changed

- Lighter graphics by default: supersampling 2x (was 4x) and double resolution off (was on), so the graphics card
  has much less work. This only applies to new installs; if you already have a `settings.ini`, your
  values are kept. You can raise both in OPTIONS > PC OPTIONS > PC GRAPHICS or on the game's page in RecompLauncher.
- `F1-06.log` now shows the real values in the messages the game's sound and disc modules print, and the two
  warnings the original game sometimes prints when you quit a race ("Closing file ... will probably be fatal" and
  "IOP FATAL ERROR: callback received ...") are marked as a known, harmless warning from the original game. They
  come from a race in the original game's own code, nothing is lost and the game carries on normally.
- Less noise in `F1-06.log`: memory card messages are only written when something changes, and the frame counter
  line comes every 600 frames instead of every 50, so long sessions no longer make the log huge.

## [1.1.1] - 2026-11-16

### Added

- Freeze watchdog: if the game stops moving for 10 seconds, `F1-06.log` records where every part of the game was
  stuck, without closing it, and, if the game carries on, how long the freeze lasted. Nothing is written while the
  game runs normally, and loading screens, videos, the pause menu or a minimized window do not trigger it. Change the
  wait with `vigilante` in `config\settings.ini` (3 to 600 seconds; 0 turns it off).
- Freeze reports can now be traced back to the exact place in the game's code, which makes reported hangs much
  easier to fix. Nothing extra is downloaded or installed for this.

### Changed

- The player's folders and files now have English names: `datos\` is `data\`, `partidas\` is `saves\`,
  `registros\` is `logs\`, `capturas\` is `screenshots\`, `licencias\` is `licenses\`, `config\opciones.ini` is
  `config\settings.ini` and `config\controles.ini` is `config\controls.ini`. Option names inside the `.ini` files,
  menus and windows stay as they were.
- Updating from 1.1.0 needs nothing from you: on its first start the game renames the old folders and files (in
  the game folder, `Documents\My Games\F1-06` and `%LOCALAPPDATA%\F1-06`) without deleting, copying or merging
  anything, and the installer renames `datos\` to `data\` in the game folder. If both names exist, the new one is
  used and the old one is left untouched. If something cannot be renamed (a file in use, or `datos\` under Program
  Files without administrator rights), the game keeps using the old name.
- The package now has `README.txt` in English next to `LEEME.txt` in Spanish, and `Jugar en red - Invitado.cmd` is
  now `Play online - Guest.cmd`.

## [1.1.0] - 2026-11-12

First public release. It contains no game data: on the first start you choose your own copy of Formula One 06
(PS2, European PAL version, SCES-53950) as an `.iso` file.

### Added

- The full PS2 game running natively on Windows at 50 fps, with GPU rendering (paraLLEl-GS on Vulkan) and a
  software renderer as a fallback.
- PC options in the game's own menu (OPTIONS > PC OPTIONS): supersampling, image filters (FSR 1, CRT, sharp...),
  VSync, windowed, borderless or full screen, widescreen, field of view up to +30 %, car detail (REDUCED, AS ON
  PS2, HIGH, MAXIMUM) and scenery distance (AS ON PS2, FAR, VERY FAR), HUD modes, fast loading, finer AI difficulty,
  colour-blind filters, sound and vibration settings.
- Photo mode with a free camera, replays with rewind and fast-forward, and PNG screenshots.
- DualSense, DualShock 4, XInput and keyboard support; Logitech G29 as the game's Driving Force Pro, with force
  feedback; split screen; experimental online play between two PCs.
- Installer and portable package.

[1.1.2]: https://github.com/KevinL50-sysadmin/F1-06-PC/releases/tag/v1.1.2
[1.1.1]: https://github.com/KevinL50-sysadmin/F1-06-PC/releases/tag/v1.1.1
[1.1.0]: https://github.com/KevinL50-sysadmin/F1-06-PC/releases/tag/v1.1.0
