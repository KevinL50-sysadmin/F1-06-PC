# Start here

> What RecompLauncher does for your recomp, the three levels, and the smallest file that works.

One file in your release, `recomplauncher.json`, lets RecompLauncher set your recomp up for the player. They link their own copy of the game, watch it get checked and set up, change your settings on the game's page or in Big Picture mode, and press Play. They never open a config file or your own launcher.

Your recomp doesn't have to change. The launcher only does what a player would do by hand: it edits your settings file, passes command-line arguments and starts your executable.

## Why it's worth ten minutes

- **One-click installs.** Players press Install, and the launcher picks the right file from your latest GitHub release, checks it and unpacks it. Updates come the same way, and saves, settings and mods are left alone.
- **Their copy, found for them.** The launcher scans players' game folders and tells them when a game they own has your recomp, so you reach people who didn't know it existed.
- **Your settings, without a config file.** Resolution, frame rate, upscaling and the rest show on the game's page and in Big Picture mode, so players change them with a mouse or a controller.
- **Fewer support questions.** The setup screen checks the player's copy before the first start, and crash reports arrive with your game's log attached.
- **A page of your own.** Your recomp's page on recomplauncher.com, with its art, your words and your players' reports, editable from your developer hub.

## The three levels

Each level builds on the last, and you can stop at any of them. Level 1 takes about ten minutes.

| Level | What players get | What you add |
| --- | --- | --- |
| 1. Plays from RecompLauncher | Their copy is found or dropped in, checked and set up; Play starts the game directly | `game`, `start` |
| 2. Settings in the launcher | Your settings on the game's page and in Big Picture mode, plus looks, mods, folders and friendly error messages | `config`, `settings`, and optionally `looks`, `mods`, `folders`, `actions`, `exit_codes` |
| 3. Full setup | Your own setup step on the setup screen, and a verified badge for clean dumps | `prepare`, `game.dumps` |

Without a file, your recomp still installs, updates and starts from the launcher. The file is what makes it one click.

## Where the file goes

Put `recomplauncher.json` at the top of your release zip, next to your executable. It ships with every release, so it's always in step with your build.

The launcher uses the first file it finds:

1. **Your release.** The best place, for the reason above.
2. **The catalog.** If you can't ship it, send it to us and we'll add it to your entry on recomplauncher.com.
3. **Built in.** A few files are built into the launcher and matched by GitHub repo. Two good examples: 2014 FIFA World Cup (Xbox 360, a settings file with eight settings) and NBA Street Vol. 2 (PS2, settings passed on the command line).

## The smallest useful file

```json
{
  "$schema": "https://recomplauncher.com/schema/recomplauncher.json",
  "format": 1,
  "game": {
    "kind": "iso",
    "label": "Your disc image (.iso) of the USA release, serial SLUS-20651.",
    "check": { "iso9660": "SLUS_206.51" }
  },
  "start": {
    "program": ["bin/game.exe", "game.exe"],
    "args": ["--iso", "{game}"]
  }
}
```

The `$schema` line lets your editor complete fields and flag mistakes as you type. `format` is always `1`.

Next: **Releases that install cleanly** covers how the launcher picks and installs your files, and **Level 1** explains each part of this file.

---

# Releases that install cleanly

> How the launcher picks your release files, what updates keep, and how pre-releases become betas.

The launcher installs and updates your recomp straight from your GitHub releases. Nothing goes through us, and every release you publish reaches players the next time the launcher checks. Most releases already work. These make sure yours does.

## One file per system

Put the system and the processor in each file's name, and the launcher picks the one for the player's PC:

- `MyRecomp-v1.2.0-Windows-x64.zip`
- `MyRecomp-v1.2.0-Linux-x64.AppImage`
- `MyRecomp-v1.2.0-Windows-arm64.zip` for ARM builds

When your recomp is listed, its entry can name the file outright (`MyRecomp-*-Windows.zip`). If you rename your files later, the launcher falls back to reading the names, so nothing breaks.

## The right kind of file

- **Windows:** a `.zip` of your folder is best, then `.7z`. An installer `.exe` works, but it's the last choice, because it wants to be run as setup.
- **Linux:** an AppImage or a `.tar.gz`. A Flatpak bundle comes last, since it needs Flatpak itself.
- **One folder on top is fine.** A zip with everything inside `MyRecomp-v1.2.0/` is unwrapped, so your program lands in the game's folder either way.
- **Extras stay out of the way.** Files with `debug`, `symbols`, `pdb` or `source` in the name, and `.sha256`, `.sig` and `.asc` files, are never taken for the game.

## Checked on the way in

GitHub keeps a SHA-256 of every file you upload. The launcher checks each download against it before installing anything. You don't have to do anything for this.

## What updates keep

An update unpacks over the game's folder. Files your game made, like saves, settings, caches and mods, stay where they are, and the launcher backs up saves before every update.

That also means a settings file *inside your release* replaces the player's on every update. Ship your defaults another way:

- name them differently (`default.toml`) and copy them on first start, or
- write them when the file doesn't exist yet, or
- let the launcher write them with `config.new` (Level 2).

Put `recomplauncher.json` at the top of the zip, next to your program, and it updates with every release.

## Pre-releases are betas

Mark a test build as a pre-release on GitHub. Players on your normal release never get it by accident: the launcher always installs your latest full release.

When you want players to try it, tell us in Messages and we open it as a beta. It can be open to everyone, by request, or by invite. Players join from the launcher or their account page, play it, and go back to your normal release with one button. A beta can follow your newest pre-release, or one release by its tag.

---

# Level 1: plays from RecompLauncher

> The player's copy and how your game starts, plus the paths you can use anywhere.

With `game` and `start`, installing your recomp ends with it ready to play. What the player sees:

1. They install your recomp. If the launcher already found their copy in one of their game folders, it offers that one. Otherwise they drop the disc image on the setup screen, or pick it in the launcher's file browser.
2. The setup screen reads the disc, checks it's your game, reads the whole image to make sure it's complete, and writes your settings.
3. "Ready to play", with Play and Settings. Play starts your program straight away.

## `game`: the player's copy

- `kind`: `"iso"`, `"folder"` or `"iso_or_folder"`.
- `label`: what to ask for. Name the region or serial if only one works.
- `check`: how to tell it's the right game. Use whichever fits:
  - `file`: a file that should be in the folder, like `"default.xex"`.
  - `xdvdfs`: a file on an Xbox 360 disc image.
  - `iso9660`: a file in the top folder of a PlayStation 2 (or any ISO 9660) disc image.

The same check is how the launcher spots your game in the player's folders, so a disc image named `backup-07.iso` is still found, and the player hears about your recomp when it comes out. Leave `game` out if your recomp finds the game itself.

## `start`

- `program`: paths inside your release, tried in order. The first one that exists runs.
- `args`: always passed. Players can add their own launch options after yours.
- `env` (RecompLauncher 0.9.3 and later): environment variables for your game, as names and values. Paths fill in like everywhere else. Names are letters, digits and `_`; system ones like `PATH` can't be set.

```json
"start": {
  "program": ["MyRecomp.exe"],
  "args": ["--iso", "{game}"],
  "env": { "MYRECOMP_DATA": "{documents}/My Recomp" }
}
```

## What every game is told

From RecompLauncher 0.9.3, every game the launcher starts gets these environment variables, with a `recomplauncher.json` or without:

| Variable | Value |
| --- | --- |
| `RECOMPLAUNCHER` | `1` |
| `RECOMPLAUNCHER_VERSION` | The launcher's version, like `0.9.3` |
| `RECOMPLAUNCHER_MODE` | `bigpicture` when Play was pressed in Big Picture mode, else `desktop` |
| `RECOMPLAUNCHER_PAD` | The controller plugged in: `xbox` or `playstation` |

Use them to fit in: skip your own launcher window, open full screen with your controller menus when the mode is `bigpicture`, and show the right button prompts from the first frame. More in **Controllers and Big Picture mode**.

## Paths

These fill in anywhere a path, an argument or a value is written.

| Placeholder | What it becomes |
| --- | --- |
| `{install}` | Your game's folder |
| `{game}` | The player's copy (the .iso or the folder) |
| `{documents}` | Documents |
| `{local}` | AppData\Local, or ~/.local/share |
| `{config}` | AppData\Roaming, or ~/.config |
| `{home}` | The user's home folder |
| `{pad}` | The controller plugged in: `xbox` or `playstation` |

Where a file can be in more than one place, write a list. The first entry whose `if` exists is used, and an entry without `if` is the fallback:

```json
"file": [
  { "path": "{install}/game.toml", "if": "{install}/portable.txt" },
  { "path": "{documents}/My Game/game.toml" }
]
```

## `saves`

Where your game keeps its saves. The launcher copies them after every session (the newest ten stay on the player's PC), and players who sign in can send them to their account and pick up on another PC. List folders, single files, or files matching a `*` pattern:

```json
"saves": ["{local}/My Game/saves", "{install}/settings.toml"]
```

Without it, the launcher watches which files the game writes while it runs and uses those places. That works for most games, but saying where is better. Only list saves and small settings: game files, programs and archives are never copied, and nothing over 64 MB.

---

# Level 2: settings in the launcher

> Your settings on the game's page and in Big Picture mode, plus upscalers, looks, mods, folders and error messages.

With `config` and `settings`, your options show on the game's page and in Big Picture mode, where left and right on a controller change them. You write them once; the launcher draws them.

## `config`: your settings files

Each entry is a file the launcher may edit before every start.

- `id`: the name settings use to point at it. Defaults to `"main"`.
- `format`: `"toml"`, `"ini"` or `"json"`. TOML and INI are edited line by line, so comments, order and keys the launcher doesn't know about stay as they are.
- `file`: where it is (see Paths in Level 1).
- `game_path`: the key that holds the path to the player's copy. Written on every start.
- `ensure`: keys set only when they're missing or empty.
- `always`: keys set on every start. Keep this to what your recomp needs to run from the launcher.
- `new`: keys set only when the file doesn't exist yet. These are your defaults.

Values are written exactly as given, so TOML strings keep their quotes: `"\"xenos\""`.

## `settings`: what the player can change

Each setting is a row with a list of choices.

```json
{
  "id": "resolution",
  "label": "Resolution",
  "help": "What the game draws at. 720p is the console's own.",
  "choices": [
    { "label": "720p", "set": { "resolution_scale": "1" } },
    { "label": "1440p", "set": { "resolution_scale": "2" } }
  ],
  "defaults": { "resolution_scale": "1" }
}
```

- **In a file.** A choice's `set` lists the keys it writes, in the file named by `in` (default `"main"`). The launcher shows the first choice whose keys all match the file. Keys the file doesn't have yet are read from `defaults`, so put your game's own defaults there.
- **On the command line.** Set `"in": "args"` and give each choice `args` instead of `set`. The launcher remembers the pick and adds those arguments on Play. An empty `args` makes a good first choice, labeled "Game's option" or similar.
- **Only sometimes.** `when` grays a setting out unless the keys it lists match. FIFA's Window size has `"when": { "fullscreen": "false" }`.

Keep labels short and help text to one sentence. Say what it does for the player, not what the key is called.

## Upscalers and DLSS

If your recomp has DLSS, FSR, XeSS or frame generation, expose it as a setting like any other. These need data from inside the game every frame (motion vectors, depth), so the launcher can't add them from outside, but it can give players one clear place to turn them on.

```json
{
  "id": "upscaler",
  "label": "Upscaling",
  "help": "DLSS needs an NVIDIA RTX card. FSR works on any card.",
  "choices": [
    { "label": "Off", "set": { "upscaler": "\"none\"" } },
    { "label": "FSR", "set": { "upscaler": "\"fsr\"" } },
    { "label": "DLSS", "set": { "upscaler": "\"dlss\"" } }
  ],
  "defaults": { "upscaler": "\"none\"" }
}
```

## `looks`

Players can pick a look for any game on Windows: Sharp, Vivid, Film, CRT or Studio monitor. They run on ReShade, which the launcher puts next to your executable for that start only and takes away when the game closes. Your release isn't changed.

You don't need to do anything for this. Add `looks` only if one of these is true:

- Your recomp can draw with more than one API and the player picks it in your settings file. Name that key in `graphics_key` (and its file in `in`). Values containing `vulkan`, `d3d12`, `d3d11`, `d3d9` or `opengl` are understood.
- It always uses an API the launcher might not spot, such as OpenGL loaded at run time. Set `"graphics": "opengl"`.
- Your game didn't run at the console's usual line count (240 for Nintendo 64, 480 for PlayStation 2). Set `"lines"` for the CRT looks.
- Your recomp has its own filters, or ReShade doesn't work with it. Set `"allowed": false` and a `"reason"` to show the player.

```json
"looks": { "graphics_key": "graphics_api", "in": "main" }
```

## `mods`

The game's page gets a Mods tab with the most popular mods, installed and removed in one click. Thunderstore and GameBanana are supported. Zelda 64: Recompiled, Banjo: Recompiled, Unleashed Recompiled, Marathon Recompiled and the Ship of Harkinian family are built in.

```json
"mods": { "thunderstore": "your-community", "data": "YourRecomp", "gamebanana": 12345, "loader": "nrm" }
```

- `thunderstore` and `data`: your community on thunderstore.io and your data folder's name (in AppData\Local on Windows, ~/.config on Linux). Mods go in its `mods` folder, or in your game's own `mods` folder when `portable.txt` is next to it.
- `gamebanana` and `loader`: your game's id from its GameBanana address and how your game reads mods:
  - `nrm`: `.nrm` files in the same mods folder as above (needs `data`).
  - `otr`: `.otr` and `.o2r` files in a `mods` folder next to your executable.
  - `hmm`: Hedge Mod Manager mods with a `mod.ini`, copied next to your executable and switched on in `ModsDB.ini`. `user` names your folder in AppData\Roaming that holds `cpkredir.ini`.

Only GameBanana files that passed its virus scan and come as a zip, 7z or rar install from the launcher. For the rest, the player gets a link to the mod's page.

## `folders`, `actions` and `exit_codes`

- `folders`: named places the launcher can open or clear. `saves` is what it backs up before every update. When your game crashes, the newest `.log` in `logs` (or in your game's folder) is shown and goes with the report the player can send you.
- `actions`: buttons under the settings. `open` opens one of your folders; `clear` empties one, written as a folder name and a subfolder (`"saves/cache"`). The launcher asks first.
- `exit_codes`: when your game exits with one of these, the player sees your message instead of a crash report.

```json
"exit_codes": { "252968961": "It couldn't find your copy of the game. Pick it again on the game's page." }
```

---

# Level 3: full setup

> Run your own setup step without a window, and give clean dumps a verified badge.

## `prepare`: your own setup step

If your own launcher does work before the first start, like pulling fonts or menu art from the disc or building a cache, give the launcher a way to run it without a window:

```json
"prepare": {
  "program": ["yourgame.exe"],
  "args": ["--prepare", "--game", "{game}"],
  "label": "Menu art and fonts from your disc",
  "timeout": 300
}
```

It runs on the setup screen as "Getting the extras", after the copy is checked and before the settings are written. It runs again whenever the player changes their copy.

- Don't open a window.
- Print lines like `progress 0.4 Unpacking menu art` as you go. The number runs from 0 to 1, and the text after it shows under the step.
- Exit with 0 when it worked. Anything else shows as a failed setup, with your exit code.
- Write what you make to the same place your own launcher would, so both stay in step.

**When players must run it again.** Players set up once. A launcher update, or changes to anything else in
your file, never ask them to set up again. When an update of yours changes what the step makes, so
everyone's copy has to be made again, change `"version"` in `prepare` (any text, like `"2"`):

```json
"prepare": { "program": ["yourgame.exe"], "args": ["--prepare", "--game", "{game}"], "version": "2" }
```

Players whose setup was done with a different version run the step again. Leave it out until you need it.

**A build players already have.** If players can also build the game with your own launcher, list a file
your step leaves behind once it has finished, in `"done"`. When all of them are there, RecompLauncher
(Beta 51 and newer) counts the step as done and starts the game without asking for the copy again, so a
player who built it your way first doesn't build it twice:

```json
"prepare": { "program": ["yourgame.exe"], "args": ["--prepare", "--game", "{game}"], "done": ["{home}/Games/YourGame/prepared.bin"] }
```

The same words work as elsewhere: `{home}`, `{local}`, `{install}`.

## `game.dumps`: known good copies

```json
"game": {
  "kind": "iso",
  "check": { "xdvdfs": "default.xex" },
  "dumps": [
    { "name": "USA", "size": 7838695424, "crc32": "1a2b3c4d" },
    { "name": "Europe", "size": 7838695424, "crc32": "5e6f7a8b" }
  ]
}
```

The launcher reads every byte of the player's copy anyway, to make sure it's complete. With `dumps` it also compares the CRC32 and shows "Matches the verified USA disc". Take the values from Redump. A copy that doesn't match still works; it just doesn't get the badge.

---

# Controllers and Big Picture mode

> Fit in with players on a controller, the couch or a Steam Deck, and why SDL3 is the way to go.

Big Picture mode is the launcher's full-screen view for a controller, the couch and the Steam Deck. Players pick a game, set it up and press Play without touching a mouse. The best thing your recomp can do is carry on the same way.

## Use SDL3 for input if you can

SDL3's gamepad support knows hundreds of controllers, including Xbox, DualSense and DualShock, Switch Pro, 8BitDo and the Steam Deck, and names their buttons the same way. It also gives you gyro, touchpads, rumble and lights where the pad has them.

Recomps on SDL2 work fine. SDL3 is where controller support is going, and it's what we'll build further controller features on, so it's the one to pick for anything new.

## Playable start to finish on a controller

Menus, settings, the "pick your copy" step if you still have one, and quitting. A player in Big Picture mode may have no keyboard in reach.

## Know where you were started from

From RecompLauncher 0.9.3, every game gets `RECOMPLAUNCHER_MODE` and `RECOMPLAUNCHER_PAD` (see **Level 1**).

- **`RECOMPLAUNCHER_MODE`:** start full screen in `bigpicture`, and as your players expect in `desktop`.
- **`RECOMPLAUNCHER_PAD` or `{pad}`:** default your button prompts to the player's controller, `xbox` or `playstation`, so the first screen already shows the right buttons.
- **`RECOMPLAUNCHER`:** when it's `1`, skip your own launcher window. The launcher has already checked the player's copy and written your settings.

## Quit cleanly

When your game closes, the launcher backs up the save straight away. A "Quit" in your menu means a controller player never needs a keyboard to get back to their library.

## Background input

If your game can read controllers while it isn't in front (`SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS` in SDL), keep it off by default. The launcher's own overlay uses the controller too.

---

# Make it run its best

> Small things that make a recomp feel finished in the launcher. None are required; each one shows.

None of these are required. Each one makes your recomp feel finished to the player, and most take minutes.

## Good defaults

Start at the console's own look: its resolution, frame rate and aspect. Let players go up from there in your settings. A first start that just works beats one that asks questions.

## Settings the launcher can show

Keep your options in a TOML, INI or JSON file and describe the ones players care about in `settings` (Level 2): resolution, frame rate, anti-aliasing, V-Sync, window mode, upscaling. They show on the game's page and in Big Picture mode, with your help text, and players never open the file.

## High frame rates, done right

If you unlock the frame rate, say in the help text what it changes, since some games tie physics or cutscenes to it, and keep the console's rate as a choice.

## Saves where the launcher can find them

Say where they are with `saves` (Level 1). Backups and cloud saves then pick up exactly the right files, and nothing else.

## A log, and exit codes that explain themselves

- Write a `.log` in a `logs` folder, or next to your program. It goes with the crash report a player sends, so you get it without asking.
- Explain your exit codes with `exit_codes`. A clear "Your copy is the European release; this one needs the USA disc" saves you a bug report.

## Portable mode

If a `portable.txt` next to your program makes your recomp keep everything in its own folder, mod installs from the launcher follow it.

## Trust the check

With `game` and `dumps`, the launcher has already read and checked the player's copy before your program starts. Your own check can be quick.

## Keep the file in step

Ship `recomplauncher.json` in every release, and update it when you add or rename settings keys. The Developer tab in the launcher shows problems before players do (see **Testing and getting listed**).

---

# Testing and getting listed

> Check your file with the launcher's Developer tools, and get onto recomplauncher.com.

## Check your file

The launcher has tools for this. In Settings, open Developers and turn on Developer tools.

1. **Add your build.** Use "Add a build" and pick your recomp's folder, the one with your program and `recomplauncher.json`. It shows in your Library as a local build. Nothing is downloaded, and the launcher never updates, moves or deletes that folder.
2. **Read the check.** The build opens on its Developer tab. It reads your file fresh each time and lists what it found: where Play starts, what the setup screen asks for, each settings file and the choice each setting shows right now. Problems are marked in red, things worth a look in yellow, and JSON mistakes show the line and column. It also shows which level your file reaches. No file yet? "Make a starter file" writes one next to your program to begin from.
3. **Try it like a player.** Set it up with your own copy, change a setting, then open your settings file: only the keys you listed should have changed. Use "Change your copy" and check the setup screen turns down a different disc. Press Play: your game should start without your own launcher.
4. **Change the file and press Check again.** No restart needed.

Add `"name"` to your file and the local build goes by that name. A file the launcher can't read is skipped, and the built-in one (if any) is used instead.

## Your releases

The launcher installs from your GitHub releases. How it picks the right file, and how to keep updates from touching players' settings, is in **Releases that install cleanly**.

## Getting on recomplauncher.com

Use **Submit a game** in your hub, or on recomplauncher.com, with your GitHub repo. Recomps are picked by hand, so not everything sent in is listed. Once it's listed, claim it from your hub so it carries your name.

Stuck, or want us to write the file with you? Send it to us in Messages and we'll help.

---

# recomplauncher.json reference

> Every field on one page, with the level it belongs to.

Only `format` is required. Everything else is optional, and the launcher ignores fields it doesn't know.

| Field | Level | What it holds |
| --- | --- | --- |
| `$schema` | Any | `https://recomplauncher.com/schema/recomplauncher.json`, for your editor |
| `format` | Any | Always `1` |
| `repo` | Any | Your GitHub repo (owner/name). Only for files built into the launcher |
| `name` | Any | The name a local build goes by in the Developer tools |
| `game` | 1 | The player's copy: `kind`, `label`, `check`, `dumps` |
| `start` | 1 | What to run: `program`, `args`, `env` |
| `saves` | 1 | Where your saves are, for backups and cloud saves |
| `config` | 2 | Settings files: `id`, `format`, `file`, `game_path`, `ensure`, `always`, `new` |
| `settings` | 2 | Options for the player: `id`, `label`, `help`, `in`, `choices`, `defaults`, `when` |
| `looks` | 2 | `graphics`, `graphics_key`, `in`, `lines`, `allowed`, `reason` |
| `mods` | 2 | `thunderstore`, `data`, `gamebanana`, `loader`, `user` |
| `folders` | 2 | Named places; `saves` and `logs` are used by the launcher |
| `actions` | 2 | Buttons: `id`, `label`, `help`, `open` or `clear` |
| `exit_codes` | 2 | Exit code (as a string) to the message shown |
| `prepare` | 3 | Your setup step: `program`, `args`, `label`, `timeout`, `options` (ticks before it runs), `version` (change it when players must run it again), `done` (files that exist once it has finished) |

The full schema, with every value checked, is at https://recomplauncher.com/schema/recomplauncher.json.

---

# Your developer hub

> Claiming your work, your press kit, messages and the Lounge.

The developer hub is where you look after your apps and recomps once they're listed. It's the same hub on recompstore.com and recomplauncher.com.

## Say what you make

The first time you open your hub, pick PC recomps, PS5 apps or both. The hub shows the tabs and docs that matter for each. Change it any time with **Change** next to your name.

## Claim your work

At the top of **Your apps** and **PC recomps**, everything listed from your GitHub is one click to claim, and it's yours straight away. Made one that lives somewhere else, like a fork or a friend's account? Search for it, say how it's yours, and we'll check. The answer arrives in your messages. Claimed apps carry your name and a link to your developer page.

## Edit your pages

Change the summary, description, features and pictures. Verified developers' changes go up straight away; others are checked first. You can also say whether AI tools helped make it. That's your word, shown on its page.

## Your press kit

The **Press kit** tab has badges made for each of your apps: Get it on RecompStore and Get it on RecompLauncher, in dark or light. Copy them as Markdown for your README, HTML for a website or BBCode for forums. Each links straight to its page. Verified developers also get the verified badge, which links to their developer page.

A few rules: write the names as one word, RecompStore and RecompLauncher; use the badges as they are; and give them room.

## Messages and the Lounge

- **The team.** A private conversation with us. It's open even while your account waits for approval.
- **The Lounge.** Channels every approved developer shares: `#general`, `#showcase` for what you're working on, and `#pc-recomps` or `#ps5-apps` for what you make.

## Docs

The **Docs** tab, where you're reading this. Every doc can be downloaded as Markdown, to keep or to hand to an AI assistant, or saved as a PDF.
