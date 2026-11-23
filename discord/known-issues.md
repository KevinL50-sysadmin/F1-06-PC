# F1 06 PC: known issues shown in Discord #known-issues

How it works: every line starting with "- " is one issue (indented lines continue it). Anything else,
like these lines, is ignored. Add "(fixed-in: X.Y.Z)" at the end of an issue when a fix lands: until X.Y.Z is
released Discord shows "Fixed in X.Y.Z", after that the issue disappears. Write #channel-name for a channel.
English only, no em or en dashes. Discord is updated by the release workflow.

- Older workstation CPUs and low-power laptop CPUs run races below 50 fps (in slow motion, not with skipped frames).
- With Windows' *Balanced* power plan and background programs, the 22-car race start can drop to about 42-45 fps for
  a few seconds: use *High performance*.
- CAR DETAIL HIGH costs about 16 % CPU at the start; MAXIMUM runs at about 25 fps there. AS ON PS2 is the default.
- Custom driving controls (PC CONTROLS) only apply in Quick Race. (fixed-in: 1.2.0)
- Linux/Proton: the game can be silent because some copied game files come out damaged. (fixed-in: 1.2.0)
- Capture/streaming tools such as Sunshine can cause stutter in menus.
- Netplay is experimental: 2 players, signed but not encrypted.
- No 60 fps mode. The `.exe` is not code-signed (SmartScreen warning).
