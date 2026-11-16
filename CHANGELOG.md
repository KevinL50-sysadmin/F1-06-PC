# Changelog

All notable changes for players are listed here, newest first. Each GitHub release copies its section.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

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

[1.1.1]: https://github.com/KevinL50-sysadmin/F1-06-PC/releases/tag/v1.1.1
[1.1.0]: https://github.com/KevinL50-sysadmin/F1-06-PC/releases/tag/v1.1.0
