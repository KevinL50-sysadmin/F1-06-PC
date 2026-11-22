# Changelog

All notable changes for players are listed here, newest first. Each GitHub release copies its section.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [1.2.0] - 2026-11-23

### Added

- The README links to the new **Formula One 2005/2006 PC Discord** (the community of F1 06 PC and F1 05 PC) for questions and chat:
  https://discord.gg/sSFHTNQsmv.
- Triangle can now be chosen for any driving action in **PC CONTROLS** (and as `triangulo` in `config\settings.ini`),
  for example LOOK BEHIND on Triangle (Y on an Xbox controller). Triangle is the game's race camera button, so the
  camera moves to the button that action leaves free: with LOOK BEHIND on Triangle, R1 changes the camera.
  RecompLauncher's "Driving controls" has a preset with R2/L2 accelerate and brake, Square/X gears and Triangle
  looks behind.
- `F1-06.exe --reparar-iso "path\game.iso"` checks every file of the `data\` folder copied from that disc and copies
  again only the damaged ones, without copying the whole 4.2 GB (exit code 0 done, 3 a file still does not copy
  right, 5 no `data\` folder of that disc). The `[arranque]` lines of `F1-06.log` list the files repaired.

### Changed

- The installer's shortcuts are now called **F1 06** and **F1 06 - Jugar en red (invitado)** (they were "F1 06 PC").
  Updating removes the old ones, so they do not appear twice. The name in Settings > Apps is still F1 06 PC.
- Besides writing `F1-06.log`, the game now prints a short summary to its console output: the `[version]`,
  `[sistema]` and `[opciones]` lines at start, the `[salida]` lines when it closes (how the session went, the reason
  and the exit code) and the first line of a crash. Some RecompLauncher crash reports attach that output instead of
  the log, and before they only had the log's path. `F1-06.log` itself does not change.

### Fixed

- The shader (pipeline) cache was not saved when you closed the game less than about a minute after starting it,
  or while it was still growing, so the next start compiled those shaders again (short stutters). It is now saved
  once when the game closes normally.
- Custom driving controls only worked in Quick Race: in a Grand Prix weekend (practice, qualifying, race), the
  championship and time trial the car still used the original game's buttons. They now apply whenever you drive.
- No sound on Linux/Proton (issue #3): the copy of the disc could leave some game files damaged (with zeros, at the
  right size) without any error. Now every file is checked as it is copied (its CRC32 against the supported dump,
  and read back after it is written to the drive) and copied again if it comes out wrong; if it is still wrong after
  three attempts, the copy stops with a message naming the file and exit code 3. A `data\` folder you already have
  is checked and its damaged files are repaired: RecompLauncher runs its setup step once more after this update,
  and `F1-06.exe --extraer-iso` does it when `data\` is already there.
- Holding left or right for a moment on a **PC CONTROLS** row could change it twice (for example ACCELERATE from X
  to Circle instead of Square). One press now changes it once; holding the arrow keeps changing it at the same pace
  as the game's own rows.

## [1.1.3] - 2026-11-21

### Added

- Custom driving controls: the new **OPTIONS > PC OPTIONS > PC CONTROLS** page lets you choose the button for
  ACCELERATE, BRAKE, GEAR UP, GEAR DOWN and LOOK BEHIND among X, Square, Circle, L1, R1, L2 and R2 (by default the
  original game's: X, Square, R2, L2 and R1). Choosing a button another action uses swaps them. It only applies
  while driving (replays, photo mode, the pause and the menus keep the usual buttons), and accelerate and brake keep
  the button's pressure, so the analog triggers (RT/LT on an Xbox controller) give progressive throttle and brake.
  RecompLauncher has the same setting as "Driving controls" presets.
- `audio_backend = auto | wasapi | directsound | winmm` in `config\settings.ini` (environment variable
  `PS2X_AUDIO_BACKEND`) chooses which Windows audio system the game tries first; the others stay as fallback.
- `F1-06.log` now has `[audio]` lines: which audio system opened, with which device and format, and whether the
  sound is really playing.
- Launch arguments of the form `NAME=value` at the start of the command line (for example `F1_SPU_TRAZA=1`; capital
  letters, digits and `_`) now set that environment variable, for launchers without Steam's `VAR=value %command%`
  syntax such as RecompLauncher; a literal `%command%` is ignored.
  Before, the game took them for the game file and closed. `F1-06.log` lists them as `[argumentos] variable ...`.

### Changed

- Faster races on older and low-power processors: part of the work that draws the cars and the track (the PS2's
  vector unit) kept running into a very slow path on processors that are slow with certain tiny numbers. The game
  now avoids it, with exactly the same result. Measured on the developer's PC limited to its efficiency cores, as a
  stand-in for a slower CPU: races run about 20-25 % faster (around 34 to 42 fps). Fast desktop processors that
  already reach 50 fps see no change. Not yet measured on an older CPU like the one in issue #1.
- The game's threads now wait for each other for a fixed time instead of a fixed number of processor pauses (much
  shorter on older CPUs), and the graphics thread is woken up more cheaply. This is meant for older processors; on
  modern ones it makes no difference (measured). `F1-06.log` counts these waits in its `[gs:gpu]` and `[iop:hilo]`
  lines.

### Fixed

- Stutter with capture or streaming tools such as Sunshine: when the graphics card falls behind, the graphics
  thread no longer stops to wait for it on every frame; it skips that frame's copy instead. This should mean fewer
  stutters in the menus with these tools open (not yet confirmed on an affected PC; if you still see stutter, see
  *Performance tips* in the README). `F1-06.log` counts the skipped copies in its `[gs:gpu]` lines.
- Sound output on Linux with Proton or Wine: if WASAPI fails to open, or opens but stays silent, the game now falls
  back to DirectSound and then WinMM instead of staying mute. If there is still no sound, try
  `audio_backend = directsound` or `winmm` and send `F1-06.log`. This does not fix every case yet: a Proton setup
  where the audio device opens fine but the game's own sound driver stays silent is still under investigation
  (issue #3).

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

[1.1.3]: https://github.com/KevinL50-sysadmin/F1-06-PC/releases/tag/v1.1.3
[1.1.2]: https://github.com/KevinL50-sysadmin/F1-06-PC/releases/tag/v1.1.2
[1.1.1]: https://github.com/KevinL50-sysadmin/F1-06-PC/releases/tag/v1.1.1
[1.1.0]: https://github.com/KevinL50-sysadmin/F1-06-PC/releases/tag/v1.1.0
