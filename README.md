# wkdmod is a fork of DevilutionX
## wkdmod aims to implement more quality of life changes that are deemed out of scope for DevX.  This modified version of DevilutionX focuses on a more modern and casual approach to ARPG gameplay and mechanics.

<video>
  <source src="https://www.youtube.com/watch?v=qGFhbjFcxxQ" type="video/mp4">
Your browser does not support the video tag.
</video>

wkdmod is compatible with standard DevilutionX characters. Simply copy your character data and stash data into the wkdgmr/wkdmod folder in %appdata%/roaming/

wkdmod is compatible with Diablo & Hellfire game modes within DevilutionX.

### **DevX Master** (as of 4/2/2023) with additional **NEW** features:

- When Diablo is killed the game does not end and he drops loot (drops 5 items).
- When killing Diablo, Nakrul, Hork Demon or Defiler loot drop tiers are determined by difficulty.  Best drops from these bosses will be found on Hell difficulty.
- Buffed Nakrul drops slightly (drops 7 items).
- Buffed Defiler Drops (drops 4 items)
- Hork Demon amulet drop is now a higher item level depending on difficulty (Normal: ilvl 15, Nightmare: ilvl 20, Hell: ilvl 30) and he drops one extra random item.
- Items are no longer destroyed at 0 durability, they become disabled and you must repair.
- Players do not drop items on death (you will still drop gold if it is on your character and not in the stash).
- Shared XP in multiplayer games.
- Black Deaths no longer permanently damage your character, HOWEVER, they have been given the same LIFESTEAL mechanic as the Skeleton King.
- Warrior, Barbarian, and Bard have all received a melee ToHit buff and are no longer desperate for Dexterity/ToHit gear.
- Warrior and Bard have received a buff to ranged ToHit as well.
- Hellfire spell blocking has been completely removed.

**Splash damage now does damage to two extra tiles** (so its more like a U instead of a T, the shape of the splash), and can be accessed with the following classes/weapons:
- **Monk:** Staff, 2H Sword
- **Warrior:** 2H Sword, 1H Sword + Shield, 1H Mace + Shield (must have Shield equipped for splash to work with 1H)
- **Barbarian:** 2H Sword, 2H Axe, Staff, 2H Mace
- **Bard:** 2x 1H Sword, 2x 1H Mace, or 1H Sword + 1H Mace
- **Sorcerer:** Staff

**AT LEVEL 40 ALL CLASSES UNLOCK WEAPONS EXPERT WHICH GRANTS UNRESTRICTED SPLASH FOR ALL WEAPONS INCLUDING BAREHANDED!!!**

### Please see below this point for more information on the DevilutionX project.

<p align="center">
<img width="554" src="https://user-images.githubusercontent.com/204594/113575181-c946a400-961d-11eb-8347-a8829fa3830c.png">
</p>

---

[![Discord Channel](https://img.shields.io/discord/518540764754608128?color=%237289DA&logo=discord&logoColor=%23FFFFFF)](https://discord.gg/YQKCAYQ)
[![Downloads](https://img.shields.io/github/downloads/diasurgical/devilutionX/total.svg)](https://github.com/diasurgical/devilutionX/releases/latest)
[![Codecov](https://codecov.io/gh/diasurgical/devilutionX/branch/master/graph/badge.svg)](https://codecov.io/gh/diasurgical/devilutionX)

<p align="center">
<img width="838" src="https://user-images.githubusercontent.com/204594/113578478-26912400-9623-11eb-9ff6-9bd9717462b6.png">
</p>

<sub>*(The health-bar and XP-bar are off by default, but can be enabled in the [game settings](https://github.com/diasurgical/devilutionX/wiki/DevilutionX-diablo.ini-configuration-guide). Widescreen can also be disabled if preferred)*</sub>

# What is DevilutionX

DevilutionX is a port of Diablo and Hellfire that strives to make it simple to run the game while providing engine improvements, bugfixes, and some optional quality of life features.

Check out the [manual](https://github.com/diasurgical/devilutionX/wiki) for what features are available and how best to take advantage of them.

For a full list of changes see our [changelog](docs/CHANGELOG.md).

# How to Install

Note: You'll need access to the data from the original game. If you don't have an original CD then you can [buy Diablo from GoG.com](https://www.gog.com/game/diablo). Alternately you can use `spawn.mpq` from the [shareware](https://github.com/diasurgical/devilutionx-assets/releases/download/v2/spawn.mpq) [[2]](http://ftp.blizzard.com/pub/demos/diablosw.exe) version, in place of `DIABDAT.MPQ`, to play the shareware portion of the game.

Download the latest [DevilutionX release](https://github.com/diasurgical/devilutionX/releases/latest) and extract the contents to a location of your choosing or [build from source](#building-from-source).

- Copy `DIABDAT.MPQ` from the CD or GOG-installation (or [extract it from the GoG installer](https://github.com/diasurgical/devilutionX/wiki/Extracting-the-.MPQs-from-the-GoG-installer)) to the DevilutionX folder.
- To run the Diablo: Hellfire expansion you will need to also copy `hellfire.mpq`, `hfmonk.mpq`, `hfmusic.mpq`, `hfvoice.mpq`.

For more detailed instructions: [Installation Instructions](./docs/installing.md).

# Contributing

We are always looking for more people to help with [coding](docs/CONTRIBUTING.md), [documentation](https://github.com/diasurgical/devilutionX/wiki), testing the [latest builds](https://app.circleci.com/pipelines/github/diasurgical/devilutionX?branch=master), spreading the word, or simply just hanging out on [the chat](https://discord.gg/YQKCAYQ).

# Mods

We hope to provide a good starting point for mods, in addition to the full Devilution source code we also provide modding tools. Also, check out the list of known [mods based on DevilutionX](https://github.com/diasurgical/devilutionX/wiki/Mods-and-related-projects).

# Test builds

If you want to help test the latest development stage of the next version (make sure to backup your files as these may contain bugs), you can fetch the test build artifact from one of the build server:

*Note: You must be logged into GitHub to download the attachments!*

[![Linux x86](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_x86.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_x86.yml?query=branch%3Amaster)
[![Linux x86-64 SDL1](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_x86_64_SDL1.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_x86_64_SDL1.yml?query=branch%3Amaster)
[![Linux aarch64](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_aarch64.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_aarch64.yml?query=branch%3Amaster)
[![MacOSX](https://github.com/diasurgical/devilutionX/actions/workflows/MacOSX.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/MacOSX.yml?query=branch%3Amaster)
[![Windows x64](https://github.com/diasurgical/devilutionX/actions/workflows/Windows_MSVC_x64.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Windows_MSVC_x64.yml?query=branch%3Amaster)
[![Windows MinGW x64](https://github.com/diasurgical/devilutionX/actions/workflows/Windows_MinGW_x64.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Windows_MinGW_x64.yml?query=branch%3Amaster)
[![Windows MinGW x86](https://github.com/diasurgical/devilutionX/actions/workflows/Windows_MinGW_x86.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Windows_MinGW_x86.yml?query=branch%3Amaster)
[![Android](https://github.com/diasurgical/devilutionX/actions/workflows/Android.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Android.yml?query=branch%3Amaster)
[![iOS](https://github.com/diasurgical/devilutionX/actions/workflows/iOS.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/iOS.yml?query=branch%3Amaster)
[![PS4](https://github.com/diasurgical/devilutionX/actions/workflows/PS4.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/PS4.yml?query=branch%3Amaster)
[![Original Xbox](https://github.com/diasurgical/devilutionX/actions/workflows/xbox_nxdk.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/xbox_nxdk.yml?query=branch%3Amaster)
[![Xbox One/Series](https://github.com/diasurgical/devilutionX/actions/workflows/xbox_one.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/xbox_one.yml?query=branch%3Amaster)

Linux x86-64, Switch, Vita, 3DS, Amiga, [![CircleCI](https://circleci.com/gh/diasurgical/devilutionX.svg?style=shield)](https://app.circleci.com/pipelines/github/diasurgical/devilutionX?branch=master)

# Building from Source

Want to compile the program by yourself? Great! Simply follow the [build instructions](./docs/building.md).

# Credits

- The original Devilution project [Devilution](https://github.com/diasurgical/devilution#credits)
- [Everyone](https://github.com/diasurgical/devilutionX/graphs/contributors) who worked on Devilution/DevilutionX
- [Nikolay Popov](https://www.instagram.com/nikolaypopovz/) for UI and graphics
- [WiAParker](https://wiaparker.pl/projekty/diablo-hellfire/) for the Polish voice pack
- And thanks to all who support the project, report bugs, and help spread the word ❤️

# Legal

DevilutionX is made publicly available and released under the Sustainable Use License (see [LICENSE](LICENSE.md))

The source code in this repository is for non-commercial use only. If you use the source code you may not charge others for access to it or any derivative work thereof.

Diablo® - Copyright © 1996 Blizzard Entertainment, Inc. All rights reserved. Diablo and Blizzard Entertainment are trademarks or registered trademarks of Blizzard Entertainment, Inc. in the U.S. and/or other countries.

DevilutionX and any of its maintainers are in no way associated with or endorsed by Blizzard Entertainment®.
