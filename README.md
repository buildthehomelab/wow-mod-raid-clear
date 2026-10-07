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
- **Co-tanking bosses that drop threat.** Some bosses knock their tank away and wipe part of its
  threat (Broodlord Lashlayer's Knock Away, the Blackwing drakes' Wing Buffet). On those, an
  off-tank with no add to hold attacks the boss too, so it sits second on threat and catches the
  boss before it turns on a healer. The main tank taunts it back.
- **Endless adds are ignored.** Off-tanks don't chase adds that respawn faster than anyone can
  tank them (Blackwing Lair's Suppression Room whelps).

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

### Blackwing Lair

Playerbots already handles the Onyxia Scale Cloak, Razorgore's egg phase, Vaelastrasz's Burning
Adrenaline, Chromaggus's Bronze affliction, Nefarian's Wild Magic and Fear Ward, Wyrmguard spacing
and the fire resistance auras. On top of that:

| Fight | What the bots do |
|---|---|
| Razorgore | Kill his adds in order: **Death Talon Dragonspawn**, then **Blackwing Mages**, then **Legionnaires**. The off-tank split stays out of the fight (playerbots places the tanks). |
| Vaelastrasz | He can't be taunted, so the off-tank keeps itself second on threat for when **Burning Adrenaline** kills the main tank. |
| Warlock packs | Kill the **Blackwing Warlocks** first: each one keeps opening Demon Portals that summon Enraged Felguards until it dies. Then Taskmasters, Spellbinders and Death Talon Captains, then the felguards. |
| Death Talon packs | Casters (**Death Talon Wyrmkin**) first. Two **Wyrmguards** are tanked 30 yards apart (**War Stomp** reaches 15). |
| Technician packs | Ranged bots and healers keep 6 yards from each other while **Blackwing Technicians** are fighting nearby, so one **Bomb** (5-yard splash) hits one bot. Only bots in a clump move, one short step every 1.5 seconds. |
| Suppression Room | With the `raid` bot cheat on (the default), bots turn off every armed **Suppression Device** within 22 yards: the aura reaches 20, playerbots' own disarm only 15. Kill the Hatchers and Taskmasters before Broodlord if they come along. Off-tanks ignore the whelps. |
| Broodlord Lashlayer | The off-tank co-tanks him through **Knock Away**. Ranged and healers stay out of **Blast Wave** (20 yards); a healer only backs off as far as it can still reach the tank. |
| Firemaw, Ebonroc, Flamegor | The off-tank co-tanks through **Wing Buffet**. |
| Firemaw | At 5-7 stacks of **Flame Buffet** (varies per bot, so the raid doesn't leave together) a non-tank hides behind cover until the stacks drop. At most a third of the healers hide at once. Hidden bots keep healing and casting at anything they can see. |
| Chromaggus | When he starts a breath, non-tanks with cover within a 2-second run duck behind it. **Time Lapse** is the exception: everyone takes it, because it halves the threat of everyone it hits, tank included. |
| Nefarian | Ranged and healers stay out of **Bellowing Roar** (35-yard fear), healers only as far as they can still reach the tank. Kill the **Drakonids** and **Bone Constructs** before him. |
| Ebonroc | The off-tank taunts him off a tank with **Shadow of Ebonroc** (he heals on every hit on it), and that tank leaves him alone until it wears off. |

Disarmed Suppression Devices that keep slowing the raid are a core script bug that affects
players too; [mod-raid-bwl](https://github.com/buildthehomelab/wow-mod-raid-bwl) fixes it.

## Requirements

- An AzerothCore WotLK server running [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
  on its core fork
  ([mod-playerbots/azerothcore-wotlk](https://github.com/mod-playerbots/azerothcore-wotlk),
  `Playerbot` branch). The module uses playerbots APIs directly, so the stock core won't build it.
- `AiPlayerbot.ApplyInstanceStrategies = 1` in the playerbots config (the default), so the bots
  keep playerbots' own raid strategies alongside this module's.
- Optional: [mod-dungeon-clear](https://github.com/jrad7/mod-dungeon-clear), which walks a bot
  raid from boss to boss. The mechanics also apply when you lead the raid yourself.
- WoW 3.3.5a (12340) client.

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
| `RaidClear.BlackwingLair.Enable` | 1 | Blackwing Lair strategy. |

## Patch Notes: Raid Clear

Category: Raids

- Bot raids now know more of Molten Core's and Blackwing Lair's boss mechanics.
- Bot raids now have a main tank and off-tanks. Off-tanks pick up the mobs the main tank isn't
  holding and tank them a few steps away, so one cleave doesn't hit both tanks.
- Bots kill the dangerous adds first: **Lava Spawn**, then the Flamewaker healers and priests,
  then each boss's guards, before turning to the boss.
- Shamans drop **Tremor Totem** for Magmadar's **Panic**.
- Bots stop attacking Majordomo's adds while **Magic Reflection** or **Damage Reflection** is up.
- Ranged bots and healers keep out of reach of **Wrath of Ragnaros**.
- Blackwing Lair: bots kill the **Blackwing Warlocks** before the felguards their portals keep
  summoning, then the Taskmasters, Spellbinders and Death Talon Captains.
- Blackwing Lair: bots turn off **Suppression Devices** within the aura's full reach, not just the
  ones right next to them.
- Broodlord Lashlayer, Firemaw, Ebonroc and Flamegor: the off-tank builds threat on the boss, so
  **Knock Away** and **Wing Buffet** no longer send the boss onto the healers.
- Broodlord Lashlayer: ranged bots and healers stay out of **Blast Wave**.
- Ebonroc: bot tanks swap on **Shadow of Ebonroc**.
- Blackwing Lair: ranged bots and healers spread out against the Technicians' **Bombs**.
- Razorgore: bots kill the Dragonspawn, then the Mages, then the Legionnaires.
- Vaelastrasz: the off-tank keeps up threat so it can take over when **Burning Adrenaline** kills the
  main tank.
- Blackwing Lair: bots kill the Death Talon casters first and tank the Wyrmguards well apart.
- Firemaw: bots hide behind cover to drop **Flame Buffet** stacks, a few at a time, with healers
  taking turns.
- Chromaggus: bots duck behind cover when he breathes, except for **Time Lapse**.
- Nefarian: ranged bots and healers stay out of **Bellowing Roar**, and bots kill his adds first.

> Most vanilla bosses only had a resistance aura in the bots' playbook. This is the first raid
> of many.

## Troubleshooting

- **Bots don't use any of the raid mechanics.** Check that `AiPlayerbot.ApplyInstanceStrategies`
  is `1`, that `RaidClear.Enable` and the raid's own setting (`RaidClear.MoltenCore.Enable`,
  `RaidClear.BlackwingLair.Enable`) are `1`, and restart the worldserver: `RaidClear.Enable` is
  read at startup only.
- **The worldserver doesn't start or the module doesn't link.** The folder must be named exactly
  `mod-raid-clear`, because AzerothCore derives the loader function from it. Re-run CMake after
  cloning.
- **Off-tanks don't pull their mob away during a boss fight.** `RaidClear.Tanks.SeparateOnBosses`
  is `0` by default; separation only happens on trash.

## Credits

Built on [mod-dungeon-clear](https://github.com/jrad7/mod-dungeon-clear) by jrad7, whose way of
adding strategies to playerbots this module follows, and on
[mod-playerbots](https://github.com/mod-playerbots/mod-playerbots).

Author: [buildthehomelab](https://github.com/buildthehomelab)

## License

MIT. See [LICENSE](LICENSE).
