<p align="center">
  <img src="docs/images/arcade-lan-header.jpg" alt="Namco 22: Arcade LAN - four linked Tokyo Wars cabinets" width="100%">
</p>

# Namco 22: Arcade LAN

**A fork of [namco22-decompile](https://github.com/spacestate1/namco22-decompile) by
[spacestate1](https://github.com/spacestate1), made for real arcade cabinets.**

namco22-decompile rebuilds Namco's 1990s arcade games (System 21, 22 and 23) so they run natively on a PC. All of that
work is spacestate1's. This fork adds one thing on top: an **Arcade LAN**, so cabinets that have a PC in place of
their original boards link over a wired network the way the boards linked over their cable. They link when switched
on, take coins as normal, and need no menus. Huge respect and thanks to spacestate1; please star and support
[the original project](https://github.com/spacestate1/namco22-decompile), and use it for playing at home or online.

**You need your own copy of the games.** This repository contains no ROMs or game data.

> **Project status:** a hobby project for restoring real cabinets, with no regular updates or support. The link has
> been tested with up to four PCs on a home network and with up to four copies on one PC. It has not yet run in a full
> arcade installation. Bug reports and pull requests are welcome but may not get picked up. You are welcome to fork it
> under the same licence (MIT).

## What the Arcade LAN adds

### Link play for the cabinets

| Game | Cabinets | Status |
|---|---|---|
| **Tokyo Wars** | up to 4 | Linked battles, team play; tested with 4 |
| **Rave Racer** | up to 4 | Linked races; tested with 4 |
| **Time Crisis II** | 2 (the twin cabinet) | Linked LINK PLAY; one known issue, see below |
| **Cyber Cycles** | up to 4 | A new port, on the `cybrcycc-feasibility` branch: plays, links 2 or 4 bikes |

- **Links at power-on, like the boards.** Each cabinet looks for the others when its game starts, and only then. When
  all are there, or the start-up wait runs out, they begin together. A cabinet that starts late, or restarts after a
  crash, plays on its own until the whole set is restarted: the same rule as the real hardware.
- **No menus to play.** Coins and Start work as on the original machines. Each game's own test-menu settings (cabinet
  ID, team colour, car type and so on) are kept per cabinet.
- **LAN only.** Nothing uses the internet. **Find cabinets** on the Network page fills in every PC's address by itself.
- **Safety net.** If its partners disappear, a cabinet shows *LINK LOST - RESTARTING* and starts again, alone, instead
  of being left in a battle nobody else is in (or carries on, if you prefer).
- **Link lights**: small status lights in a corner of the screen, one per cabinet, for setting up.
- **The same rules as the chips.** The link is a model of Namco's C139 link chip (and the C422 for Time Crisis II),
  read from each game's own driver, so the games run their original link code unchanged.

<p align="center">
  <img src="docs/images/tokyowar-link-4cab.jpg" alt="Tokyo Wars: the four cabinets' screens during one linked battle" width="80%">
</p>

### Also in this fork

- **Party mode**: two or four linked copies on one PC as a split screen, one player per controller (or the keyboard).
- **Session recordings on or off**: each game keeps the inputs of the last session played, so a problem can be
  replayed exactly. `record=off` in `link.cfg` (or the kit menu's Tools page) turns that off.
- **Esc only opens and closes the menu** (the original's double-Esc quits, which an arcade does not want).
- **Fix**: quick clicks and touchpad taps in the menus are no longer lost.

## Version history

The fork follows the original project's releases; each is merged in and the link tests are run again.

- **0.9.2** (October 2026): merged the original 0.9.1 and 0.9.2 (16:9 widescreen, Rave Racer session recordings, a
  geometry fix). Adds the session recording switch; the Cyber Cycles port is on its own branch.
- **0.9.0**: Time Crisis II twin-cabinet link, one gun per screen with both cabinets on one PC, link lights for every game.
- **0.7.0**: party mode, *Arcade mode* (the original's online pages hidden, kept for later merges).
- **0.5.4**: the frame timing choice (the original's new timer or 0.4.0's).
- **0.4.0**: the first Arcade LAN: Tokyo Wars and Rave Racer, the Network page, Find cabinets, the safety net.

## Requirements

- One PC per cabinet with Windows 10 or 11 (64-bit) and working OpenGL. Modest PCs are fine.
- **A wired network**: a simple switch and cables. The games expect every cabinet to answer within about a tenth of a
  second, all the time. Wi-Fi reconnects and power saving break that.
- **Full screen** on each cabinet. In a window, a PC with modest graphics can freeze for a moment, which can drop the
  link with three or more cabinets.
- The same build on every cabinet, and Windows Firewall allowing it on the cabinets' network (*Private*).
- Your own game files: MAME ROM sets, 0.271 or later (`tokyowar.zip`; `raverace.zip` + `namcoc74.zip`;
  `timecrs2.zip`; `cybrcycc` for Cyber Cycles).

## Setting up

1. Build the games (below) and put a kit together:
   `tools/make_arcade_kit.sh <kit folder> <folder with your MAME zips>`. For Time Crisis II, also unpack its files with
   `tc2/tools/setup_roms.py` into `<kit>/Time Crisis 2/extracted`.
2. Copy the kit folder to every cabinet PC.
3. On every PC, start the game with its **FULL SCREEN** launcher (or `0 - START HERE - choose a game.bat`). On each
   one, press **Esc**, open **Network**, choose **Find cabinets**, then **Use the N found**. That fills in the address
   list (the same on every PC) with *This cabinet* on *Auto*.
4. Close the game on every PC and start them all again. They wait up to a minute for each other, then link.

The kit's `READ ME - LINK TEST.txt` walks through every launcher, testing on one PC first, and what each message
means. [docs/ARCADE_LAN.md](docs/ARCADE_LAN.md) describes the link in more detail.

## Settings

The link settings are in `link.cfg` beside the program; the **Network** page in the Esc menu edits them (Time Crisis
II has its own, `Time Crisis 2\tc2_link.cfg`).

| Setting | |
|---|---|
| `link` | `on` or `off` |
| `cabinet` | `auto` (its place in the address list) or a number |
| `cabinets`, `address1` ... `address8` | how many cabinets, and each one's address |
| `wait` | seconds to wait at power-on for a missing cabinet (default 60) |
| `lights` | the link lights: `off`, `bottom-left` (default), `bottom-centre`, `bottom-right`, `top-left`, ... |
| `if_lost` | `restart` (default) or `carry_on` when the partners are gone |
| `timing` | `new` (the original's timer) or `classic` (0.4.0's) |
| `record` | `on` (default) or `off`: the last session's inputs, `<game>_last.rec` |
| `arcade_mode` | `on` (default) hides the original's online pages; `off` brings them back |

## Known issues

- **Time Crisis II**: if one player CONTINUEs while the other is still playing, the other cabinet treats them as a
  computer partner until its game restarts. Fresh joins and new games work. (Work on this is paused.)
- Rare: when a cabinet is switched off without warning, the others may show a link blip of about a third of a second
  before carrying on.
- Not yet tested with real controllers in party mode, or in a full arcade installation.

## Building

On Windows, in an MSYS2 *MINGW64* shell with `mingw-w64-x86_64-toolchain`, `-cmake`, `-ninja`, `-pkgconf`, `-SDL2`,
`-zlib`, `-python` and `-python-numpy` installed. Each game's ROM files must be unpacked in its `extracted/` folder first:
its sound and DSP programs are translated from them at build time (see the original README).

```
cmake -S tokyowar -B tokyowar/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DZLIB_LIBRARY=/mingw64/lib/libz.a
ninja -C tokyowar/build          # the same for raverace and tc2 (Time Crisis II's first build takes ~20 minutes)
```

The link tests run four copies on one PC over 127.0.0.1, with network faults injected:
`tokyowar/tools/link_tests.sh` and `raverace/tools/link_tests.sh` (`gather`, `missing`, `bad`, `skew`, `crash`).

## How it works

Each original board has a link chip (Namco's C139; the C422 on System 23) joined to the next cabinet's in a ring.
`engine/lan139.c` is a model of the chip at the level of whole frames, written from how each game's own link driver uses
it: a frame the game sends goes to the next cabinet as one UDP packet (`engine/link_net.c`), and a frame received is
put in the chip's buffer and the game is interrupted, as on the board. `engine/link_host.c` handles start-up (finding the
others, the power-on wait, the fixed set), the Network page, `link.cfg` and the safety net. Notes on each game's link
protocol are in [docs/LINK_NOTES.md](docs/LINK_NOTES.md). The original author's own online play (`engine/net.c` and
friends) is kept as it is, hidden by *Arcade mode*, so their releases keep merging cleanly.

## Licence and legal

MIT License; see `LICENSE`. Copyright of the original project is its author's; this fork's additions are under the same
licence.

This project is not affiliated with or endorsed by Bandai Namco. Tokyo Wars, Rave Racer, Time Crisis and Cyber Cycles
are trademarks of Bandai Namco Entertainment Inc. You need your own legally obtained copy of each game.

## Credits

- **[spacestate1](https://github.com/spacestate1)** for [namco22-decompile](https://github.com/spacestate1/namco22-decompile),
  the rebuilt games this fork is built on, and the long reverse-engineering work behind them. This fork would not exist
  without it. Their README is kept in [docs/ORIGINAL_README.md](docs/ORIGINAL_README.md).
- **Claude** (Anthropic's AI, through Claude Code) wrote the Arcade LAN changes.
- [MAME](https://www.mamedev.org) served as the reference for how the boards behave, and [Ghidra](https://ghidra-sre.org)
  made the analysis of the games' link drivers possible.
- Namco, for the games and the cabinets.
