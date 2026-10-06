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

### Main tank and off-tanks (all raids)

`.botraid` builds raids with two or more tanks, but playerbots treats them all the same: each one
grabs whatever isn't hitting it, off-tanks taunt the main tank's mob, and every mob ends up in one
pile. In any raid:

- **One main tank.** If nobody has the group's Main Tank flag, the tank playerbots already treats
  as main gets it (first tank in group order). The main tank then sticks to its own target, and
  mod-dungeon-clear leads with the same tank. A Main Tank you set yourself is kept.
- **Off-tanks.** The other tanks never take the main tank's target. They pick up mobs hitting
  healers or DPS first, then extra mobs on the main tank, and leave mobs another off-tank already
  holds.
- **Separated on trash.** An off-tank drags its mob away from the main tank's mob, so cleaves and
  stomps hit only one tank: at least 12 yards apart, and further for mobs with a big AoE (in Molten
  Core, two Molten Giants end up 30 yards apart and two Molten Destroyers 36). It only picks dry
  ground (no lava) at about the same height and in the main tank's line of sight. In boss fights
  it stays put unless configured otherwise.

## Raids

### Molten Core

Playerbots already handles Living Bomb and Inferno, Shazzrah's Arcane Explosion, the Golemagg /
Core Rager tank split (the off-tank split stays out of that fight), Core Hound packs, lava pools and the resistance auras. On top of that:

| Fight | What the bots do |
|---|---|
| Firelord trash | Kill **Lava Spawn** before anything else (they split every 15 seconds). |
| Lucifron, Gehennas, Garr | Kill the Flamewaker Protectors, Flamewalkers and Firesworn before the boss. |
| Magmadar | Every shaman keeps **Tremor Totem** down against **Panic**. |
| Sulfuron Harbinger | Kill the Flamewaker Priests first. |
| Majordomo Executus | Healers first, then Elites. No spells into **Magic Reflection**; melee stop swinging into **Damage Reflection** (tanks keep going). |
| Ragnaros | Ranged and healers stay out of **Wrath of Ragnaros** range. Kill the **Sons of Flame** when he submerges. |

## Install

1. Clone into `modules/mod-raid-clear`. The folder name must be exactly that, because
   AzerothCore derives the loader function from it:

   ```bash
   git clone https://github.com/buildthehomelab/wow-mod-raid-clear.git modules/mod-raid-clear
   ```

2. Re-run CMake and rebuild the worldserver.
3. Copy `conf/mod_raid_clear.conf.dist` to `mod_raid_clear.conf` to change the defaults.

Requires mod-playerbots with `AiPlayerbot.ApplyInstanceStrategies = 1` (the default), so the bots
also keep playerbots' own raid strategies.

## Config

| Option | Default | |
|---|---|---|
| `RaidClear.Enable` | 1 | Master switch (startup only). |
| `RaidClear.KillOrder` | 1 | Skull kill order. |
| `RaidClear.Tanks.AssignMainTank` | 1 | Give the group's Main Tank flag to a tank if nobody has it. |
| `RaidClear.Tanks.Split` | 1 | Off-tanks leave the main tank's target and pick up the rest. |
| `RaidClear.Tanks.Separation` | 12 | Minimum yards between the two tanks' mobs on trash; big-AoE mobs get more (0 = off). |
| `RaidClear.Tanks.SeparateOnBosses` | 0 | Also separate during boss fights. |
| `RaidClear.MoltenCore.Enable` | 1 | Molten Core strategy. |

## Patch Notes: Raid Clear

Category: Raids

- Bot raids now know more of Molten Core's boss mechanics.
- Bot raids now have a main tank and off-tanks. Off-tanks pick up the mobs the main tank isn't
  holding and tank them a few steps away, so one cleave doesn't hit both tanks.
- Bots kill the dangerous adds first: **Lava Spawn**, then the Flamewaker healers and priests,
  then each boss's guards, before turning to the boss.
- Shamans drop **Tremor Totem** for Magmadar's **Panic**.
- Bots stop attacking Majordomo's adds while **Magic Reflection** or **Damage Reflection** is up.
- Ranged bots and healers keep out of reach of **Wrath of Ragnaros**.

> Most vanilla bosses only had a resistance aura in the bots' playbook. This is the first raid
> of many.

## License

MIT
