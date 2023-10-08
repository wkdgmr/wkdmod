# wkdmod is a fork of DevilutionX
## wkdmod aims to implement more quality of life changes that are deemed out of scope for DevX.  This modified version of DevilutionX focuses on a more modern and casual approach to ARPG gameplay and mechanics.

**[CREDITS]**

This mod was created by <a href="https://www.patreon.com/wkd1337gmr/membership" target="_blank" rel="noopener noreferrer">wkd1337gmr</a>.

Thank you to the invaluable efforts of the DevilutionX community going above and beyond whenever I needed assistance during my journey creating this mod!  The following handles contributed to my mod via either direct or indirect code consultation/contribution or game testing:

- Ajenbo
- staphen
- Qndel
- kphoeniX
- DakkJaniels
- ikonomov
- Knight of NSFW
- Brewbeck
- Entkoffeinator
- Tree
- Belix (wkdmod is inspired by the efforts of Hellfire modders that came before me)
- Toothless

(click the banner for current intro trailer)
<a href="https://www.youtube.com/watch?v=qGFhbjFcxxQ" target="_blank" rel="noopener noreferrer">
<img src="https://github.com/wkdgmr/wkdmod/blob/1e45674d564003a906c145185e141bda8ed285d9/wkdmodbanner.jpg" alt="wkdmod banner" width="838">
</a>

<a href="https://www.youtube.com/watch?v=6lJHouylAdA" target="_blank" rel="noopener noreferrer">wkdmod gameplay trailer</a>

wkdmod is compatible* with standard DevilutionX characters. Simply copy your character data and stash data into the **devxmods/wkdmod folder in %appdata%/roaming/**

***WARNING**
1) Unique Items:  All Unique Items are now available and have been extensively overhauled.  Ice Shank and Hammer of Johlm have been replaced with the Amulet of Empowerment and Tsang's Blessed Hammer. **(Unique's are not forwards or backwards compatible from standard devx/vanilla characters.  If brought into or out of wkdmod they could cause game crashing/character save corruption).**  If you plan on bringing over your character and stash, make sure you make a copy of this stuff and before bringing in the character load up the save and toss out all of your uniques.  There have been many new uniques and effects added.  Play the game to discover them all!

2) Magic/Blue Items:  In Hell the player will be able to find all affixes in the dungeon.  There are no longer any items that "can't" be found. **(Magic/Blue Items are intended to be forwards compatible into wkdmod.  There may still be potentially a small percentage of these items that could morph... it's just the nature of what happens when you bring stuff into a mod)**

3) Oils and Scrolls are **NOT** forwards compatible into wkdmod.

So in summary, what can be expected to be forwards compatible are: 
**--** Character saves with only blue items
**--** Stashes with gold, blue items, elixirs, books.

Don't forget, you can always start fresh as well if you'd like!

Please report bugs in the #wkdmod channel in the DevilutionX discord, underneath the wkdmod bug reports thread.  Or use github bug reports on the wkdmod github repo.

### **DevX Release 1.5.x** with additional **NEW** features:

**[GLOBAL]**

- When Diablo is killed the game does not end and he drops loot (drops 6 items).
- Buffed loot drop chances 300% in Hell, 200% in Nightmare, Normal is unchanged.
- When killing Diablo, Nakrul, Hork Demon or Defiler loot drop tiers are determined by difficulty.  Best drops from these bosses will be found on Hell difficulty.
- Diablo and Nakrul have been buffed and are much more difficult to defeat.

- All magic affixes including the previous unfindable ones are now available to find in the dungeon
- All unique items can now be found (including brand new never before seen uniques and unique item effects)

- Nightmare and Hell monster HP/AC/ToHit has been increased
- previous Triple Immunes now have RESISTANCE TO MAGIC and can be hit by magic spells
- Nightmare and Hell difficulties have been buffed significantly

- MLVL's have been increased in Nightmare and Hell to allow for slightly better affix pools in the higher difficulties.
- Wirt's shop price cap has been raised from 200,000 to 300,000. 
- Items are no longer destroyed at 0 durability, they become disabled and you must repair.
- Players do not drop items on death, unless you enable **INVADER** mode, more details below.  Death Penalty: -25% xp loss of current level progress and items take 50% of their max durability in damage.

- Shared XP in multiplayer games.
- Character's now have a natural random chance to dodge incoming missiles.  This scales with character level.
- Character's now gain a passive spell dodge affect based on resistances to that spell element.

- Black Deaths have a decreasing chance based on character level to steal 1 Vit point from the player, and they have been given the same LIFESTEAL mechanic as the Skeleton King.
- **CRIPPLING SHRINES EFFECTS HAVE BEEN REMOVED:**
  - shrines that increase spell levels and did -10% mana now do -2 to the magic stat
  - Spiritual Shrines gold pile amounts per difficulty are increase by x 20
  - Stone Shrine now increases equipped Staff Charges by a random amount based on character level as well as refilling them
  - Gloomy Shrine can't increase AC past 90 (balance fix for wkdmod)
  - Weird Shrine cannot increase weapon max damage past weapon maximums set in wkdmod, will also increase elemental damage if present
  **- HELLFIRE Shrines**
  - Glowing Shrine now increases magic randomly between 1 - 5 and gives XP between 1000 - 5000, removed penalty
  - Murphy's Shrine now has a 50% chance to increase a random items durability using the Oil of Fortitude logic and give you money using the Spiritual  Shrine logic, the downsides remain the same
  - This should make it so wkdmod players no longer need to use the disable crippling shrines option as all negative effects are reversible with oils or elixirs (or even other shrines)

- Warrior, Barbarian, and Bard have all received a melee and ranged ToHit buff and are no longer desperate for Dexterity/ToHit gear.
- Barbarian no longer takes damage from the Rage skill, it will drain mana instead if the Barbarian has any
- Buffed Barbarian's passive bonuses to include 2H Axes, 2H Maces, Staffs, 2H Swords
- Buffed Bard's passive bonuses with dual wielding any combination of 1H weapons
- Bard is now buffed when wearing Unique, Medium, and Light Armor.  However receives a debuff when wearing non-Unique Heavy Armor (same as Monk).
- Golem has been buffed significantly and can no longer take Friendly Fire damage.
- Rogue gains +100 on Normal, +500 on Nightmare, or +1000 on Hell experience points when disarming a trap
- The Thorns affix has been reworked into Holy Fire.  Holy Fire damage can be boosted by elemental damage Oils and will do full additive damage from any equipped Holy Fire source to any Monster that melee hits the player (Holy Shock has replaced Flash, Exploding Bone Armor added as special item empowered effect).
- The Damage% line of affixes have been buffed, ending with Merciless which now can get up to 250% damage (151 - 250).
- The +Damage line of affixes have been buffed, ending with Slaughter, which now can get up to +50 damage (17 - 50).
- all Lifesteal and Manasteal affixes are now additive
- Nerfed Apocalypse in Diablo so it acts like its Hellfire counterpart
- Swapped some Scrolls out for the Oils I added, nobody will miss them

**UNIQUE ITEM OVERHAUL**
- Quest uniques have been brought in line with how easy they are to obtain, but mostly have also received buffs (a small handful nerfed) and can be farmed for better/preferred rolls on stats
- Some items will seem to have more punishing curses on them, but this is only because of how powerful they are and the curses are really there to make it a dangerous item for under leveled characters
- Some Uniques are simply just going to be better than others, no way to avoid that.  But hopefully everything is at least useful if found in a playthrough even if not the most optimal piece in the long run... also trying to maintain theme of the items as well
- There have been a ton of unique items given spell-on-strike and bows that shoot spells... explore the dungeon to find these new powerful weapons!
- New Item effect: Empowerment (Can be found on the Amulet of Empowerment (Hellfire) and/or the Amulet of the Horadrim (Diablo/Hellfire))
Many unique items have new EMPOWERED effects when combined with either of the two new powerful amulets waiting for you to discover them!

**SPELL AND ELEMENTAL WEAPON DAMAGE BALANCING**
- Holy Bolt is now Magic Missile
- Flash is now Holy Shock (even though its one of the new auto cast effects, it can still be manually cast as well)
- Magic Missile, Blood Star and Bone Spirit have had damage buffs in line with their balance in the game and Blood Star/Bone Spirit should be considered equivalent damage to Fireball
    - Bone Spirit being a significantly stronger spell for high magic users and it is more then triple the mana cost of Blood Star) (edited)
- Firebolt (+Guardian), Charged Bolt, Inferno and Lightning have had slight damage buffs to scale with character level, and these spells pierce enemy immunes and damage is resisted instead of ignored.
- Lightning and Fire WEAPON damage pierce enemy immunes and damage is resisted instead of ignored.  This includes the special Hellfire weapons that cast spells based on Weapon element damage.
- Mana shield has been rebalanced as well and no longer redirects 100% of damage to mana

**OILS**
- All available Oils now drop in Classic and Hellfire multiplayer mode as well as singleplayer mode and added the following NEW Oils with NEW changes to 
  Accuracy, Mastery, Hardening, Imperviousness, Sharpening, Death, Permanence (note Blacksmith Oil has been removed):
- Oil of Sharpening:  +1 minimum dmg, +2 maximum damage, with 15/20/30/40 being the ceiling for minimum dmg for bows/1h/2h/Axe respectively / Can never exceed Maximum Damage
- Oil of Death:  +2 minimum dmg, +4 maximum damage, with 30/35/60/80 being the ceiling for maximum dmg for bows/1h/2h/Axe respectively 
- Oil of Accuracy: Can now increase passive or active ToHit bonus on a weapon to 60
- Oil of Mastery: Can now increase passive or active ToHit bonus on a weapon to 120
- Oil of Hardening: Can now increase item AC to a maximum of 60
- Oil of Imperviousness: Can now increase item AC to a maximum of 90
- Oil of Permanence: 10% Chance to make an item Indestructible, will increase item durability slightly on fail only if item durability is 200 or greater.  If increasing an item above 255 with Permeance it will automatically become Indestructible.
- Oil of the Horadrim (small): +2 minimum/+4 maximum fire/lightning dmg, with a ceiling of 200 minimum fire/lightning damage / Can never exceed maximum fire/lightning damage
- Oil of the Horadrim (large:) +4 minimum/+8 maximum fire/lightning dmg, with a ceiling of 200 maximum fire/lightning damage

**Splash damage now does damage to two extra tiles** (so its more like a U instead of a T, the shape of the splash), and can be accessed with the following classes/weapons (and thanks to DevX is applied in both Diablo and Hellfire gamemodes):
- **Monk:** Staff, 2H Sword
- **Warrior:** 2H Sword, 1H Sword + Shield, 1H Mace + Shield (must have Shield equipped for splash to work with 1H)
- **Barbarian:** 2H Sword, 2H Axe, Staff, 2H Mace
- **Bard:** 2x 1H Sword, 2x 1H Mace, or 1H Sword + 1H Mace
- **Sorcerer:** Staff
- **WEAPONS EXPERT** (unlocked at Lvl 40) All classes gain access to splash damage on all melee weapons and barehanded.

**[HELLFIRE ONLY]**
- Auric Amulet can now be held in your inventory to receive the double gold pile capacity bonus and you no longer have to equip it.
- Monk is now buffed when wearing Unique, Medium, and Light Armor.  However receives a debuff when wearing non-Unique Heavy Armor (same as Bard).
- Monk gets fastest attack with unique staffs at level 30 while wearing an empowered affix amulet
- Buffed Monk's abilities with barehanded and 2H Sword
- Monk now has Infravision as a class skill
- Search Books and Search Scrolls no longer drop
- Hork Demon amulet drop is now a higher item level depending on difficulty (Normal: ilvl 15, Nightmare: ilvl 20, Hell: ilvl 30) and he drops more or less items depending on difficulty.
- Hellfire spell blocking has been completely removed.
- Hork Spawn now drop items like normal monsters.
- Fixed Hellfire Arrow behavior to be like Diablo
- Gnat Sting now always properly fires 3 arrows in a strafe pattern instead of bugging out and only firing one arrow if the player tries to attack too quickly.
- Runes have been disabled

**[INVADER]**
- INVADER is a stand alone game mode within wkdmod.  It's just your standard wkdmod Diablo or Hellfire... but with a twist...
- If you toggle Friendly Fire ON within wkdmod, you will no longer have access to your normal characters and stash.  Instead, you will have access to your INVADER characters and stash.  
- INVADER characters and stash ARE NOT MEANT TO BE BACKED UP.  This is not enforceable, but if you play this mode and back up your files you are a wussy... especially because I already give you the chance to play with FF off and not lose your shit!
- INVADER characters are meant to be played in public games, and are encouraged to kill each other and take each others items.

### Please see below this point for more information on the DevilutionX project.

<p align="center">
<img width="554" src="https://user-images.githubusercontent.com/204594/113575181-c946a400-961d-11eb-8347-a8829fa3830c.png">
</p>

---

[![Discord Channel](https://img.shields.io/discord/518540764754608128?color=%237289DA&logo=discord&logoColor=%23FFFFFF)](https://discord.gg/devilutionx)
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

Note: You'll need access to the data from the original game. If you don't have an original CD then you can [buy Diablo from GoG.com](https://www.gog.com/game/diablo). Alternately you can use `spawn.mpq` from the [shareware](https://github.com/diasurgical/devilutionx-assets/releases/latest/download/spawn.mpq) [[2]](http://ftp.blizzard.com/pub/demos/diablosw.exe) version, in place of `DIABDAT.MPQ`, to play the shareware portion of the game.

Download the latest [DevilutionX release](https://github.com/diasurgical/devilutionX/releases/latest) and extract the contents to a location of your choosing or [build from source](#building-from-source).

- Copy `DIABDAT.MPQ` from the CD or GOG-installation (or [extract it from the GoG installer](https://github.com/diasurgical/devilutionX/wiki/Extracting-the-.MPQs-from-the-GoG-installer)) to the DevilutionX folder.
- To run the Diablo: Hellfire expansion you will need to also copy `hellfire.mpq`, `hfmonk.mpq`, `hfmusic.mpq`, `hfvoice.mpq`.

For more detailed instructions: [Installation Instructions](./docs/installing.md).

# Contributing

We are always looking for more people to help with [coding](docs/CONTRIBUTING.md), [documentation](https://github.com/diasurgical/devilutionX/wiki), testing the [latest builds](https://app.circleci.com/pipelines/github/diasurgical/devilutionX?branch=master), spreading the word, or simply just hanging out on [the chat](https://discord.gg/devilutionx).

# Mods

We hope to provide a good starting point for mods, in addition to the full Devilution source code we also provide modding tools. Also, check out the list of known [mods based on DevilutionX](https://github.com/diasurgical/devilutionX/wiki/Mods-and-related-projects).

# Test builds

If you want to help test the latest development stage of the next version (make sure to backup your files as these may contain bugs), you can fetch the test build artifact from one of the build server:

*Note: You must be logged into GitHub to download the attachments!*

[![Linux x86_64](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_x86_64.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_x86_64.yml?query=branch%3Amaster)
[![Linux aarch64](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_aarch64.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_aarch64.yml?query=branch%3Amaster)
[![Linux x86](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_x86.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_x86.yml?query=branch%3Amaster)
[![Linux x86_64 SDL1](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_x86_64_SDL1.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Linux_x86_64_SDL1.yml?query=branch%3Amaster)
[![macOS x86_64](https://github.com/diasurgical/devilutionX/actions/workflows/macOS_x86_64.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/macOS_x86_64.yml?query=branch%3Amaster)
[![Windows x64](https://github.com/diasurgical/devilutionX/actions/workflows/Windows_MSVC_x64.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Windows_MSVC_x64.yml?query=branch%3Amaster)
[![Windows MinGW x64](https://github.com/diasurgical/devilutionX/actions/workflows/Windows_MinGW_x64.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Windows_MinGW_x64.yml?query=branch%3Amaster)
[![Windows MinGW x86](https://github.com/diasurgical/devilutionX/actions/workflows/Windows_MinGW_x86.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Windows_MinGW_x86.yml?query=branch%3Amaster)
[![Android](https://github.com/diasurgical/devilutionX/actions/workflows/Android.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/Android.yml?query=branch%3Amaster)
[![iOS](https://github.com/diasurgical/devilutionX/actions/workflows/iOS.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/iOS.yml?query=branch%3Amaster)
[![PS4](https://github.com/diasurgical/devilutionX/actions/workflows/PS4.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/PS4.yml?query=branch%3Amaster)
[![Original Xbox](https://github.com/diasurgical/devilutionX/actions/workflows/xbox_nxdk.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/xbox_nxdk.yml?query=branch%3Amaster)
[![Xbox One/Series](https://github.com/diasurgical/devilutionX/actions/workflows/xbox_one.yml/badge.svg)](https://github.com/diasurgical/devilutionX/actions/workflows/xbox_one.yml?query=branch%3Amaster)

Switch, Vita, 3DS, Amiga: [![CircleCI](https://circleci.com/gh/diasurgical/devilutionX.svg?style=shield)](https://app.circleci.com/pipelines/github/diasurgical/devilutionX?branch=master)

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
