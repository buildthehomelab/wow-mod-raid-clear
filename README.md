# Raid Clear

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) module that teaches
[mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) bots the boss and trash
mechanics of each raid, one raid at a time.

[mod-dungeon-clear](https://github.com/jrad7/mod-dungeon-clear) already walks a bot raid from
boss to boss. As soon as a boss is pulled it steps back and leaves the fight to playerbots' own
raid strategies. In the TBC raids and ICC those are detailed, but in the vanilla raids most
bosses only get a resistance aura. This module fills in the missing mechanics.

## How it works

- Each raid has one strategy, named `rc <raid>`. Every bot on that raid's map gets it
  automatically and loses it when it leaves. Playerbots' own strategy for the raid keeps running
  alongside it.
- It doesn't need mod-dungeon-clear. The mechanics also apply when you lead the raid yourself.
- Neither mod-playerbots nor mod-dungeon-clear is edited. The module adds its strategies to
  playerbots at startup, the same way mod-dungeon-clear does.

### Kill order (all raids)

DPS bots always attack the skull-marked enemy first. The group's main-tank bot keeps the skull on
whichever add should die first: adds that split or multiply, then healers, then the boss's other
adds. A skull already on an add of the same or higher priority is left where it is, so the mark
doesn't flicker between equal adds. A skull you put on an add stays there too.

## Raids

### Molten Core

Playerbots already handles Living Bomb and Inferno, Shazzrah's Arcane Explosion, the Golemagg /
Core Rager tank split, Core Hound packs, lava pools and the resistance auras. On top of that:

| Fight | What the bots do |
|---|---|
| Firelord trash | Kill **Lava Spawn** before anything else (they split every 15 seconds). |
| Lucifron, Gehennas, Garr | Kill the Flamewaker Protectors, Flamewalkers and Firesworn before the boss. |
| Magmadar | Every shaman keeps **Tremor Totem** down against **Panic**. |
| Sulfuron Harbinger | Kill the Flamewaker Priests first. |
| Majordomo Executus | Healers first, then Elites. No spells into **Magic Reflection**; melee stop swinging into **Damage Reflection** (tanks keep going). |
| Ragnaros | Ranged and healers stay out of **Wrath of Ragnaros** range. Kill the **Sons of Flame** when he submerges. |

## Install

1. Clone into `modules/mod-raid-clear` (the folder name must match the loader function).
2. Re-run CMake and rebuild the worldserver.
3. Copy `conf/mod_raid_clear.conf.dist` to `mod_raid_clear.conf` to change the defaults.

Requires mod-playerbots with `AiPlayerbot.ApplyInstanceStrategies = 1` (the default), so the bots
also keep playerbots' own raid strategies.

## Config

| Option | Default | |
|---|---|---|
| `RaidClear.Enable` | 1 | Master switch (startup only). |
| `RaidClear.KillOrder` | 1 | Skull kill order. |
| `RaidClear.MoltenCore.Enable` | 1 | Molten Core strategy. |

## Patch Notes: Raid Clear

Category: Raids

- Bot raids now know more of Molten Core's boss mechanics.
- Bots kill the dangerous adds first: **Lava Spawn**, then the Flamewaker healers and priests,
  then each boss's guards, before turning to the boss.
- Shamans drop **Tremor Totem** for Magmadar's **Panic**.
- Bots stop attacking Majordomo's adds while **Magic Reflection** or **Damage Reflection** is up.
- Ranged bots and healers keep out of reach of **Wrath of Ragnaros**.

> Most vanilla bosses only had a resistance aura in the bots' playbook. This is the first raid
> of many.

## License

MIT
